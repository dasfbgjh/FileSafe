#ifndef APP_SESSION_STORE_H
#define APP_SESSION_STORE_H

#include "CryptoStore.h"

#include <chrono>
#include <map>
#include <mutex>
#include <string>

struct Session {
    std::string dir;
    std::string password;
    std::map<int, cryptostore::FileEntry> files;
    std::chrono::steady_clock::time_point expires;
};

class SessionStore {
public:
    std::string create( const std::string &dir,
                        const std::string &password,
                        const cryptostore::StoreInfo &info );

    bool get( const std::string &token, Session &out );

    bool refresh( const std::string &token, const cryptostore::StoreInfo &info );

    void refreshDir( const std::string &dir, const cryptostore::StoreInfo &info );

private:
    void purgeExpired();

    static constexpr std::chrono::minutes TTL{ 30 };

    std::mutex m_mutex;
    std::map<std::string, Session> m_sessions;
};

#endif // APP_SESSION_STORE_H