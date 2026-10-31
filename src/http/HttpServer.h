#pragma once
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <memory>
#include <filesystem>
#include <regex>
#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace http {

namespace beast = boost::beast;
namespace asio = boost::asio;

using tcp = asio::ip::tcp;
using error_code = boost::system::error_code;

class HttpServer {
public:
    struct Request {
        beast::http::verb method;
        std::string target;
        int version = 11;
        std::map<std::string, std::string> headers;
        std::string body;
        std::map<std::string, std::string> params;
        std::smatch pathMatches;
    };

    struct Response {
        int statusCode = 200;
        std::string reason = "OK";
        std::map<std::string, std::string> headers;
        std::string body;

        void setJson( const std::string &json ) {
            body = json;
            headers["Content-Type"] = "application/json";
        }

        void setHtml( const std::string &html ) {
            body = html;
            headers["Content-Type"] = "text/html; charset=utf-8";
        }

        void setText( const std::string &text ) {
            body = text;
            headers["Content-Type"] = "text/plain; charset=utf-8";
        }

        void setStatus( int code, const std::string &reasonStr ) {
            statusCode = code;
            reason = reasonStr;
        }
    };

    using Handler = std::function<void( const Request &, Response & )>;
    using Middleware = std::function<bool( const Request &, Response & )>;

    HttpServer();
    explicit HttpServer( asio::io_context &ioc );
    ~HttpServer();

    HttpServer( const HttpServer & ) = delete;
    HttpServer &operator=( const HttpServer & ) = delete;
    HttpServer( HttpServer && ) = delete;
    HttpServer &operator=( HttpServer && ) = delete;

    void get( const std::string &path, Handler handler );
    void post( const std::string &path, Handler handler );
    void put( const std::string &path, Handler handler );
    void del( const std::string &path, Handler handler );
    void route( beast::http::verb method, const std::string &path, Handler handler );
    void any( const std::string &path, Handler handler );

    void use( Middleware middleware );

    void setStaticDir( const std::string &dir );

    bool listen( const std::string &host = "0.0.0.0", unsigned short port = 8080 );
    void stop();

    asio::io_context &getIoContext();

    static std::string contentType( const std::filesystem::path &path );

private:
    struct Route {
        beast::http::verb method;
        bool matchAny = false;
        std::regex pattern;
        std::string patternStr;
        Handler handler;
    };

    class Connection;

    void doAccept();
    void dispatch( const Request &req, Response &resp );
    std::string matchStaticFile( const std::string &target ) const;

    std::unique_ptr<asio::io_context> m_ownedIoc;
    asio::io_context &m_ioc;
    tcp::acceptor m_acceptor;
    std::vector<Route> m_routes;
    std::vector<Middleware> m_middlewares;
    std::string m_staticDir;
    bool m_running = false;
};

} // namespace http

#endif // HTTP_SERVER_H