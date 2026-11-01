#include "HttpServer.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <filesystem>

namespace http {

class HttpServer::Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection( asio::io_context &ioc, HttpServer &server )
        : m_stream( ioc ), m_server( server ) {
    }

    tcp::socket &socket() {
        return m_stream.socket();
    }

    void start() {
        asio::dispatch(
            m_stream.get_executor(),
            beast::bind_front_handler( &Connection::doRead, shared_from_this() ) );
    }

private:
    void doRead() {
        m_parser.emplace();
        m_parser->body_limit( 10 * 1024 * 1024 );

        m_buffer.clear();

        beast::http::async_read(
            m_stream, m_buffer, *m_parser,
            beast::bind_front_handler( &Connection::onRead, shared_from_this() ) );
    }

    void onRead( const error_code &ec, std::size_t bytesTransferred ) {
        if ( ec == beast::http::error::end_of_stream ) {
            doClose();
            return;
        }

        if ( ec ) {
            return;
        }

        auto &reqMsg = m_parser->get();
        Request req;
        req.method = reqMsg.method();
        req.target = std::string( reqMsg.target() );
        req.version = reqMsg.version();
        req.body = reqMsg.body();

        for ( auto it = reqMsg.begin(); it != reqMsg.end(); ++it ) {
            req.headers.emplace( std::string( it->name_string() ), std::string( it->value() ) );
        }

        auto queryPos = req.target.find( '?' );
        if ( queryPos != std::string::npos ) {
            std::string queryStr = req.target.substr( queryPos + 1 );
            req.target = req.target.substr( 0, queryPos );
            parseQueryParams( queryStr, req.params );
        }

        Response resp;

        m_server.dispatch( req, resp );

        if ( resp.isStreaming() ) {
            startStream( resp, req.version );
            return;
        }

        m_serializer.reset();
        m_response.emplace();

        beast::http::response<beast::http::string_body> &resMsg = *m_response;
        resMsg.version( req.version );
        resMsg.result( resp.statusCode );
        resMsg.reason( resp.reason );
        resMsg.body() = resp.body;

        for ( auto &[key, value] : resp.headers ) {
            resMsg.set( key, value );
        }

        resMsg.set( beast::http::field::server, "HttpServer/1.0" );
        resMsg.set( beast::http::field::content_length, std::to_string( resp.body.size() ) );
        if ( resp.headers.find( "Content-Type" ) == resp.headers.end() ) {
            resMsg.set( beast::http::field::content_type, "text/plain; charset=utf-8" );
        }
        resMsg.prepare_payload();

        m_serializer.emplace( resMsg );

        beast::http::async_write(
            m_stream, *m_serializer,
            beast::bind_front_handler( &Connection::onWrite, shared_from_this() ) );
    }

    void onWrite( const error_code &ec, std::size_t ) {
        if ( ec ) {
            return;
        }
        doRead();
    }

    void startStream( const Response &resp, int version ) {
        m_bodyPuller = resp.streamBody;

        m_streamResponse.emplace();
        beast::http::response<beast::http::empty_body> &resMsg = *m_streamResponse;
        resMsg.version( version );
        resMsg.result( resp.statusCode );
        resMsg.reason( resp.reason );

        for ( const auto &[key, value] : resp.headers ) {
            resMsg.set( key, value );
        }

        resMsg.set( beast::http::field::server, "HttpServer/1.0" );
        resMsg.set( beast::http::field::content_length, std::to_string( resp.contentLength ) );
        if ( resp.headers.find( "Content-Type" ) == resp.headers.end() ) {
            resMsg.set( beast::http::field::content_type, "text/plain; charset=utf-8" );
        }

        m_streamSerializer.emplace( resMsg );

        beast::http::async_write_header(
            m_stream, *m_streamSerializer,
            beast::bind_front_handler( &Connection::onStreamHeader, shared_from_this() ) );
    }

    void onStreamHeader( const error_code &ec, std::size_t ) {
        if ( ec ) {
            return;
        }
        writeNextChunk();
    }

    void writeNextChunk() {
        m_chunk.clear();
        if ( !m_bodyPuller || !m_bodyPuller( m_chunk, STREAM_CHUNK_SIZE ) || m_chunk.empty() ) {
            m_bodyPuller = nullptr;
            m_chunk.clear();
            m_streamSerializer.reset();
            m_streamResponse.reset();
            doRead();
            return;
        }

        asio::async_write(
            m_stream, asio::buffer( m_chunk ),
            beast::bind_front_handler( &Connection::onStreamChunk, shared_from_this() ) );
    }

    void onStreamChunk( const error_code &ec, std::size_t ) {
        if ( ec ) {
            return;
        }
        writeNextChunk();
    }

    void doClose() {
        error_code ec;
        m_stream.socket().shutdown( tcp::socket::shutdown_both, ec );
    }

    static void parseQueryParams( const std::string &query, std::map<std::string, std::string> &params ) {
        std::istringstream iss( query );
        std::string pair;
        while ( std::getline( iss, pair, '&' ) ) {
            auto eq = pair.find( '=' );
            if ( eq != std::string::npos ) {
                std::string key = urlDecode( pair.substr( 0, eq ) );
                std::string value = urlDecode( pair.substr( eq + 1 ) );
                params.emplace( std::move( key ), std::move( value ) );
            } else if ( !pair.empty() ) {
                params.emplace( urlDecode( pair ), "" );
            }
        }
    }

    static std::string urlDecode( const std::string &str ) {
        std::string result;
        result.reserve( str.size() );
        for ( size_t i = 0; i < str.size(); ++i ) {
            if ( str[i] == '%' && i + 2 < str.size() ) {
                char hex[3] = { str[i + 1], str[i + 2], '\0' };
                char *end = nullptr;
                long val = std::strtol( hex, &end, 16 );
                if ( end != hex ) {
                    result += static_cast<char>( val );
                    i += 2;
                    continue;
                }
            } else if ( str[i] == '+' ) {
                result += ' ';
                continue;
            }
            result += str[i];
        }
        return result;
    }

    beast::tcp_stream m_stream;
    beast::flat_buffer m_buffer;
    std::optional<beast::http::request_parser<beast::http::string_body>> m_parser;
    std::optional<beast::http::response<beast::http::string_body>> m_response;
    std::optional<beast::http::response_serializer<beast::http::string_body>> m_serializer;
    std::optional<beast::http::response<beast::http::empty_body>> m_streamResponse;
    std::optional<beast::http::response_serializer<beast::http::empty_body>> m_streamSerializer;
    BodyPuller m_bodyPuller;
    std::string m_chunk;
    HttpServer &m_server;

    static constexpr std::size_t STREAM_CHUNK_SIZE = 256u << 10;
};

HttpServer::HttpServer()
    : m_ownedIoc( std::make_unique<asio::io_context>() ), m_ioc( *m_ownedIoc ), m_acceptor( m_ioc ) {
}

HttpServer::HttpServer( asio::io_context &ioc )
    : m_ioc( ioc ), m_acceptor( m_ioc ) {
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::get( const std::string &path, Handler handler ) {
    route( beast::http::verb::get, path, std::move( handler ) );
}

void HttpServer::post( const std::string &path, Handler handler ) {
    route( beast::http::verb::post, path, std::move( handler ) );
}

void HttpServer::put( const std::string &path, Handler handler ) {
    route( beast::http::verb::put, path, std::move( handler ) );
}

void HttpServer::del( const std::string &path, Handler handler ) {
    route( beast::http::verb::delete_, path, std::move( handler ) );
}

void HttpServer::route( beast::http::verb method, const std::string &path, Handler handler ) {
    Route r;
    r.method = method;
    r.matchAny = false;
    r.patternStr = path;
    r.handler = std::move( handler );

    std::string regexStr;
    for ( size_t i = 0; i < path.size(); ++i ) {
        if ( path[i] == ':' ) {
            size_t j = i + 1;
            while ( j < path.size() && ( std::isalnum( static_cast<unsigned char>( path[j] ) ) || path[j] == '_' ) ) {
                ++j;
            }
            regexStr += "([^/]+)";
            i = j - 1;
        } else if ( path[i] == '*' ) {
            regexStr += "(.*)";
        } else {
            regexStr += path[i];
        }
    }
    r.pattern = std::regex( "^" + regexStr + "$" );

    m_routes.push_back( std::move( r ) );
}

void HttpServer::any( const std::string &path, Handler handler ) {
    Route r;
    r.matchAny = true;
    r.patternStr = path;
    r.handler = std::move( handler );

    std::string regexStr;
    for ( size_t i = 0; i < path.size(); ++i ) {
        if ( path[i] == ':' ) {
            size_t j = i + 1;
            while ( j < path.size() && ( std::isalnum( static_cast<unsigned char>( path[j] ) ) || path[j] == '_' ) ) {
                ++j;
            }
            regexStr += "([^/]+)";
            i = j - 1;
        } else if ( path[i] == '*' ) {
            regexStr += "(.*)";
        } else {
            regexStr += path[i];
        }
    }
    r.pattern = std::regex( "^" + regexStr + "$" );

    m_routes.push_back( std::move( r ) );
}

void HttpServer::use( Middleware middleware ) {
    m_middlewares.push_back( std::move( middleware ) );
}

void HttpServer::setStaticDir( const std::string &dir ) {
    m_staticDir = dir;
}

bool HttpServer::listen( const std::string &host, unsigned short port ) {
    try {
        auto addr = asio::ip::make_address( host );
        tcp::endpoint endpoint( addr, port );

        m_acceptor.open( endpoint.protocol() );
        m_acceptor.set_option( asio::socket_base::reuse_address( true ) );
        m_acceptor.bind( endpoint );
        m_acceptor.listen( asio::socket_base::max_listen_connections );

        m_running = true;
        doAccept();
        return true;
    } catch ( const std::exception & ) {
        return false;
    }
}

void HttpServer::stop() {
    if ( !m_running )
        return;
    m_running = false;

    error_code ec;
    m_acceptor.close( ec );
}

asio::io_context &HttpServer::getIoContext() {
    return m_ioc;
}

void HttpServer::doAccept() {
    m_acceptor.async_accept(
        asio::make_strand( m_ioc ),
        [this]( const error_code &ec, tcp::socket socket ) {
            if ( ec ) {
                if ( m_running )
                    doAccept();
                return;
            }

            auto conn = std::make_shared<Connection>( m_ioc, *this );
            conn->socket() = std::move( socket );
            conn->start();

            doAccept();
        } );
}

void HttpServer::dispatch( const Request &req, Response &resp ) {
    for ( auto &mw : m_middlewares ) {
        if ( !mw( req, resp ) )
            return;
    }

    for ( auto &route : m_routes ) {
        if ( !route.matchAny && route.method != req.method )
            continue;

        std::smatch matches;
        if ( std::regex_match( req.target, matches, route.pattern ) ) {
            Request matchedReq = req;
            matchedReq.pathMatches = matches;
            route.handler( matchedReq, resp );
            return;
        }
    }

    if ( !m_staticDir.empty() ) {
        std::string filePath = matchStaticFile( req.target );
        if ( !filePath.empty() ) {
            std::ifstream file( std::filesystem::u8path( filePath ), std::ios::binary );
            if ( file.is_open() ) {
                std::ostringstream ss;
                ss << file.rdbuf();
                resp.body = ss.str();
                resp.headers["Content-Type"] = contentType( std::filesystem::u8path( filePath ) );
                return;
            }
        }
    }

    resp.statusCode = 404;
    resp.reason = "Not Found";
    resp.setText( "页面不存在" );
}

std::string HttpServer::matchStaticFile( const std::string &target ) const {
    namespace fs = std::filesystem;

    std::string safePath = target;
    while ( !safePath.empty() && safePath.front() == '/' )
        safePath.erase( safePath.begin() );

    if ( safePath.empty() )
        safePath = "index.html";

    if ( safePath.find( ".." ) != std::string::npos )
        return {};

    fs::path fullPath = fs::path( m_staticDir ) / fs::u8path( safePath );
    if ( fs::exists( fullPath ) && fs::is_regular_file( fullPath ) ) {
        return fullPath.u8string();
    }

    return {};
}

std::string HttpServer::contentType( const std::filesystem::path &path ) {
    std::string ext = path.extension().u8string();
    std::transform( ext.begin(), ext.end(), ext.begin(), ::tolower );

    if ( ext == ".html" || ext == ".htm" )
        return "text/html; charset=utf-8";
    if ( ext == ".css" )
        return "text/css; charset=utf-8";
    if ( ext == ".js" || ext == ".mjs" )
        return "application/javascript; charset=utf-8";
    if ( ext == ".json" )
        return "application/json; charset=utf-8";
    if ( ext == ".txt" || ext == ".log" )
        return "text/plain; charset=utf-8";
    if ( ext == ".csv" )
        return "text/csv; charset=utf-8";
    if ( ext == ".xml" )
        return "application/xml; charset=utf-8";
    if ( ext == ".pdf" )
        return "application/pdf";
    if ( ext == ".zip" )
        return "application/zip";
    if ( ext == ".wasm" )
        return "application/wasm";

    if ( ext == ".svg" )
        return "image/svg+xml";
    if ( ext == ".png" )
        return "image/png";
    if ( ext == ".jpg" || ext == ".jpeg" )
        return "image/jpeg";
    if ( ext == ".gif" )
        return "image/gif";
    if ( ext == ".webp" )
        return "image/webp";
    if ( ext == ".ico" )
        return "image/x-icon";
    if ( ext == ".bmp" )
        return "image/bmp";

    if ( ext == ".woff" )
        return "font/woff";
    if ( ext == ".woff2" )
        return "font/woff2";
    if ( ext == ".ttf" )
        return "font/ttf";
    if ( ext == ".otf" )
        return "font/otf";

    if ( ext == ".mp4" )
        return "video/mp4";
    if ( ext == ".webm" )
        return "video/webm";

    if ( ext == ".mp3" )
        return "audio/mpeg";
    if ( ext == ".wav" )
        return "audio/wav";
    if ( ext == ".ogg" )
        return "audio/ogg";
    if ( ext == ".flac" )
        return "audio/flac";
    if ( ext == ".m4a" || ext == ".aac" )
        return "audio/mp4";
    if ( ext == ".opus" )
        return "audio/ogg";
    if ( ext == ".wma" )
        return "audio/x-ms-wma";
    return "application/octet-stream";
}

} // namespace http