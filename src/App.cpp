#include "App.h"
#include "Logger.hpp"
#include "Utils.h"

#include <boost/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

boost::json::array filesJson( const std::vector<cryptostore::FileEntry> &files ) {
    boost::json::array items;
    for ( const auto &entry : files ) {
        boost::json::object item;
        item["id"] = entry.id;
        item["path"] = utils::toJsonString( entry.path );
        item["size"] = entry.size;
        items.push_back( std::move( item ) );
    }
    return items;
}

} // namespace

App::App( const fs::path &baseDir )
    : m_baseDir( baseDir ),
      m_store( baseDir / "data" ),
      m_queue( m_store ) {
}

App::~App() {
    m_queue.stop();
}

bool App::run( unsigned short port ) {
    registerRoutes();

    if ( !m_server.listen( "127.0.0.1", port ) ) {
        LOG_ERROR << "Failed to listen on port " + std::to_string( port );
        return false;
    }

    m_queue.setCompletionHandler( [this]( const std::string &store, const cryptostore::StoreInfo &info ) {
        m_sessions.refreshDir( store, info );
    } );

    m_queue.start();

    LOG_INFO << "Server started: http://127.0.0.1:" + std::to_string( port );
    LOG_INFO << "Data directory: " + ( m_baseDir / "data" ).u8string();

    m_server.getIoContext().run();

    m_queue.stop();
    return true;
}

void App::fail( Response &resp, int code, const std::string &reason, const std::string &message ) {
    resp.setStatus( code, reason );
    resp.setText( message );
}

void App::registerRoutes() {
    m_server.get( "/api/stores", [this]( const Request &req, Response &resp ) { handleStores( req, resp ); } );
    m_server.post( "/api/encrypt", [this]( const Request &req, Response &resp ) { handleEncrypt( req, resp ); } );
    m_server.get( "/api/tasks", [this]( const Request &req, Response &resp ) { handleTasks( req, resp ); } );
    m_server.post( "/api/open", [this]( const Request &req, Response &resp ) { handleOpen( req, resp ); } );
    m_server.post( "/api/add", [this]( const Request &req, Response &resp ) { handleAdd( req, resp ); } );
    m_server.post( "/api/delete", [this]( const Request &req, Response &resp ) { handleDelete( req, resp ); } );
    m_server.post( "/api/export", [this]( const Request &req, Response &resp ) { handleExport( req, resp ); } );
    m_server.get( "/api/file", [this]( const Request &req, Response &resp ) { handleFile( req, resp ); } );
    m_server.get( "/api/files", [this]( const Request &req, Response &resp ) { handleFiles( req, resp ); } );
    m_server.get( "*", [this]( const Request &req, Response &resp ) { handleStatic( req, resp ); } );
}

void App::handleStores( const Request &, Response &resp ) {
    boost::json::object root;
    root["ok"] = true;

    boost::json::array names;
    for ( const auto &name : m_store.listStores() ) {
        names.push_back( utils::toJsonString( name ) );
    }
    root["stores"] = std::move( names );

    resp.setJson( boost::json::serialize( root ) );
}

void App::handleEncrypt( const Request &req, Response &resp ) {
    boost::json::object body;
    if ( !utils::parseJsonObject( req.body, body ) ) {
        fail( resp, 400, "Bad Request", "请求体格式错误" );
        return;
    }

    std::string dir = utils::jsonStringField( body, "dir" );
    std::string password = utils::jsonStringField( body, "password" );
    if ( dir.empty() || password.empty() ) {
        fail( resp, 400, "Bad Request", "目录和密码不能为空" );
        return;
    }

    std::error_code ec;
    fs::path sourceDir = utils::toPath( dir );
    if ( !fs::exists( sourceDir, ec ) || !fs::is_directory( sourceDir, ec ) ) {
        fail( resp, 400, "Bad Request", "待加密目录不存在或不是目录: " + dir );
        return;
    }

    std::string id = m_queue.submitEncrypt( dir, password );
    if ( id.empty() ) {
        fail( resp, 500, "Internal Server Error", "创建加密任务失败" );
        return;
    }

    boost::json::object root;
    root["ok"] = true;
    root["id"] = utils::toJsonString( id );
    resp.setStatus( 202, "Accepted" );
    resp.setJson( boost::json::serialize( root ) );
}

void App::handleTasks( const Request &, Response &resp ) {
    std::uint64_t finished = 0;
    auto tasks = m_queue.list( finished );

    boost::json::object root;
    root["ok"] = true;
    root["finished"] = finished;

    boost::json::array items;
    for ( const auto &task : tasks ) {
        boost::json::object item;
        item["id"] = utils::toJsonString( task.id );
        item["kind"] = utils::toJsonString( task.kind );
        item["title"] = utils::toJsonString( task.title );
        item["dir"] = utils::toJsonString( task.dir );
        item["state"] = utils::toJsonString( task.state );
        item["error"] = utils::toJsonString( task.error );
        item["current"] = utils::toJsonString( task.current );
        item["filesDone"] = static_cast<std::int64_t>( task.filesDone );
        item["filesTotal"] = static_cast<std::int64_t>( task.filesTotal );
        item["bytesDone"] = task.bytesDone;
        item["bytesTotal"] = task.bytesTotal;

        auto begin = ( task.startedAt != std::chrono::steady_clock::time_point{} )
                         ? task.startedAt
                         : task.createdAt;
        auto end = ( task.finishedAt != std::chrono::steady_clock::time_point{} )
                       ? task.finishedAt
                       : std::chrono::steady_clock::now();
        item["elapsed"] = std::chrono::duration<double>( end - begin ).count();

        items.push_back( std::move( item ) );
    }
    root["tasks"] = std::move( items );

    resp.setJson( boost::json::serialize( root ) );
}

void App::handleOpen( const Request &req, Response &resp ) {
    boost::json::object body;
    if ( !utils::parseJsonObject( req.body, body ) ) {
        fail( resp, 400, "Bad Request", "请求体格式错误" );
        return;
    }

    std::string dir = utils::jsonStringField( body, "dir" );
    std::string password = utils::jsonStringField( body, "password" );
    if ( dir.empty() || password.empty() ) {
        fail( resp, 400, "Bad Request", "目录和密码不能为空" );
        return;
    }

    cryptostore::StoreInfo info;
    std::string error;
    if ( !m_store.openStore( dir, password, info, error ) ) {
        fail( resp, 403, "Forbidden", error );
        return;
    }

    std::string token = m_sessions.create( dir, password, info );
    if ( token.empty() ) {
        fail( resp, 500, "Internal Server Error", "创建会话失败" );
        return;
    }

    boost::json::object root;
    root["ok"] = true;
    root["token"] = utils::toJsonString( token );
    root["dir"] = utils::toJsonString( info.name );
    root["created"] = info.created;
    root["files"] = filesJson( info.files );

    resp.setJson( boost::json::serialize( root ) );
}

void App::handleAdd( const Request &req, Response &resp ) {
    boost::json::object body;
    if ( !utils::parseJsonObject( req.body, body ) ) {
        fail( resp, 400, "Bad Request", "请求体格式错误" );
        return;
    }

    std::string token = utils::jsonStringField( body, "token" );
    std::string target = utils::jsonStringField( body, "target" );
    std::string path = utils::jsonStringField( body, "path" );
    if ( token.empty() ) {
        fail( resp, 401, "Unauthorized", "缺少会话令牌" );
        return;
    }
    if ( path.empty() ) {
        fail( resp, 400, "Bad Request", "路径不能为空" );
        return;
    }

    Session session;
    if ( !m_sessions.get( token, session ) ) {
        fail( resp, 401, "Unauthorized", "会话已失效，请重新输入密码打开目录" );
        return;
    }

    std::string name = utils::toPath( path ).filename().u8string();
    if ( name.empty() ) {
        name = path;
    }
    std::string title = "添加 " + name + " 到 " + ( target.empty() ? "根目录" : target );

    std::string id = m_queue.submitAdd( session.dir, session.password, target, utils::toPath( path ), title );
    if ( id.empty() ) {
        fail( resp, 500, "Internal Server Error", "加入任务队列失败" );
        return;
    }

    boost::json::object root;
    root["ok"] = true;
    root["id"] = utils::toJsonString( id );
    root["state"] = "queued";
    resp.setStatus( 202, "Accepted" );
    resp.setJson( boost::json::serialize( root ) );
}

void App::handleDelete( const Request &req, Response &resp ) {
    boost::json::object body;
    if ( !utils::parseJsonObject( req.body, body ) ) {
        fail( resp, 400, "Bad Request", "请求体格式错误" );
        return;
    }

    std::string token = utils::jsonStringField( body, "token" );
    std::string path = utils::jsonStringField( body, "path" );
    if ( token.empty() ) {
        fail( resp, 401, "Unauthorized", "缺少会话令牌" );
        return;
    }
    if ( path.empty() ) {
        fail( resp, 400, "Bad Request", "路径不能为空" );
        return;
    }

    Session session;
    if ( !m_sessions.get( token, session ) ) {
        fail( resp, 401, "Unauthorized", "会话已失效，请重新输入密码打开目录" );
        return;
    }

    std::string title = "删除 " + path;

    std::string id = m_queue.submitRemove( session.dir, session.password, path, title );
    if ( id.empty() ) {
        fail( resp, 500, "Internal Server Error", "加入任务队列失败" );
        return;
    }

    boost::json::object root;
    root["ok"] = true;
    root["id"] = utils::toJsonString( id );
    root["state"] = "queued";
    resp.setStatus( 202, "Accepted" );
    resp.setJson( boost::json::serialize( root ) );
}

void App::handleExport( const Request &req, Response &resp ) {
    boost::json::object body;
    if ( !utils::parseJsonObject( req.body, body ) ) {
        fail( resp, 400, "Bad Request", "请求体格式错误" );
        return;
    }

    std::string token = utils::jsonStringField( body, "token" );
    std::string path = utils::jsonStringField( body, "path" );
    if ( token.empty() ) {
        fail( resp, 401, "Unauthorized", "缺少会话令牌" );
        return;
    }

    Session session;
    if ( !m_sessions.get( token, session ) ) {
        fail( resp, 401, "Unauthorized", "会话已失效，请重新输入密码打开目录" );
        return;
    }

    std::string prefix = path.empty() ? std::string() : path + "/";
    bool matched = false;
    for ( const auto &[id, entry] : session.files ) {
        if ( path.empty() || entry.path == path ||
             entry.path.compare( 0, prefix.size(), prefix ) == 0 ) {
            matched = true;
            break;
        }
    }
    if ( !matched ) {
        fail( resp, 404, "Not Found", "目录中没有可导出的文件" );
        return;
    }

    fs::path destRoot = m_baseDir / "output" / utils::toPath( session.dir );
    std::string title = "导出 " + ( path.empty() ? session.dir : path ) + " 到 output";

    std::string id = m_queue.submitExport( session.dir, session.password, path, destRoot, title );
    if ( id.empty() ) {
        fail( resp, 500, "Internal Server Error", "加入任务队列失败" );
        return;
    }

    boost::json::object root;
    root["ok"] = true;
    root["id"] = utils::toJsonString( id );
    root["state"] = "queued";
    root["dest"] = utils::toJsonString( destRoot.u8string() );
    resp.setStatus( 202, "Accepted" );
    resp.setJson( boost::json::serialize( root ) );
}

void App::handleFiles( const Request &req, Response &resp ) {
    auto tokenIt = req.params.find( "token" );
    if ( tokenIt == req.params.end() ) {
        fail( resp, 400, "Bad Request", "缺少会话令牌" );
        return;
    }

    Session session;
    if ( !m_sessions.get( tokenIt->second, session ) ) {
        fail( resp, 401, "Unauthorized", "会话已失效，请重新输入密码打开目录" );
        return;
    }

    std::vector<cryptostore::FileEntry> files;
    files.reserve( session.files.size() );
    for ( const auto &[id, entry] : session.files ) {
        files.push_back( entry );
    }

    boost::json::object root;
    root["ok"] = true;
    root["dir"] = utils::toJsonString( session.dir );
    root["files"] = filesJson( files );
    resp.setJson( boost::json::serialize( root ) );
}

void App::handleFile( const Request &req, Response &resp ) {
    auto tokenIt = req.params.find( "token" );
    if ( tokenIt == req.params.end() ) {
        fail( resp, 400, "Bad Request", "缺少会话令牌" );
        return;
    }

    Session session;
    if ( !m_sessions.get( tokenIt->second, session ) ) {
        fail( resp, 401, "Unauthorized", "会话已失效，请重新输入密码打开目录" );
        return;
    }

    auto idIt = req.params.find( "id" );
    int id = ( idIt != req.params.end() ) ? std::atoi( idIt->second.c_str() ) : 0;

    auto entryIt = session.files.find( id );
    if ( entryIt == session.files.end() ) {
        fail( resp, 404, "Not Found", "文件不存在" );
        return;
    }
    const cryptostore::FileEntry &entry = entryIt->second;

    std::uint64_t offset = 0;
    std::uint64_t length = entry.size;
    utils::RangeKind range = utils::parseRange(
        utils::findHeader( req.headers, "Range" ), entry.size, offset, length );

    if ( range == utils::RangeKind::Unsatisfiable ) {
        resp.setStatus( 416, "Range Not Satisfiable" );
        resp.setText( "请求的字节范围无效" );
        resp.headers["Accept-Ranges"] = "bytes";
        resp.headers["Content-Range"] = "bytes */" + std::to_string( entry.size );
        return;
    }

    std::string error;
    std::shared_ptr<cryptostore::FileStream> stream =
        m_store.openStream( session.dir, entry, session.password, offset, length, error );
    if ( !stream ) {
        fail( resp, 500, "Internal Server Error", error );
        return;
    }

    bool download = req.params.count( "download" ) > 0 && req.params.at( "download" ) == "1";
    std::string contentType = HttpServer::contentType( utils::toPath( entry.path ) );

    resp.setStream( length, [stream]( std::string &chunk, std::size_t maxSize ) {
        return stream->read( chunk, maxSize );
    } );
    resp.headers["Content-Type"] = contentType;
    resp.headers["Content-Disposition"] = utils::contentDisposition( download, entry.path );
    resp.headers["X-Content-Type-Options"] = "nosniff";
    resp.headers["Accept-Ranges"] = "bytes";
    if ( !download && contentType != "application/pdf" ) {
        resp.headers["Content-Security-Policy"] = "sandbox";
    }

    if ( range == utils::RangeKind::Satisfiable ) {
        resp.setStatus( 206, "Partial Content" );
        resp.headers["Content-Range"] = "bytes " + std::to_string( offset ) + "-" +
                                        std::to_string( offset + length - 1 ) + "/" +
                                        std::to_string( entry.size );
    }
}

bool App::serveResource( Response &resp, const std::string &resName ) {
    std::string content;
    if ( !utils::loadResource( resName, content ) ) {
        return false;
    }
    resp.body = std::move( content );
    resp.headers["Content-Type"] = HttpServer::contentType( utils::toPath( resName ) );
    return true;
}

void App::redirectPage( Response &resp, const std::string &path ) {
    std::string html = "<html>"
                       "<head> <title>重定向页</title> </head>"
                       "<body> <script>";
    html += "var params = window.location.search; window.location.href = '" + path + "' + params;";
    html += "</script> </body>"
            "</html>";
    resp.setHtml( html );
}

void App::handleStatic( const Request &req, Response &resp ) {
    std::string path = utils::urlDecode( req.target );
    std::string fullPath = std::string( RESOURCE_PREFIX ) + path;
    if ( serveResource( resp, fullPath ) ) {
        return;
    }

    if ( path.find( '.' ) == std::string::npos ) {
        std::string base = path;
        while ( base.size() > 0 && base.back() == '/' )
            base.pop_back();

        if ( utils::resourceExists( std::string( RESOURCE_PREFIX ) + base + "/index.html" ) ) {
            redirectPage( resp, base + "/index.html" );
            return;
        }
        if ( utils::resourceExists( std::string( RESOURCE_PREFIX ) + base + ".html" ) ) {
            redirectPage( resp, base + ".html" );
            return;
        }
    }

    resp.setStatus( 404, "Not Found" );
    serveResource( resp, std::string( RESOURCE_PREFIX ) + "/404.html" );
}