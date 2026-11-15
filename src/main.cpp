#include "App.h"
#include "Logger.hpp"
#include "Utils.h"

#include "cli11/CLI11.hpp"

#include <cctype>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

Logger::Level logLevelFromString( const std::string &text ) {
    std::string value;
    value.reserve( text.size() );
    for ( char c : text ) {
        value += static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
    }

    if ( value == "log" ) {
        return Logger::Level::LOG;
    }
    if ( value == "info" ) {
        return Logger::Level::INFO;
    }
    if ( value == "warn" ) {
        return Logger::Level::WARN;
    }
    if ( value == "error" ) {
        return Logger::Level::ERR;
    }
    return Logger::Level::DEBUG;
}

fs::path resolveDir( const std::string &option, const char *defaultName ) {
    if ( option.empty() ) {
        return utils::appDirectory() / defaultName;
    }

    fs::path path = utils::toPath( option );
    if ( path.is_relative() ) {
        path = utils::appDirectory() / path;
    }

    std::error_code ec;
    fs::path normalized = fs::weakly_canonical( path, ec );
    return ec ? path.lexically_normal() : normalized;
}

} // namespace

int main( int argc, char *argv[] ) {
    system( "chcp 65001 >nul" );

    utils::setAppDirectory( argc > 0 ? argv[0] : nullptr );
    CLI::App cli{ "FileSafe 文件加密服务" };
    cli.set_help_flag( "-h,--help", "显示帮助信息并退出" );

    int port = 8080;
    std::string logLevel = "info";
    std::string dataDirOption;
    std::string outputDirOption;

    cli.add_option( "-p,--port", port, "监听端口" )
        ->default_val( 8080 )
        ->check( CLI::Range( 1, 65535 ) );
    cli.add_option( "-l,--log-level", logLevel, "日志等级: debug/log/info/warn/error" )
        ->default_val( "info" )
        ->check( CLI::IsMember( { "debug", "log", "info", "warn", "error" }, CLI::ignore_case ) );
    cli.add_option( "-d,--data-dir", dataDirOption, "存储目录（默认: 程序目录下的 data）" );
    cli.add_option( "-o,--output-dir", outputDirOption, "导出目录（默认: 程序目录下的 output）" );

    CLI11_PARSE( cli, argc, argv );

    Logger::getInstance().setLevel( logLevelFromString( logLevel ) );

    unsigned short listenPort = static_cast<unsigned short>( port );
    fs::path dataDir = resolveDir( dataDirOption, "data" );
    fs::path outputDir = resolveDir( outputDirOption, "output" );

    LOG_INFO << "程序启动, 端口: " + std::to_string( listenPort ) + ", 日志等级: " + logLevel;
    LOG_DEBUG << "程序所在目录: " + utils::appDirectory().u8string();
    LOG_INFO << "存储目录: " + dataDir.u8string();
    LOG_INFO << "导出目录: " + outputDir.u8string();

    App application( dataDir, outputDir );
    bool success = application.run( listenPort );
    if ( success ) {
        LOG_INFO << "程序正常退出";
    } else {
        LOG_ERROR << "程序异常退出";
    }
    return success ? 0 : 1;
}