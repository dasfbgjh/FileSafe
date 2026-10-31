#ifndef APP_ENCRYPT_QUEUE_H
#define APP_ENCRYPT_QUEUE_H

#include "CryptoStore.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class TaskKind {
    Encrypt,
    Add,
    Remove
};

struct EncryptTask {
    std::string id;
    std::string kind = "encrypt";
    std::string title;
    std::string dir;
    std::string state = "queued";
    std::string error;
    std::string current;
    std::size_t filesDone = 0;
    std::size_t filesTotal = 0;
    std::uint64_t bytesDone = 0;
    std::uint64_t bytesTotal = 0;
    std::chrono::steady_clock::time_point createdAt = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point startedAt{};
    std::chrono::steady_clock::time_point finishedAt{};
};

class EncryptQueue {
public:
    using CompletionHandler = std::function<void( const std::string &store, const cryptostore::StoreInfo &info )>;

    explicit EncryptQueue( cryptostore::Store &store );

    ~EncryptQueue();

    EncryptQueue( const EncryptQueue & ) = delete;
    EncryptQueue &operator=( const EncryptQueue & ) = delete;
    EncryptQueue( EncryptQueue && ) = delete;
    EncryptQueue &operator=( EncryptQueue && ) = delete;

    void setCompletionHandler( CompletionHandler handler );

    void start();
    void stop();

    std::string submitEncrypt( const std::string &dir, const std::string &password );

    std::string submitAdd( const std::string &store,
                           const std::string &password,
                           const std::string &targetDir,
                           const std::filesystem::path &sourcePath,
                           const std::string &title );

    std::string submitRemove( const std::string &store,
                              const std::string &password,
                              const std::string &path,
                              const std::string &title );

    std::vector<EncryptTask> list( std::uint64_t &finished ) const;

private:
    struct Job {
        std::string id;
        TaskKind kind = TaskKind::Encrypt;
        std::string dir;
        std::string store;
        std::string password;
        std::string targetDir;
        std::string path;
        std::filesystem::path source;
    };

    std::string submitJob( Job job, const std::string &title );
    void workerLoop();
    void runJob( const Job &job );
    void removeLocked( const std::string &id );
    void pruneLocked();

    static constexpr std::size_t MAX_FAILED = 20;

    cryptostore::Store &m_store;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::map<std::string, EncryptTask> m_tasks;
    std::vector<std::string> m_order;
    std::deque<Job> m_pending;
    std::thread m_thread;
    CompletionHandler m_completion;
    std::uint64_t m_finished = 0;
    bool m_running = false;
};

#endif // APP_ENCRYPT_QUEUE_H