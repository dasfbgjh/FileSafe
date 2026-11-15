#include "SessionStore.h"
#include "Logger.hpp"
#include "Utils.h"

std::string SessionStore::create( const std::string &dir,
                                  const std::string &password,
                                  const cryptostore::StoreInfo &info ) {
    std::string token = utils::randomHex( 16 );
    if ( token.empty() ) {
        LOG_ERROR << "创建会话失败: 生成令牌失败";
        return {};
    }

    Session session;
    session.dir = dir;
    session.password = password;
    session.expires = std::chrono::steady_clock::now() + TTL;
    for ( const auto &entry : info.files ) {
        session.files[entry.id] = entry;
    }

    std::lock_guard<std::mutex> lock( m_mutex );
    purgeExpired();
    m_sessions[token] = std::move( session );
    LOG_DEBUG << "已创建会话, 目录: " + dir + ", 当前会话数: " + std::to_string( m_sessions.size() );
    return token;
}

bool SessionStore::get( const std::string &token, Session &out ) {
    std::lock_guard<std::mutex> lock( m_mutex );
    purgeExpired();

    auto it = m_sessions.find( token );
    if ( it == m_sessions.end() ) {
        return false;
    }
    out = it->second;
    return true;
}

bool SessionStore::refresh( const std::string &token, const cryptostore::StoreInfo &info ) {
    std::lock_guard<std::mutex> lock( m_mutex );
    purgeExpired();

    auto it = m_sessions.find( token );
    if ( it == m_sessions.end() ) {
        return false;
    }

    it->second.files.clear();
    for ( const auto &entry : info.files ) {
        it->second.files[entry.id] = entry;
    }
    it->second.expires = std::chrono::steady_clock::now() + TTL;
    return true;
}

void SessionStore::refreshDir( const std::string &dir, const cryptostore::StoreInfo &info ) {
    std::lock_guard<std::mutex> lock( m_mutex );
    purgeExpired();

    std::size_t refreshed = 0;
    for ( auto &[token, session] : m_sessions ) {
        if ( session.dir != dir ) {
            continue;
        }
        session.files.clear();
        for ( const auto &entry : info.files ) {
            session.files[entry.id] = entry;
        }
        session.expires = std::chrono::steady_clock::now() + TTL;
        ++refreshed;
    }
    LOG_DEBUG << "已刷新目录会话, 目录: " + dir + ", 会话数: " + std::to_string( refreshed ) +
                     ", 文件数: " + std::to_string( info.files.size() );
}

void SessionStore::purgeExpired() {
    auto now = std::chrono::steady_clock::now();
    std::size_t removed = 0;
    for ( auto it = m_sessions.begin(); it != m_sessions.end(); ) {
        if ( it->second.expires <= now ) {
            it = m_sessions.erase( it );
            ++removed;
        } else {
            ++it;
        }
    }
    if ( removed > 0 ) {
        LOG_DEBUG << "清理过期会话: " + std::to_string( removed ) + " 个, 剩余: " +
                         std::to_string( m_sessions.size() ) + " 个";
    }
}