#include "CryptoStore.h"
#include "CryptoFile.h"
#include "Utils.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

#include <boost/json.hpp>

namespace cryptostore {

namespace fs = std::filesystem;

namespace {

boost::json::value jsonString( const std::string &text ) {
    return boost::json::value( boost::json::string_view( text.data(), text.size() ) );
}

std::int64_t toInt64( const boost::json::value &value ) {
    if ( value.is_int64() ) {
        return value.as_int64();
    }
    if ( value.is_uint64() ) {
        return static_cast<std::int64_t>( value.as_uint64() );
    }
    throw std::runtime_error( "not an integer" );
}

bool normalizeRelativePath( const std::string &text, std::string &out ) {
    out.clear();

    std::vector<std::string> parts;
    std::string token;
    for ( char c : text ) {
        if ( c == '/' || c == '\\' ) {
            if ( !token.empty() ) {
                parts.push_back( std::move( token ) );
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if ( !token.empty() ) {
        parts.push_back( std::move( token ) );
    }

    for ( const auto &part : parts ) {
        if ( part == "." ) {
            continue;
        }
        if ( part == ".." ) {
            return false;
        }
        if ( !out.empty() ) {
            out += '/';
        }
        out += part;
    }
    return true;
}

} // namespace

FileStream::FileStream( std::unique_ptr<CryptoFile> file, std::uint64_t remaining )
    : m_file( std::move( file ) ), m_remaining( remaining ) {
}

FileStream::~FileStream() = default;

bool FileStream::read( std::string &chunk, std::size_t maxSize ) {
    chunk.clear();

    if ( m_remaining == 0 ) {
        return false;
    }
    if ( !m_file ) {
        m_remaining = 0;
        return false;
    }

    std::size_t want = static_cast<std::size_t>(
        std::min<std::uint64_t>( m_remaining, static_cast<std::uint64_t>( maxSize ) ) );
    std::size_t got = m_file->read( chunk, want );
    if ( got == 0 ) {
        m_remaining = 0;
        return false;
    }

    m_remaining -= got;
    return true;
}

std::uint64_t FileStream::remaining() const {
    return m_remaining;
}

const std::string &FileStream::lastError() const {
    static const std::string empty;
    return m_file ? m_file->lastError() : empty;
}

Store::Store( fs::path dataDir )
    : m_dataDir( std::move( dataDir ) ) {
}

const fs::path &Store::dataDir() const {
    return m_dataDir;
}

std::vector<std::string> Store::listStores() const {
    std::vector<std::string> names;

    std::error_code ec;
    if ( !fs::exists( m_dataDir, ec ) || !fs::is_directory( m_dataDir, ec ) ) {
        return names;
    }

    for ( const auto &entry : fs::directory_iterator( m_dataDir, ec ) ) {
        if ( !entry.is_directory( ec ) ) {
            continue;
        }
        if ( !fs::exists( entry.path() / CONFIG_NAME, ec ) ) {
            continue;
        }
        names.push_back( entry.path().filename().u8string() );
    }

    std::sort( names.begin(), names.end() );
    return names;
}

bool Store::isValidName( const std::string &name ) {
    if ( name.empty() || name.size() > 128 ) {
        return false;
    }
    for ( char c : name ) {
        if ( c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
             c == '"' || c == '<' || c == '>' || c == '|' ) {
            return false;
        }
    }
    return name != "." && name != "..";
}

bool Store::encryptDirectory( const fs::path &sourceDir,
                              const std::string &password,
                              StoreInfo &info,
                              std::string &error,
                              const ProgressCallback &progress ) {
    if ( password.empty() ) {
        error = "密码不能为空";
        return false;
    }

    std::error_code ec;
    if ( !fs::exists( sourceDir, ec ) || !fs::is_directory( sourceDir, ec ) ) {
        error = "待加密目录不存在或不是目录: " + sourceDir.u8string();
        return false;
    }

    fs::path normalized = fs::weakly_canonical( sourceDir, ec );
    if ( ec ) {
        normalized = sourceDir;
        ec.clear();
    }

    std::string name = normalized.filename().u8string();
    if ( !isValidName( name ) ) {
        error = "目录名不合法: " + name;
        return false;
    }

    fs::path destDir = m_dataDir / utils::toPath( name );
    if ( fs::exists( destDir / CONFIG_NAME, ec ) ) {
        error = "同名加密目录已存在: " + name;
        return false;
    }

    std::vector<std::pair<std::string, fs::path>> files;
    std::uint64_t totalBytes = 0;
    for ( fs::recursive_directory_iterator it( sourceDir, ec ), end; it != end && !ec; it.increment( ec ) ) {
        if ( !it->is_regular_file( ec ) ) {
            continue;
        }
        std::string relative = fs::relative( it->path(), sourceDir, ec ).generic_u8string();
        if ( ec ) {
            ec.clear();
            continue;
        }
        auto fileSize = it->file_size( ec );
        if ( ec ) {
            ec.clear();
            fileSize = 0;
        }
        totalBytes += fileSize;
        files.emplace_back( std::move( relative ), it->path() );
    }
    if ( ec ) {
        error = "遍历目录失败: " + ec.message();
        return false;
    }

    std::sort( files.begin(), files.end(), []( const auto &a, const auto &b ) {
        return a.first < b.first;
    } );

    if ( fs::exists( destDir, ec ) ) {
        fs::remove_all( destDir, ec );
    }

    fs::create_directories( destDir, ec );
    if ( ec ) {
        error = "无法创建目录: " + destDir.u8string();
        return false;
    }

    info = StoreInfo{};
    info.name = name;
    info.created = static_cast<std::int64_t>( std::time( nullptr ) );

    Progress state;
    state.filesTotal = files.size();
    state.bytesTotal = totalBytes;
    if ( progress ) {
        progress( state );
    }

    std::vector<char> buffer( 1u << 20 );
    int id = 1;

    for ( const auto &[relative, sourcePath] : files ) {
        fs::path target = destDir / ( std::to_string( id ) + DATA_SUFFIX );

        state.current = relative;
        if ( progress ) {
            progress( state );
        }

        CryptoFile out( target, password, CryptoFile::Mode::Write );
        if ( !out.isOpen() ) {
            error = "加密失败: " + out.lastError();
            fs::remove_all( destDir, ec );
            return false;
        }

        std::ifstream in( sourcePath, std::ios::binary );
        if ( !in.is_open() ) {
            error = "无法读取文件: " + sourcePath.u8string();
            fs::remove_all( destDir, ec );
            return false;
        }

        while ( in ) {
            in.read( buffer.data(), static_cast<std::streamsize>( buffer.size() ) );
            auto got = in.gcount();
            if ( got <= 0 ) {
                break;
            }
            if ( !out.write( buffer.data(), static_cast<std::size_t>( got ) ) ) {
                error = "加密写入失败: " + out.lastError();
                fs::remove_all( destDir, ec );
                return false;
            }

            state.bytesDone += static_cast<std::uint64_t>( got );
            if ( progress ) {
                progress( state );
            }
        }

        if ( in.bad() ) {
            error = "读取文件失败: " + sourcePath.u8string();
            fs::remove_all( destDir, ec );
            return false;
        }

        out.close();
        if ( !out.lastError().empty() ) {
            error = "加密写入失败: " + out.lastError();
            fs::remove_all( destDir, ec );
            return false;
        }

        FileEntry entry;
        entry.id = id;
        entry.path = relative;
        entry.size = static_cast<std::uint64_t>( fs::file_size( sourcePath, ec ) );
        if ( ec ) {
            entry.size = 0;
            ec.clear();
        }
        info.files.push_back( std::move( entry ) );

        ++id;
        ++state.filesDone;
        if ( progress ) {
            progress( state );
        }
    }

    if ( !writeConfig( name, password, info, error ) ) {
        fs::remove_all( destDir, ec );
        return false;
    }

    return true;
}

bool Store::openStore( const std::string &name,
                       const std::string &password,
                       StoreInfo &info,
                       std::string &error ) const {
    if ( !isValidName( name ) ) {
        error = "目录名不合法";
        return false;
    }
    if ( password.empty() ) {
        error = "密码不能为空";
        return false;
    }

    fs::path configPath = m_dataDir / utils::toPath( name ) / CONFIG_NAME;
    std::error_code ec;
    if ( !fs::exists( configPath, ec ) ) {
        error = "加密目录不存在: " + name;
        return false;
    }

    CryptoFile config( configPath, password, CryptoFile::Mode::Read );
    if ( !config.isOpen() ) {
        error = config.lastError();
        return false;
    }

    std::string text = config.readAll();
    if ( !config.lastError().empty() ) {
        error = config.lastError();
        return false;
    }

    StoreInfo parsed;
    if ( !parseConfig( text, parsed, error ) ) {
        return false;
    }

    parsed.name = name;
    info = std::move( parsed );
    return true;
}

bool Store::readFile( const std::string &name,
                      const FileEntry &entry,
                      const std::string &password,
                      std::string &content,
                      std::string &error ) const {
    return readFile( name, entry, password, 0, entry.size, content, error );
}

bool Store::readFile( const std::string &name,
                      const FileEntry &entry,
                      const std::string &password,
                      std::uint64_t offset,
                      std::uint64_t length,
                      std::string &content,
                      std::string &error ) const {
    content.clear();

    if ( offset >= entry.size ) {
        return true;
    }
    if ( length > entry.size - offset ) {
        length = entry.size - offset;
    }

    fs::path filePath = m_dataDir / utils::toPath( name ) / ( std::to_string( entry.id ) + DATA_SUFFIX );

    CryptoFile file( filePath, password, CryptoFile::Mode::Read );
    if ( !file.isOpen() ) {
        error = file.lastError();
        return false;
    }

    if ( offset > 0 && !file.seek( offset ) ) {
        error = file.lastError();
        return false;
    }

    content.reserve( static_cast<std::size_t>( length ) );

    std::string chunk;
    std::uint64_t remaining = length;
    while ( remaining > 0 ) {
        std::size_t want = static_cast<std::size_t>( std::min<std::uint64_t>( remaining, 1u << 20 ) );
        std::size_t got = file.read( chunk, want );
        if ( got == 0 ) {
            break;
        }
        content.append( chunk );
        remaining -= got;
    }

    if ( !file.lastError().empty() ) {
        content.clear();
        error = file.lastError();
        return false;
    }

    return true;
}

std::shared_ptr<FileStream> Store::openStream( const std::string &name,
                                               const FileEntry &entry,
                                               const std::string &password,
                                               std::uint64_t offset,
                                               std::uint64_t length,
                                               std::string &error ) const {
    error.clear();

    if ( offset >= entry.size ) {
        return std::make_shared<FileStream>( nullptr, 0 );
    }
    if ( length > entry.size - offset ) {
        length = entry.size - offset;
    }
    if ( length == 0 ) {
        return std::make_shared<FileStream>( nullptr, 0 );
    }

    fs::path filePath = m_dataDir / utils::toPath( name ) / ( std::to_string( entry.id ) + DATA_SUFFIX );

    auto file = std::make_unique<CryptoFile>( filePath, password, CryptoFile::Mode::Read );
    if ( !file->isOpen() ) {
        error = file->lastError();
        return nullptr;
    }

    if ( offset > 0 && !file->seek( offset ) ) {
        error = file->lastError();
        return nullptr;
    }

    return std::make_shared<FileStream>( std::move( file ), length );
}

bool Store::writeConfig( const std::string &name,
                         const std::string &password,
                         const StoreInfo &info,
                         std::string &error ) const {
    fs::path configPath = m_dataDir / utils::toPath( name ) / CONFIG_NAME;

    CryptoFile config( configPath, password, CryptoFile::Mode::Write );
    if ( !config.isOpen() ) {
        error = "写入配置文件失败: " + config.lastError();
        return false;
    }
    if ( !config.write( serializeConfig( info ) ) ) {
        error = "写入配置文件失败: " + config.lastError();
        return false;
    }
    config.close();
    if ( !config.lastError().empty() ) {
        error = "写入配置文件失败: " + config.lastError();
        return false;
    }
    return true;
}

bool Store::addPath( const std::string &name,
                     const std::string &password,
                     const std::string &relativeDir,
                     const fs::path &sourcePath,
                     StoreInfo &info,
                     std::string &error,
                     const ProgressCallback &progress ) {
    if ( !isValidName( name ) ) {
        error = "目录名不合法";
        return false;
    }
    if ( password.empty() ) {
        error = "密码不能为空";
        return false;
    }

    std::error_code ec;
    if ( !fs::exists( sourcePath, ec ) ) {
        error = "路径不存在: " + sourcePath.u8string();
        return false;
    }

    std::string targetDir;
    if ( !normalizeRelativePath( relativeDir, targetDir ) ) {
        error = "目标目录不合法";
        return false;
    }

    StoreInfo current;
    std::string openError;
    if ( !openStore( name, password, current, openError ) ) {
        error = openError;
        return false;
    }

    std::vector<std::pair<std::string, fs::path>> incoming;
    if ( fs::is_directory( sourcePath, ec ) ) {
        std::string rootName = sourcePath.filename().u8string();
        if ( rootName.empty() ) {
            rootName = sourcePath.parent_path().filename().u8string();
        }
        std::string targetRoot = targetDir.empty() ? rootName : targetDir + "/" + rootName;

        for ( fs::recursive_directory_iterator it( sourcePath, ec ), end; it != end && !ec; it.increment( ec ) ) {
            if ( !it->is_regular_file( ec ) ) {
                ec.clear();
                continue;
            }
            std::string relative = fs::relative( it->path(), sourcePath, ec ).generic_u8string();
            if ( ec ) {
                ec.clear();
                continue;
            }
            incoming.emplace_back( targetRoot + "/" + relative, it->path() );
        }
        if ( ec ) {
            error = "遍历目录失败: " + ec.message();
            return false;
        }
    } else if ( fs::is_regular_file( sourcePath, ec ) ) {
        std::string fileName = sourcePath.filename().u8string();
        incoming.emplace_back( targetDir.empty() ? fileName : targetDir + "/" + fileName, sourcePath );
    } else {
        error = "不是可添加的文件或目录: " + sourcePath.u8string();
        return false;
    }

    if ( incoming.empty() ) {
        error = "目录中没有可添加的文件";
        return false;
    }

    std::sort( incoming.begin(), incoming.end(), []( const auto &a, const auto &b ) {
        return a.first < b.first;
    } );

    std::set<std::string> existing;
    int nextId = 1;
    for ( const auto &entry : current.files ) {
        existing.insert( entry.path );
        nextId = std::max( nextId, entry.id + 1 );
    }
    for ( const auto &item : incoming ) {
        if ( existing.count( item.first ) ) {
            error = "目标位置已存在同名文件: " + item.first;
            return false;
        }
    }

    fs::path destDir = m_dataDir / utils::toPath( name );
    StoreInfo updated = current;
    std::vector<int> written;

    Progress state;
    state.filesTotal = incoming.size();
    for ( const auto &[relative, filePath] : incoming ) {
        auto fileSize = fs::file_size( filePath, ec );
        if ( ec ) {
            ec.clear();
            fileSize = 0;
        }
        state.bytesTotal += fileSize;
    }
    if ( progress ) {
        progress( state );
    }

    std::vector<char> buffer( 1u << 20 );
    for ( const auto &[relative, filePath] : incoming ) {
        int id = nextId++;
        fs::path target = destDir / ( std::to_string( id ) + DATA_SUFFIX );

        state.current = relative;
        if ( progress ) {
            progress( state );
        }

        CryptoFile out( target, password, CryptoFile::Mode::Write );
        if ( !out.isOpen() ) {
            error = "加密失败: " + out.lastError();
            break;
        }
        written.push_back( id );

        std::ifstream in( filePath, std::ios::binary );
        if ( !in.is_open() ) {
            error = "无法读取文件: " + filePath.u8string();
            break;
        }

        while ( in ) {
            in.read( buffer.data(), static_cast<std::streamsize>( buffer.size() ) );
            auto got = in.gcount();
            if ( got <= 0 ) {
                break;
            }
            if ( !out.write( buffer.data(), static_cast<std::size_t>( got ) ) ) {
                break;
            }
            state.bytesDone += static_cast<std::uint64_t>( got );
            if ( progress ) {
                progress( state );
            }
        }

        if ( !out.lastError().empty() ) {
            out.close();
            error = "加密写入失败: " + out.lastError();
            break;
        }
        if ( in.bad() ) {
            out.close();
            error = "读取文件失败: " + filePath.u8string();
            break;
        }

        out.close();
        if ( !out.lastError().empty() ) {
            error = "加密写入失败: " + out.lastError();
            break;
        }

        FileEntry entry;
        entry.id = id;
        entry.path = relative;
        entry.size = static_cast<std::uint64_t>( fs::file_size( filePath, ec ) );
        if ( ec ) {
            entry.size = 0;
            ec.clear();
        }
        updated.files.push_back( std::move( entry ) );

        ++state.filesDone;
        if ( progress ) {
            progress( state );
        }
    }

    if ( !error.empty() ) {
        for ( int id : written ) {
            fs::remove( destDir / ( std::to_string( id ) + DATA_SUFFIX ), ec );
            ec.clear();
        }
        return false;
    }

    if ( !writeConfig( name, password, updated, error ) ) {
        for ( int id : written ) {
            fs::remove( destDir / ( std::to_string( id ) + DATA_SUFFIX ), ec );
            ec.clear();
        }
        return false;
    }

    info = std::move( updated );
    return true;
}

bool Store::removePath( const std::string &name,
                        const std::string &password,
                        const std::string &relativePath,
                        StoreInfo &info,
                        std::string &error,
                        const ProgressCallback &progress ) {
    if ( !isValidName( name ) ) {
        error = "目录名不合法";
        return false;
    }
    if ( password.empty() ) {
        error = "密码不能为空";
        return false;
    }

    std::string target;
    if ( !normalizeRelativePath( relativePath, target ) || target.empty() ) {
        error = "路径不合法";
        return false;
    }

    StoreInfo current;
    std::string openError;
    if ( !openStore( name, password, current, openError ) ) {
        error = openError;
        return false;
    }

    std::string prefix = target + "/";
    StoreInfo updated;
    updated.name = current.name;
    updated.created = current.created;

    std::vector<std::pair<int, std::string>> removed;
    for ( const auto &entry : current.files ) {
        if ( entry.path == target || entry.path.compare( 0, prefix.size(), prefix ) == 0 ) {
            removed.emplace_back( entry.id, entry.path );
        } else {
            updated.files.push_back( entry );
        }
    }

    if ( removed.empty() ) {
        error = "未找到文件或目录: " + target;
        return false;
    }

    Progress state;
    state.filesTotal = removed.size();
    state.bytesTotal = 0;
    if ( progress ) {
        progress( state );
    }

    if ( !writeConfig( name, password, updated, error ) ) {
        return false;
    }

    fs::path destDir = m_dataDir / utils::toPath( name );
    std::error_code ec;
    for ( const auto &[id, path] : removed ) {
        state.current = path;
        if ( progress ) {
            progress( state );
        }
        fs::remove( destDir / ( std::to_string( id ) + DATA_SUFFIX ), ec );
        ec.clear();
        ++state.filesDone;
        if ( progress ) {
            progress( state );
        }
    }

    info = std::move( updated );
    return true;
}

bool Store::exportPath( const std::string &name,
                        const std::string &password,
                        const std::string &relativePath,
                        const fs::path &destRoot,
                        std::string &error,
                        const ProgressCallback &progress ) const {
    if ( !isValidName( name ) ) {
        error = "目录名不合法";
        return false;
    }
    if ( password.empty() ) {
        error = "密码不能为空";
        return false;
    }

    std::string target;
    if ( !normalizeRelativePath( relativePath, target ) ) {
        error = "路径不合法";
        return false;
    }

    StoreInfo current;
    std::string openError;
    if ( !openStore( name, password, current, openError ) ) {
        error = openError;
        return false;
    }

    std::string prefix = target.empty() ? std::string() : target + "/";
    std::vector<FileEntry> selected;
    for ( const auto &entry : current.files ) {
        if ( target.empty() || entry.path == target ||
             entry.path.compare( 0, prefix.size(), prefix ) == 0 ) {
            selected.push_back( entry );
        }
    }

    if ( selected.empty() ) {
        error = "该目录下没有可导出的文件: " + ( target.empty() ? name : target );
        return false;
    }

    std::sort( selected.begin(), selected.end(), []( const FileEntry &a, const FileEntry &b ) {
        return a.path < b.path;
    } );

    std::error_code ec;
    fs::create_directories( destRoot, ec );
    if ( ec ) {
        error = "无法创建导出目录: " + destRoot.u8string() + " (" + ec.message() + ")";
        return false;
    }

    Progress state;
    state.filesTotal = selected.size();
    for ( const auto &entry : selected ) {
        state.bytesTotal += entry.size;
    }
    if ( progress ) {
        progress( state );
    }

    std::vector<fs::path> written;

    for ( const auto &entry : selected ) {
        state.current = entry.path;
        if ( progress ) {
            progress( state );
        }

        fs::path outPath = destRoot / utils::toPath( entry.path );
        fs::create_directories( outPath.parent_path(), ec );
        if ( ec ) {
            error = "无法创建导出目录: " + outPath.parent_path().u8string() + " (" + ec.message() + ")";
            break;
        }

        fs::path srcPath = m_dataDir / utils::toPath( name ) / ( std::to_string( entry.id ) + DATA_SUFFIX );
        CryptoFile in( srcPath, password, CryptoFile::Mode::Read );
        if ( !in.isOpen() ) {
            error = "解密失败: " + entry.path + " (" + in.lastError() + ")";
            break;
        }

        std::ofstream out( outPath, std::ios::binary | std::ios::trunc );
        if ( !out.is_open() ) {
            error = "无法写入文件: " + outPath.u8string();
            break;
        }
        written.push_back( outPath );

        std::string chunk;
        std::uint64_t remaining = entry.size;
        bool failed = false;
        while ( remaining > 0 ) {
            std::size_t want = static_cast<std::size_t>(
                std::min<std::uint64_t>( remaining, 1u << 20 ) );
            std::size_t got = in.read( chunk, want );
            if ( got == 0 ) {
                break;
            }
            out.write( chunk.data(), static_cast<std::streamsize>( got ) );
            if ( !out.good() ) {
                failed = true;
                break;
            }
            remaining -= got;
            state.bytesDone += static_cast<std::uint64_t>( got );
            if ( progress ) {
                progress( state );
            }
        }
        out.close();

        if ( failed ) {
            error = "写入导出文件失败: " + outPath.u8string();
            break;
        }
        if ( !in.lastError().empty() ) {
            error = "解密失败: " + entry.path + " (" + in.lastError() + ")";
            break;
        }
        if ( remaining > 0 ) {
            error = "解密数据不完整: " + entry.path;
            break;
        }

        ++state.filesDone;
        if ( progress ) {
            progress( state );
        }
    }

    if ( !error.empty() ) {
        for ( const auto &path : written ) {
            fs::remove( path, ec );
            ec.clear();
        }
        return false;
    }

    return true;
}

std::string Store::serializeConfig( const StoreInfo &info ) {
    boost::json::object root;
    root["version"] = 1;
    root["name"] = jsonString( info.name );
    root["created"] = info.created > 0 ? info.created : static_cast<std::int64_t>( std::time( nullptr ) );

    boost::json::array files;
    for ( const auto &entry : info.files ) {
        boost::json::object item;
        item["id"] = entry.id;
        item["path"] = jsonString( entry.path );
        item["size"] = entry.size;
        files.push_back( std::move( item ) );
    }
    root["files"] = std::move( files );

    return boost::json::serialize( root );
}

bool Store::parseConfig( const std::string &text, StoreInfo &info, std::string &error ) {
    try {
        auto value = boost::json::parse( text );
        auto &root = value.as_object();

        info = StoreInfo{};
        if ( auto *name = root.if_contains( "name" ) ) {
            if ( name->is_string() ) {
                info.name = std::string( name->as_string() );
            }
        }
        if ( auto *created = root.if_contains( "created" ) ) {
            try {
                info.created = toInt64( *created );
            } catch ( const std::exception & ) {
                info.created = 0;
            }
        }

        auto *files = root.if_contains( "files" );
        if ( !files || !files->is_array() ) {
            error = "配置文件缺少文件列表";
            return false;
        }

        for ( auto &item : files->as_array() ) {
            auto &obj = item.as_object();
            FileEntry entry;

            auto *id = obj.if_contains( "id" );
            auto *path = obj.if_contains( "path" );
            auto *size = obj.if_contains( "size" );
            if ( !id || !path || !size ) {
                error = "配置文件格式错误";
                return false;
            }

            entry.id = static_cast<int>( toInt64( *id ) );
            entry.path = std::string( path->as_string() );
            entry.size = static_cast<std::uint64_t>( toInt64( *size ) );
            info.files.push_back( std::move( entry ) );
        }

        return true;
    } catch ( const std::exception & ) {
        error = "配置文件已损坏或密码错误";
        return false;
    }
}

} // namespace cryptostore