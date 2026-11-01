#ifndef CRYPTOSTORE_HPP
#define CRYPTOSTORE_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CryptoFile;

namespace cryptostore {

struct FileEntry {
    int id = 0;
    std::string path;
    std::uint64_t size = 0;
};

struct StoreInfo {
    std::string name;
    std::int64_t created = 0;
    std::vector<FileEntry> files;
};

struct Progress {
    std::size_t filesDone = 0;
    std::size_t filesTotal = 0;
    std::uint64_t bytesDone = 0;
    std::uint64_t bytesTotal = 0;
    std::string current;
};

using ProgressCallback = std::function<void( const Progress & )>;

class FileStream {
public:
    FileStream( std::unique_ptr<CryptoFile> file, std::uint64_t remaining );

    ~FileStream();

    FileStream( const FileStream & ) = delete;
    FileStream &operator=( const FileStream & ) = delete;
    FileStream( FileStream && ) = delete;
    FileStream &operator=( FileStream && ) = delete;

    bool read( std::string &chunk, std::size_t maxSize );

    std::uint64_t remaining() const;

    const std::string &lastError() const;

private:
    std::unique_ptr<CryptoFile> m_file;
    std::uint64_t m_remaining;
};

class Store {
public:
    explicit Store( std::filesystem::path dataDir );

    const std::filesystem::path &dataDir() const;

    std::vector<std::string> listStores() const;

    bool encryptDirectory( const std::filesystem::path &sourceDir,
                           const std::string &password,
                           StoreInfo &info,
                           std::string &error,
                           const ProgressCallback &progress = {} );

    bool openStore( const std::string &name,
                    const std::string &password,
                    StoreInfo &info,
                    std::string &error ) const;

    bool readFile( const std::string &name,
                   const FileEntry &entry,
                   const std::string &password,
                   std::string &content,
                   std::string &error ) const;

    bool readFile( const std::string &name,
                   const FileEntry &entry,
                   const std::string &password,
                   std::uint64_t offset,
                   std::uint64_t length,
                   std::string &content,
                   std::string &error ) const;

    std::shared_ptr<FileStream> openStream( const std::string &name,
                                            const FileEntry &entry,
                                            const std::string &password,
                                            std::uint64_t offset,
                                            std::uint64_t length,
                                            std::string &error ) const;

    bool addPath( const std::string &name,
                  const std::string &password,
                  const std::string &relativeDir,
                  const std::filesystem::path &sourcePath,
                  StoreInfo &info,
                  std::string &error,
                  const ProgressCallback &progress = {} );

    bool removePath( const std::string &name,
                     const std::string &password,
                     const std::string &relativePath,
                     StoreInfo &info,
                     std::string &error,
                     const ProgressCallback &progress = {} );

    bool exportPath( const std::string &name,
                     const std::string &password,
                     const std::string &relativePath,
                     const std::filesystem::path &destRoot,
                     std::string &error,
                     const ProgressCallback &progress = {} ) const;

    static bool isValidName( const std::string &name );

private:
    bool writeConfig( const std::string &name,
                      const std::string &password,
                      const StoreInfo &info,
                      std::string &error ) const;

    static std::string serializeConfig( const StoreInfo &info );
    static bool parseConfig( const std::string &text, StoreInfo &info, std::string &error );

private:
    static constexpr const char *CONFIG_NAME = "store.cfg";
    static constexpr const char *DATA_SUFFIX = ".bin";

    std::filesystem::path m_dataDir;
};

} // namespace cryptostore

#endif