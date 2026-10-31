#ifndef APP_APP_H
#define APP_APP_H

#include "EncryptQueue.h"
#include "SessionStore.h"
#include "CryptoStore.h"
#include "http/HttpServer.h"

#include <filesystem>
#include <string>

class App {
public:
    explicit App( const std::filesystem::path &baseDir );

    ~App();

    App( const App & ) = delete;
    App &operator=( const App & ) = delete;
    App( App && ) = delete;
    App &operator=( App && ) = delete;

    bool run( unsigned short port );

private:
    using HttpServer = http::HttpServer;
    using Request = http::HttpServer::Request;
    using Response = http::HttpServer::Response;

    void fail( Response &resp, int code, const std::string &reason, const std::string &message );
    void registerRoutes();

    void handleStores( const http::HttpServer::Request &req, http::HttpServer::Response &resp );
    void handleEncrypt( const http::HttpServer::Request &req, http::HttpServer::Response &resp );
    void handleTasks( const http::HttpServer::Request &req, http::HttpServer::Response &resp );
    void handleOpen( const http::HttpServer::Request &req, http::HttpServer::Response &resp );
    void handleAdd( const http::HttpServer::Request &req, http::HttpServer::Response &resp );
    void handleDelete( const http::HttpServer::Request &req, http::HttpServer::Response &resp );
    void handleFiles( const http::HttpServer::Request &req, http::HttpServer::Response &resp );
    void handleFile( const http::HttpServer::Request &req, http::HttpServer::Response &resp );
    void handleStatic( const http::HttpServer::Request &req, http::HttpServer::Response &resp );

    bool serveResource( http::HttpServer::Response &resp, const std::string &resName );
    void redirectPage( http::HttpServer::Response &resp, const std::string &path );

    static constexpr const char *RESOURCE_PREFIX = "/html";

    std::filesystem::path m_baseDir;
    cryptostore::Store m_store;
    SessionStore m_sessions;
    EncryptQueue m_queue;
    http::HttpServer m_server;
};

#endif // APP_APP_H