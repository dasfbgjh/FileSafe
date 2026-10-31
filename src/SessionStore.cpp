#include "SessionStore.h"
#include "Utils.h"

std::string SessionStore::create( const std::string &dir,
                                  const std::string &password,
                                  const cryptostore::StoreInfo &info ) {
    std::string token = utils::randomHex( 16 );
    if ( token.empty() ) {
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

    for ( auto &[token, session] : m_sessions ) {
        if ( session.dir != dir ) {
            continue;
        }
        session.files.clear();
        for ( const auto &entry : info.files ) {
            session.files[entry.id] = entry;
        }
        session.expires = std::chrono::steady_clock::now() + TTL;
    }
}

void SessionStore::purgeExpired() {
    auto now = std::chrono::steady_clock::now();
    for ( auto it = m_sessions.begin(); it != m_sessions.end(); ) {
        if ( it->second.expires <= now ) {
            it = m_sessions.erase( it );
        } else {
            ++it;
        }
    }
}