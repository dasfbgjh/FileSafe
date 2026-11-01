#ifndef CRYPTOFILE_HPP
#define CRYPTOFILE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include <openssl/evp.h>

class CryptoFile {
public:
    enum class Mode {
        Read,
        Write
    };

    CryptoFile( const std::filesystem::path &path, const std::string &password, Mode mode );
    ~CryptoFile();

    CryptoFile( const CryptoFile &other ) = delete;
    CryptoFile &operator=( const CryptoFile &other ) = delete;
    CryptoFile( CryptoFile &&other ) = delete;
    CryptoFile &operator=( CryptoFile &&other ) = delete;

    bool isOpen() const;
    bool good() const;
    explicit operator bool() const;

    void close();

    bool write( const void *data, std::size_t size );
    bool write( const std::string &data );

    std::size_t read( void *data, std::size_t size );
    std::size_t read( std::string &data, std::size_t size );
    std::string readAll();

    bool seek( std::uint64_t offset );

    const std::string &lastError() const;

private:
    bool openForWrite( const std::filesystem::path &path, const std::string &password );
    bool openForRead( const std::filesystem::path &path, const std::string &password );
    bool writeHeader();
    bool readHeader();
    bool deriveKey( const std::string &password, const unsigned char *salt );
    void setError( const std::string &message );
    void releaseCipher();

private:
    static constexpr std::size_t SALT_SIZE = 16;
    static constexpr std::size_t IV_SIZE = 16;
    static constexpr std::size_t BLOCK_SIZE = 16;
    static constexpr std::size_t VERIFY_SIZE = 32;
    static constexpr std::size_t HEADER_SIZE = 4 + SALT_SIZE + IV_SIZE + VERIFY_SIZE;
    static constexpr int KEY_ITERATIONS = 100000;
    static constexpr std::size_t CHUNK_SIZE = 1u << 20;

    std::fstream m_file;
    Mode m_mode;
    EVP_CIPHER_CTX *m_ctx;
    std::array<unsigned char, SALT_SIZE> m_salt;
    std::array<unsigned char, IV_SIZE> m_iv;
    std::array<unsigned char, 32> m_key;
    std::array<unsigned char, VERIFY_SIZE> m_verify;
    bool m_open;
    std::string m_error;
};

#endif