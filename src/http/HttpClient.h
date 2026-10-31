#pragma once
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <memory>
#include <chrono>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>

#ifdef HTTP_ENABLE_SSL
#include <boost/beast/ssl.hpp>
#endif

namespace http {

namespace beast = boost::beast;
namespace asio = boost::asio;
namespace json = boost::json;

using tcp = asio::ip::tcp;
using error_code = boost::system::error_code;

class HttpClient {
public:
    struct Response {
        int statusCode = 0;
        std::string reason;
        std::map<std::string, std::string> headers;
        std::string body;
    };

    using ResponseCallback = std::function<void( Response &&resp )>;
    using ErrorCallback = std::function<void( const error_code &ec )>;

    struct RequestOptions {
        std::map<std::string, std::string> headers;
        std::string body;
        std::chrono::milliseconds timeout{ 30000 };
        bool followRedirects = false;
        int maxRedirects = 5;
    };

    HttpClient();
    explicit HttpClient( asio::io_context &ioc );
    ~HttpClient();

    HttpClient( const HttpClient & ) = delete;
    HttpClient &operator=( const HttpClient & ) = delete;
    HttpClient( HttpClient && ) = delete;
    HttpClient &operator=( HttpClient && ) = delete;

    void get( const std::string &url, ResponseCallback onSuccess, ErrorCallback onError = nullptr );
    void get( const std::string &url, ResponseCallback onSuccess, ErrorCallback onError, RequestOptions options );

    void post( const std::string &url, const std::string &body, ResponseCallback onSuccess, ErrorCallback onError = nullptr );
    void post( const std::string &url, const std::string &body, ResponseCallback onSuccess, ErrorCallback onError, RequestOptions options );

    void put( const std::string &url, const std::string &body, ResponseCallback onSuccess, ErrorCallback onError = nullptr );
    void put( const std::string &url, const std::string &body, ResponseCallback onSuccess, ErrorCallback onError, RequestOptions options );

    void del( const std::string &url, ResponseCallback onSuccess, ErrorCallback onError = nullptr );
    void del( const std::string &url, ResponseCallback onSuccess, ErrorCallback onError, RequestOptions options );

    void request( beast::http::verb method, const std::string &url, ResponseCallback onSuccess, ErrorCallback onError = nullptr );
    void request( beast::http::verb method, const std::string &url, ResponseCallback onSuccess, ErrorCallback onError, RequestOptions options );

    Response getSync( const std::string &url );
    Response getSync( const std::string &url, RequestOptions options );
    Response postSync( const std::string &url, const std::string &body );
    Response postSync( const std::string &url, const std::string &body, RequestOptions options );
    Response putSync( const std::string &url, const std::string &body );
    Response putSync( const std::string &url, const std::string &body, RequestOptions options );
    Response delSync( const std::string &url );
    Response delSync( const std::string &url, RequestOptions options );
    Response requestSync( beast::http::verb method, const std::string &url );
    Response requestSync( beast::http::verb method, const std::string &url, RequestOptions options );

    void setUserAgent( const std::string &agent );

#ifdef HTTP_ENABLE_SSL
    void setVerifySsl( bool verify );
#endif

private:
    struct ParsedUrl {
        std::string scheme;
        std::string host;
        std::string port;
        std::string target;
        bool ssl = false;
    };

    static ParsedUrl parseUrl( const std::string &url );

    class Session;
    std::unique_ptr<asio::io_context> m_ownedIoc;
    asio::io_context &m_ioc;
    std::string m_userAgent = "HttpClient/1.0";
#ifdef HTTP_ENABLE_SSL
    bool m_verifySsl = true;
#endif
};

} // namespace http

#endif // HTTP_CLIENT_H
