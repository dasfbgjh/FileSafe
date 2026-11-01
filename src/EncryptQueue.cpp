#include "EncryptQueue.h"
#include "Utils.h"

#include <algorithm>
#include <utility>

namespace {

const char *kindName( TaskKind kind ) {
    switch ( kind ) {
    case TaskKind::Add:
        return "add";
    case TaskKind::Remove:
        return "remove";
    case TaskKind::Export:
        return "export";
    default:
        return "encrypt";
    }
}

} // namespace

EncryptQueue::EncryptQueue( cryptostore::Store &store )
    : m_store( store ) {
}

EncryptQueue::~EncryptQueue() {
    stop();
}

void EncryptQueue::setCompletionHandler( CompletionHandler handler ) {
    m_completion = std::move( handler );
}

void EncryptQueue::start() {
    std::lock_guard<std::mutex> lock( m_mutex );
    if ( m_running ) {
        return;
    }
    m_running = true;
    m_thread = std::thread( [this] { workerLoop(); } );
}

void EncryptQueue::stop() {
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( !m_running ) {
            return;
        }
        m_running = false;
    }
    m_cv.notify_all();
    if ( m_thread.joinable() ) {
        m_thread.join();
    }
}

std::string EncryptQueue::submitEncrypt( const std::string &dir, const std::string &password ) {
    Job job;
    job.kind = TaskKind::Encrypt;
    job.dir = dir;
    job.source = utils::toPath( dir );
    job.password = password;
    return submitJob( std::move( job ), {} );
}

std::string EncryptQueue::submitAdd( const std::string &store,
                                     const std::string &password,
                                     const std::string &targetDir,
                                     const std::filesystem::path &sourcePath,
                                     const std::string &title ) {
    Job job;
    job.kind = TaskKind::Add;
    job.store = store;
    job.password = password;
    job.targetDir = targetDir;
    job.source = sourcePath;
    return submitJob( std::move( job ), title );
}

std::string EncryptQueue::submitRemove( const std::string &store,
                                        const std::string &password,
                                        const std::string &path,
                                        const std::string &title ) {
    Job job;
    job.kind = TaskKind::Remove;
    job.store = store;
    job.password = password;
    job.path = path;
    return submitJob( std::move( job ), title );
}

std::string EncryptQueue::submitExport( const std::string &store,
                                        const std::string &password,
                                        const std::string &path,
                                        const std::filesystem::path &destRoot,
                                        const std::string &title ) {
    Job job;
    job.kind = TaskKind::Export;
    job.store = store;
    job.password = password;
    job.path = path;
    job.dest = destRoot;
    return submitJob( std::move( job ), title );
}

std::string EncryptQueue::submitJob( Job job, const std::string &title ) {
    std::string id = utils::randomHex( 8 );
    if ( id.empty() ) {
        return {};
    }

    job.id = id;

    {
        std::lock_guard<std::mutex> lock( m_mutex );
        EncryptTask task;
        task.id = id;
        task.kind = kindName( job.kind );
        task.title = title;
        task.dir = ( job.kind == TaskKind::Encrypt ) ? job.dir : job.store;
        m_tasks[id] = std::move( task );
        m_order.push_back( id );
        m_pending.push_back( std::move( job ) );
        pruneLocked();
    }
    m_cv.notify_one();
    return id;
}

std::vector<EncryptTask> EncryptQueue::list( std::uint64_t &finished ) const {
    std::lock_guard<std::mutex> lock( m_mutex );
    finished = m_finished;

    std::vector<EncryptTask> out;
    out.reserve( m_order.size() );
    for ( auto it = m_order.rbegin(); it != m_order.rend(); ++it ) {
        auto taskIt = m_tasks.find( *it );
        if ( taskIt != m_tasks.end() ) {
            out.push_back( taskIt->second );
        }
    }
    return out;
}

void EncryptQueue::workerLoop() {
    while ( true ) {
        Job job;
        {
            std::unique_lock<std::mutex> lock( m_mutex );
            m_cv.wait( lock, [this] { return !m_running || !m_pending.empty(); } );
            if ( !m_running ) {
                return;
            }
            job = std::move( m_pending.front() );
            m_pending.pop_front();

            auto it = m_tasks.find( job.id );
            if ( it == m_tasks.end() ) {
                continue;
            }
            it->second.state = "running";
            it->second.startedAt = std::chrono::steady_clock::now();
        }

        runJob( job );
    }
}

void EncryptQueue::runJob( const Job &job ) {
    cryptostore::StoreInfo info;
    std::string error;

    auto progress = [this, id = job.id]( const cryptostore::Progress &state ) {
        std::lock_guard<std::mutex> lock( m_mutex );
        auto it = m_tasks.find( id );
        if ( it == m_tasks.end() ) {
            return;
        }
        it->second.filesDone = state.filesDone;
        it->second.filesTotal = state.filesTotal;
        it->second.bytesDone = state.bytesDone;
        it->second.bytesTotal = state.bytesTotal;
        it->second.current = state.current;
    };

    bool ok = false;
    switch ( job.kind ) {
    case TaskKind::Add:
        ok = m_store.addPath( job.store, job.password, job.targetDir, job.source, info, error, progress );
        break;
    case TaskKind::Remove:
        ok = m_store.removePath( job.store, job.password, job.path, info, error, progress );
        break;
    case TaskKind::Export:
        ok = m_store.exportPath( job.store, job.password, job.path, job.dest, error, progress );
        break;
    default:
        ok = m_store.encryptDirectory( job.source, job.password, info, error, progress );
        break;
    }

    if ( ok && ( job.kind == TaskKind::Add || job.kind == TaskKind::Remove ) && m_completion ) {
        m_completion( job.store, info );
    }

    std::lock_guard<std::mutex> lock( m_mutex );

    if ( ok ) {
        removeLocked( job.id );
        ++m_finished;
        return;
    }

    auto it = m_tasks.find( job.id );
    if ( it == m_tasks.end() ) {
        return;
    }
    it->second.state = "failed";
    it->second.error = error.empty() ? std::string( "任务执行失败" ) : error;
    it->second.current.clear();
    it->second.finishedAt = std::chrono::steady_clock::now();
    pruneLocked();
}

void EncryptQueue::removeLocked( const std::string &id ) {
    m_tasks.erase( id );
    m_order.erase( std::remove( m_order.begin(), m_order.end(), id ), m_order.end() );
}

void EncryptQueue::pruneLocked() {
    std::size_t failedCount = 0;
    for ( const auto &[id, task] : m_tasks ) {
        if ( task.state == "failed" ) {
            ++failedCount;
        }
    }

    for ( auto it = m_order.begin(); it != m_order.end() && failedCount > MAX_FAILED; ) {
        auto taskIt = m_tasks.find( *it );
        if ( taskIt == m_tasks.end() ) {
            it = m_order.erase( it );
            continue;
        }
        if ( taskIt->second.state == "failed" ) {
            m_tasks.erase( taskIt );
            it = m_order.erase( it );
            --failedCount;
        } else {
            ++it;
        }
    }
}