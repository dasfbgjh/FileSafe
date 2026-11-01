#include "CryptoFile.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

namespace {
constexpr unsigned char MAGIC[4] = { 'C', 'F', 'S', '1' };
}

CryptoFile::CryptoFile( const std::filesystem::path &path, const std::string &password, Mode mode )
    : m_mode( mode ),
      m_ctx( nullptr ),
      m_open( false ) {
    if ( m_mode == Mode::Write ) {
        openForWrite( path, password );
    } else {
        openForRead( path, password );
    }
}

CryptoFile::~CryptoFile() {
    close();
}

bool CryptoFile::isOpen() const {
    return m_open;
}

bool CryptoFile::good() const {
    return m_open && m_file.good();
}

CryptoFile::operator bool() const {
    return good();
}

void CryptoFile::close() {
    if ( m_ctx ) {
        if ( m_open && m_mode == Mode::Write ) {
            unsigned char output[16];
            int outputLen = 0;
            EVP_EncryptFinal_ex( m_ctx, output, &outputLen );
        }
        releaseCipher();
    }
    if ( m_file.is_open() ) {
        m_file.flush();
        m_file.close();
    }
    OPENSSL_cleanse( m_key.data(), m_key.size() );
    m_open = false;
}

bool CryptoFile::write( const void *data, std::size_t size ) {
    if ( !m_open || m_mode != Mode::Write ) {
        setError( "文件未以写入模式打开" );
        return false;
    }
    if ( size == 0 ) {
        return true;
    }
    if ( !data ) {
        setError( "写入数据为空" );
        return false;
    }

    const unsigned char *source = static_cast<const unsigned char *>( data );
    std::vector<unsigned char> buffer;

    while ( size > 0 ) {
        std::size_t chunk = std::min( size, CHUNK_SIZE );
        buffer.resize( chunk );

        int outputLen = 0;
        if ( EVP_EncryptUpdate( m_ctx, buffer.data(), &outputLen, source, static_cast<int>( chunk ) ) != 1 ) {
            setError( "加密失败" );
            return false;
        }

        m_file.write( reinterpret_cast<const char *>( buffer.data() ), outputLen );
        if ( !m_file.good() ) {
            setError( "写入文件失败" );
            return false;
        }

        source += chunk;
        size -= chunk;
    }

    return true;
}

bool CryptoFile::write( const std::string &data ) {
    return write( data.data(), data.size() );
}

std::size_t CryptoFile::read( void *data, std::size_t size ) {
    if ( !m_open || m_mode != Mode::Read ) {
        setError( "文件未以读取模式打开" );
        return 0;
    }
    if ( size == 0 || !data ) {
        return 0;
    }

    std::vector<unsigned char> buffer( std::min( size, CHUNK_SIZE ) );
    std::size_t total = 0;

    while ( size > 0 ) {
        std::size_t chunk = std::min( size, CHUNK_SIZE );
        m_file.read( reinterpret_cast<char *>( buffer.data() ), static_cast<std::streamsize>( chunk ) );

        std::size_t got = static_cast<std::size_t>( m_file.gcount() );
        if ( got == 0 ) {
            break;
        }

        int outputLen = 0;
        if ( EVP_DecryptUpdate( m_ctx, static_cast<unsigned char *>( data ) + total, &outputLen, buffer.data(), static_cast<int>( got ) ) != 1 ) {
            setError( "解密失败" );
            return total;
        }

        total += static_cast<std::size_t>( outputLen );
        size -= got;

        if ( got < chunk ) {
            break;
        }
    }

    return total;
}

std::size_t CryptoFile::read( std::string &data, std::size_t size ) {
    data.resize( size );
    std::size_t got = read( data.data(), size );
    data.resize( got );
    return got;
}

std::string CryptoFile::readAll() {
    std::string result;
    std::string chunk;
    while ( read( chunk, CHUNK_SIZE ) > 0 ) {
        result += chunk;
    }
    return result;
}

bool CryptoFile::seek( std::uint64_t offset ) {
    if ( !m_open || m_mode != Mode::Read ) {
        setError( "文件未以读取模式打开" );
        return false;
    }
    if ( !m_ctx ) {
        setError( "加密上下文未就绪" );
        return false;
    }

    std::uint64_t block = offset / BLOCK_SIZE;
    std::uint64_t rest = offset % BLOCK_SIZE;

    std::array<unsigned char, IV_SIZE> iv = m_iv;
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    for ( std::size_t i = 0; i < 8; ++i ) {
        high = ( high << 8 ) | static_cast<std::uint64_t>( iv[i] );
        low = ( low << 8 ) | static_cast<std::uint64_t>( iv[8 + i] );
    }

    std::uint64_t newLow = low + block;
    if ( newLow < low ) {
        ++high;
    }
    for ( std::size_t i = 0; i < 8; ++i ) {
        iv[i] = static_cast<unsigned char>( ( high >> ( 56 - 8 * i ) ) & 0xFFu );
        iv[8 + i] = static_cast<unsigned char>( ( newLow >> ( 56 - 8 * i ) ) & 0xFFu );
    }

    if ( EVP_DecryptInit_ex( m_ctx, EVP_aes_256_ctr(), nullptr, m_key.data(), iv.data() ) != 1 ) {
        setError( "初始化解密上下文失败" );
        return false;
    }

    m_file.clear();
    m_file.seekg( static_cast<std::streamoff>( HEADER_SIZE + block * BLOCK_SIZE ), std::ios::beg );
    if ( !m_file.good() ) {
        setError( "定位文件失败" );
        return false;
    }

    if ( rest > 0 ) {
        std::vector<unsigned char> skip( static_cast<std::size_t>( rest ) );
        m_file.read( reinterpret_cast<char *>( skip.data() ), static_cast<std::streamsize>( skip.size() ) );
        if ( static_cast<std::size_t>( m_file.gcount() ) != skip.size() ) {
            setError( "定位文件失败" );
            return false;
        }

        std::vector<unsigned char> discard( skip.size() );
        int outputLen = 0;
        if ( EVP_DecryptUpdate( m_ctx, discard.data(), &outputLen, skip.data(), static_cast<int>( skip.size() ) ) != 1 ) {
            setError( "解密失败" );
            return false;
        }
    }

    return true;
}

const std::string &CryptoFile::lastError() const {
    return m_error;
}

bool CryptoFile::openForWrite( const std::filesystem::path &path, const std::string &password ) {
    m_file.open( path, std::ios::binary | std::ios::out | std::ios::trunc );
    if ( !m_file.is_open() ) {
        setError( "无法打开文件进行写入: " + path.u8string() );
        return false;
    }

    if ( RAND_bytes( m_salt.data(), static_cast<int>( SALT_SIZE ) ) != 1 ||
         RAND_bytes( m_iv.data(), static_cast<int>( IV_SIZE ) ) != 1 ) {
        setError( "生成随机数失败" );
        return false;
    }

    if ( !deriveKey( password, m_salt.data() ) ) {
        return false;
    }

    unsigned int verifyLen = 0;
    HMAC( EVP_sha256(), m_key.data(), static_cast<int>( m_key.size() ),
          m_salt.data(), SALT_SIZE, m_verify.data(), &verifyLen );

    m_ctx = EVP_CIPHER_CTX_new();
    if ( !m_ctx ) {
        setError( "加密上下文分配失败" );
        return false;
    }
    if ( EVP_EncryptInit_ex( m_ctx, EVP_aes_256_ctr(), nullptr, m_key.data(), m_iv.data() ) != 1 ) {
        setError( "初始化加密上下文失败" );
        return false;
    }

    if ( !writeHeader() ) {
        return false;
    }

    m_open = true;
    return true;
}

bool CryptoFile::openForRead( const std::filesystem::path &path, const std::string &password ) {
    m_file.open( path, std::ios::binary | std::ios::in );
    if ( !m_file.is_open() ) {
        setError( "无法打开文件进行读取: " + path.u8string() );
        return false;
    }

    if ( !readHeader() ) {
        return false;
    }

    if ( !deriveKey( password, m_salt.data() ) ) {
        return false;
    }

    std::array<unsigned char, VERIFY_SIZE> verify = {};
    unsigned int verifyLen = 0;
    HMAC( EVP_sha256(), m_key.data(), static_cast<int>( m_key.size() ),
          m_salt.data(), SALT_SIZE, verify.data(), &verifyLen );

    if ( CRYPTO_memcmp( verify.data(), m_verify.data(), VERIFY_SIZE ) != 0 ) {
        setError( "密码错误或文件已损坏" );
        return false;
    }

    m_ctx = EVP_CIPHER_CTX_new();
    if ( !m_ctx ) {
        setError( "解密上下文分配失败" );
        return false;
    }
    if ( EVP_DecryptInit_ex( m_ctx, EVP_aes_256_ctr(), nullptr, m_key.data(), m_iv.data() ) != 1 ) {
        setError( "初始化解密上下文失败" );
        return false;
    }

    m_open = true;
    return true;
}

bool CryptoFile::writeHeader() {
    m_file.write( reinterpret_cast<const char *>( MAGIC ), sizeof( MAGIC ) );
    m_file.write( reinterpret_cast<const char *>( m_salt.data() ), static_cast<std::streamsize>( SALT_SIZE ) );
    m_file.write( reinterpret_cast<const char *>( m_iv.data() ), static_cast<std::streamsize>( IV_SIZE ) );
    m_file.write( reinterpret_cast<const char *>( m_verify.data() ), static_cast<std::streamsize>( VERIFY_SIZE ) );

    if ( !m_file.good() ) {
        setError( "写入文件头失败" );
        return false;
    }
    return true;
}

bool CryptoFile::readHeader() {
    std::array<unsigned char, HEADER_SIZE> header = {};

    m_file.read( reinterpret_cast<char *>( header.data() ), static_cast<std::streamsize>( HEADER_SIZE ) );
    if ( static_cast<std::size_t>( m_file.gcount() ) != HEADER_SIZE ) {
        setError( "文件头长度错误" );
        return false;
    }
    if ( std::memcmp( header.data(), MAGIC, sizeof( MAGIC ) ) != 0 ) {
        setError( "无效文件格式" );
        return false;
    }

    std::memcpy( m_salt.data(), header.data() + sizeof( MAGIC ), SALT_SIZE );
    std::memcpy( m_iv.data(), header.data() + sizeof( MAGIC ) + SALT_SIZE, IV_SIZE );
    std::memcpy( m_verify.data(), header.data() + sizeof( MAGIC ) + SALT_SIZE + IV_SIZE, VERIFY_SIZE );

    return true;
}

bool CryptoFile::deriveKey( const std::string &password, const unsigned char *salt ) {
    if ( PKCS5_PBKDF2_HMAC( password.c_str(), static_cast<int>( password.size() ),
                            salt, static_cast<int>( SALT_SIZE ), KEY_ITERATIONS,
                            EVP_sha256(), static_cast<int>( m_key.size() ), m_key.data() ) != 1 ) {
        setError( "密钥派生失败" );
        return false;
    }
    return true;
}

void CryptoFile::setError( const std::string &message ) {
    m_error = message;
}

void CryptoFile::releaseCipher() {
    if ( m_ctx ) {
        EVP_CIPHER_CTX_free( m_ctx );
        m_ctx = nullptr;
    }
}