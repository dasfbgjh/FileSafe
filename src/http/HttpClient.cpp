#include "HttpClient.h"

#ifdef HTTP_ENABLE_SSL
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/asio/ssl.hpp>
#endif

namespace http {

class HttpClient::Session : public std::enable_shared_from_this<Session> {
public:
    Session( asio::io_context &ioc,
#ifdef HTTP_ENABLE_SSL
             bool verifySsl,
#endif
             const std::string &userAgent,
             ResponseCallback onSuccess, ErrorCallback onError )
        : m_resolver( asio::make_strand( ioc ) )
        , m_stream( ioc )
#ifdef HTTP_ENABLE_SSL
        , m_sslCtx( asio::ssl::context::tlsv12_client )
        , m_sslStream( asio::make_strand( ioc ), m_sslCtx )
        , m_verifySsl( verifySsl )
#endif
        , m_onSuccess( std::move( onSuccess ) )
        , m_onError( std::move( onError ) )
        , m_userAgent( userAgent ) {
#ifdef HTTP_ENABLE_SSL
        if ( !m_verifySsl ) {
            m_sslStream.set_verify_mode( asio::ssl::verify_none );
        }
#endif
    }

    void run( beast::http::verb method, const std::string &url, RequestOptions options ) {
        ParsedUrl parsed = parseUrl( url );
        if ( parsed.host.empty() ) {
            if ( m_onError )
                m_onError( asio::error::invalid_argument );
            return;
        }

#ifndef HTTP_ENABLE_SSL
        if ( parsed.ssl ) {
            if ( m_onError )
                m_onError( asio::error::operation_not_supported );
            return;
        }
#endif

        m_parsed = parsed;
        m_options = std::move( options );
        m_useSsl = parsed.ssl;

        m_req.method( method );
        m_req.target( parsed.target );
        m_req.version( 11 );
        m_req.set( beast::http::field::host, parsed.host );
        m_req.set( beast::http::field::user_agent, m_userAgent );
        m_req.set( beast::http::field::connection, "close" );

        if ( !m_options.body.empty() ) {
            m_req.body() = m_options.body;
            m_req.prepare_payload();
            if ( m_req.find( beast::http::field::content_type ) == m_req.end() ) {
                m_req.set( beast::http::field::content_type, "application/octet-stream" );
            }
        }

        for ( auto &[key, value] : m_options.headers ) {
            m_req.set( key, value );
        }

        m_req.prepare_payload();

        m_resolver.async_resolve(
            parsed.host, parsed.port,
            beast::bind_front_handler( &Session::onResolve, shared_from_this() ) );
    }

private:
    void onResolve( const error_code &ec, tcp::resolver::results_type results ) {
        if ( ec ) {
            if ( m_onError )
                m_onError( ec );
            return;
        }

        if ( m_useSsl ) {
#ifdef HTTP_ENABLE_SSL
            if ( !SSL_set_tlsext_host_name( m_sslStream.native_handle(), m_parsed.host.c_str() ) ) {
                if ( m_onError )
                    m_onError( asio::error::invalid_argument );
                return;
            }

            beast::get_lowest_layer( m_sslStream ).async_connect(
                results,
                beast::bind_front_handler( &Session::onConnect, shared_from_this() ) );
#else
            if ( m_onError )
                m_onError( asio::error::operation_not_supported );
#endif
        } else {
            m_stream.async_connect(
                results,
                beast::bind_front_handler( &Session::onConnect, shared_from_this() ) );
        }
    }

    void onConnect( const error_code &ec, tcp::resolver::results_type::endpoint_type ) {
        if ( ec ) {
            if ( m_onError )
                m_onError( ec );
            return;
        }

        if ( m_useSsl ) {
#ifdef HTTP_ENABLE_SSL
            m_sslStream.async_handshake(
                asio::ssl::stream_base::client,
                beast::bind_front_handler( &Session::onHandshake, shared_from_this() ) );
#else
            if ( m_onError )
                m_onError( asio::error::operation_not_supported );
#endif
        } else {
            doWrite();
        }
    }

#ifdef HTTP_ENABLE_SSL
    void onHandshake( const error_code &ec ) {
        if ( ec ) {
            if ( m_onError )
                m_onError( ec );
            return;
        }
        doWrite();
    }
#endif

    void doWrite() {
        if ( m_useSsl ) {
#ifdef HTTP_ENABLE_SSL
            beast::http::async_write(
                m_sslStream, m_req,
                beast::bind_front_handler( &Session::onWrite, shared_from_this() ) );
#endif
        } else {
            beast::http::async_write(
                m_stream, m_req,
                beast::bind_front_handler( &Session::onWrite, shared_from_this() ) );
        }
    }

    void onWrite( const error_code &ec, std::size_t ) {
        if ( ec ) {
            if ( m_onError )
                m_onError( ec );
            return;
        }

        if ( m_useSsl ) {
#ifdef HTTP_ENABLE_SSL
            beast::http::async_read(
                m_sslStream, m_buffer, m_res,
                beast::bind_front_handler( &Session::onRead, shared_from_this() ) );
#endif
        } else {
            beast::http::async_read(
                m_stream, m_buffer, m_res,
                beast::bind_front_handler( &Session::onRead, shared_from_this() ) );
        }
    }

    void onRead( const error_code &ec, std::size_t ) {
        if ( ec ) {
            if ( m_onError )
                m_onError( ec );
            return;
        }

        Response resp;
        resp.statusCode = m_res.result_int();
        resp.reason = std::string( m_res.reason() );
        resp.body = m_res.body();

        for ( auto it = m_res.begin(); it != m_res.end(); ++it ) {
            resp.headers.emplace( std::string( it->name_string() ), std::string( it->value() ) );
        }

        doClose();

        if ( m_onSuccess )
            m_onSuccess( std::move( resp ) );
    }

    void doClose() {
        if ( m_useSsl ) {
#ifdef HTTP_ENABLE_SSL
            m_sslStream.async_shutdown(
                [this]( const error_code & ) {
                    beast::get_lowest_layer( m_sslStream ).close();
                } );
#endif
        } else {
            error_code ec;
            m_stream.socket().shutdown( tcp::socket::shutdown_both, ec );
        }
    }

    tcp::resolver m_resolver;
    beast::tcp_stream m_stream;
#ifdef HTTP_ENABLE_SSL
    asio::ssl::context m_sslCtx;
    beast::ssl_stream<beast::tcp_stream> m_sslStream;
#endif
    beast::flat_buffer m_buffer;
    beast::http::request<beast::http::string_body> m_req;
    beast::http::response<beast::http::string_body> m_res;
    ResponseCallback m_onSuccess;
    ErrorCallback m_onError;
    ParsedUrl m_parsed;
    RequestOptions m_options;
    std::string m_userAgent;
#ifdef HTTP_ENABLE_SSL
    bool m_verifySsl;
#endif
    bool m_useSsl = false;
};

HttpClient::HttpClient()
    : m_ownedIoc( std::make_unique<asio::io_context>() )
    , m_ioc( *m_ownedIoc ) {
}

HttpClient::HttpClient( asio::io_context &ioc )
    : m_ioc( ioc ) {
}

HttpClient::~HttpClient() = default;

void HttpClient::get( const std::string &url, ResponseCallback onSuccess, ErrorCallback onError ) {
    request( beast::http::verb::get, url, std::move( onSuccess ), std::move( onError ), RequestOptions() );
}

void HttpClient::get( const std::string &url, ResponseCallback onSuccess, ErrorCallback onError, RequestOptions options ) {
    request( beast::http::verb::get, url, std::move( onSuccess ), std::move( onError ), std::move( options ) );
}

void HttpClient::post( const std::string &url, const std::string &body, ResponseCallback onSuccess, ErrorCallback onError ) {
    RequestOptions opts;
    opts.body = body;
    request( beast::http::verb::post, url, std::move( onSuccess ), std::move( onError ), std::move( opts ) );
}

void HttpClient::post( const std::string &url, const std::string &body, ResponseCallback onSuccess, ErrorCallback onError, RequestOptions options ) {
    options.body = body;
    request( beast::http::verb::post, url, std::move( onSuccess ), std::move( onError ), std::move( options ) );
}

void HttpClient::put( const std::string &url, const std::string &body, ResponseCallback onSuccess, ErrorCallback onError ) {
    RequestOptions opts;
    opts.body = body;
    request( beast::http::verb::put, url, std::move( onSuccess ), std::move( onError ), std::move( opts ) );
}

void HttpClient::put( const std::string &url, const std::string &body, ResponseCallback onSuccess, ErrorCallback onError, RequestOptions options ) {
    options.body = body;
    request( beast::http::verb::put, url, std::move( onSuccess ), std::move( onError ), std::move( options ) );
}

void HttpClient::del( const std::string &url, ResponseCallback onSuccess, ErrorCallback onError ) {
    request( beast::http::verb::delete_, url, std::move( onSuccess ), std::move( onError ), RequestOptions() );
}

void HttpClient::del( const std::string &url, ResponseCallback onSuccess, ErrorCallback onError, RequestOptions options ) {
    request( beast::http::verb::delete_, url, std::move( onSuccess ), std::move( onError ), std::move( options ) );
}

void HttpClient::request( beast::http::verb method, const std::string &url, ResponseCallback onSuccess, ErrorCallback onError ) {
    request( method, url, std::move( onSuccess ), std::move( onError ), RequestOptions() );
}

void HttpClient::request( beast::http::verb method, const std::string &url, ResponseCallback onSuccess, ErrorCallback onError, RequestOptions options ) {
    auto session = std::make_shared<Session>( m_ioc,
#ifdef HTTP_ENABLE_SSL
        m_verifySsl,
#endif
        m_userAgent, std::move( onSuccess ), std::move( onError ) );
    session->run( method, url, std::move( options ) );
}

HttpClient::Response HttpClient::getSync( const std::string &url ) {
    return requestSync( beast::http::verb::get, url, RequestOptions() );
}

HttpClient::Response HttpClient::getSync( const std::string &url, RequestOptions options ) {
    return requestSync( beast::http::verb::get, url, std::move( options ) );
}

HttpClient::Response HttpClient::postSync( const std::string &url, const std::string &body ) {
    RequestOptions opts;
    opts.body = body;
    return requestSync( beast::http::verb::post, url, std::move( opts ) );
}

HttpClient::Response HttpClient::postSync( const std::string &url, const std::string &body, RequestOptions options ) {
    options.body = body;
    return requestSync( beast::http::verb::post, url, std::move( options ) );
}

HttpClient::Response HttpClient::putSync( const std::string &url, const std::string &body ) {
    RequestOptions opts;
    opts.body = body;
    return requestSync( beast::http::verb::put, url, std::move( opts ) );
}

HttpClient::Response HttpClient::putSync( const std::string &url, const std::string &body, RequestOptions options ) {
    options.body = body;
    return requestSync( beast::http::verb::put, url, std::move( options ) );
}

HttpClient::Response HttpClient::delSync( const std::string &url ) {
    return requestSync( beast::http::verb::delete_, url, RequestOptions() );
}

HttpClient::Response HttpClient::delSync( const std::string &url, RequestOptions options ) {
    return requestSync( beast::http::verb::delete_, url, std::move( options ) );
}

HttpClient::Response HttpClient::requestSync( beast::http::verb method, const std::string &url ) {
    return requestSync( method, url, RequestOptions() );
}

HttpClient::Response HttpClient::requestSync( beast::http::verb method, const std::string &url, RequestOptions options ) {
    Response resp;
    ParsedUrl parsed = parseUrl( url );
    if ( parsed.host.empty() ) {
        return resp;
    }

#ifndef HTTP_ENABLE_SSL
    if ( parsed.ssl ) {
        resp.statusCode = 501;
        resp.reason = "SSL Not Enabled";
        return resp;
    }
#endif

    try {
        asio::io_context ioc;
        tcp::resolver resolver( ioc );

        auto results = resolver.resolve( parsed.host, parsed.port );

        if ( parsed.ssl ) {
#ifdef HTTP_ENABLE_SSL
            asio::ssl::context ctx( asio::ssl::context::tlsv12_client );
            if ( !m_verifySsl ) {
                ctx.set_verify_mode( asio::ssl::verify_none );
            }

            beast::ssl_stream<beast::tcp_stream> stream( ioc, ctx );
            if ( !SSL_set_tlsext_host_name( stream.native_handle(), parsed.host.c_str() ) ) {
                return resp;
            }

            beast::get_lowest_layer( stream ).connect( results );
            stream.handshake( asio::ssl::stream_base::client );

            beast::http::request<beast::http::string_body> req;
            req.method( method );
            req.target( parsed.target );
            req.version( 11 );
            req.set( beast::http::field::host, parsed.host );
            req.set( beast::http::field::user_agent, m_userAgent );
            req.set( beast::http::field::connection, "close" );

            if ( !options.body.empty() ) {
                req.body() = options.body;
                if ( req.find( beast::http::field::content_type ) == req.end() ) {
                    req.set( beast::http::field::content_type, "application/octet-stream" );
                }
            }

            for ( auto &[key, value] : options.headers ) {
                req.set( key, value );
            }
            req.prepare_payload();

            beast::http::write( stream, req );

            beast::flat_buffer buffer;
            beast::http::response<beast::http::string_body> res;
            beast::http::read( stream, buffer, res );

            resp.statusCode = res.result_int();
            resp.reason = std::string( res.reason() );
            resp.body = res.body();
            for ( auto it = res.begin(); it != res.end(); ++it ) {
                resp.headers.emplace( std::string( it->name_string() ), std::string( it->value() ) );
            }

            error_code ec;
            stream.shutdown( ec );
#endif
        } else {
            beast::tcp_stream stream( ioc );
            stream.connect( results );

            beast::http::request<beast::http::string_body> req;
            req.method( method );
            req.target( parsed.target );
            req.version( 11 );
            req.set( beast::http::field::host, parsed.host );
            req.set( beast::http::field::user_agent, m_userAgent );
            req.set( beast::http::field::connection, "close" );

            if ( !options.body.empty() ) {
                req.body() = options.body;
                if ( req.find( beast::http::field::content_type ) == req.end() ) {
                    req.set( beast::http::field::content_type, "application/octet-stream" );
                }
            }

            for ( auto &[key, value] : options.headers ) {
                req.set( key, value );
            }
            req.prepare_payload();

            beast::http::write( stream, req );

            beast::flat_buffer buffer;
            beast::http::response<beast::http::string_body> res;
            beast::http::read( stream, buffer, res );

            resp.statusCode = res.result_int();
            resp.reason = std::string( res.reason() );
            resp.body = res.body();
            for ( auto it = res.begin(); it != res.end(); ++it ) {
                resp.headers.emplace( std::string( it->name_string() ), std::string( it->value() ) );
            }

            error_code ec;
            stream.socket().shutdown( tcp::socket::shutdown_both, ec );
        }
    } catch ( const std::exception & ) {
    }

    return resp;
}

void HttpClient::setUserAgent( const std::string &agent ) {
    m_userAgent = agent;
}

#ifdef HTTP_ENABLE_SSL
void HttpClient::setVerifySsl( bool verify ) {
    m_verifySsl = verify;
}
#endif

HttpClient::ParsedUrl HttpClient::parseUrl( const std::string &url ) {
    ParsedUrl result;

    std::string_view sv( url );

    auto schemeEnd = sv.find( "://" );
    if ( schemeEnd == std::string_view::npos ) {
        result.scheme = "http";
        result.ssl = false;
        sv = sv.substr( 0 );
    } else {
        result.scheme = std::string( sv.substr( 0, schemeEnd ) );
        result.ssl = ( result.scheme == "https" );
        sv = sv.substr( schemeEnd + 3 );
    }

    auto pathStart = sv.find( '/' );
    std::string_view authority = ( pathStart != std::string_view::npos ) ? sv.substr( 0, pathStart ) : sv;
    result.target = ( pathStart != std::string_view::npos ) ? std::string( sv.substr( pathStart ) ) : "/";

    auto portPos = authority.find( ':' );
    if ( portPos != std::string_view::npos ) {
        result.host = std::string( authority.substr( 0, portPos ) );
        result.port = std::string( authority.substr( portPos + 1 ) );
    } else {
        result.host = std::string( authority );
        result.port = result.ssl ? "443" : "80";
    }

    return result;
}

} // namespace http