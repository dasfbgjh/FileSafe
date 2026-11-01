#include "App.h"
#include "Logger.hpp"
#include "Utils.h"

#include <cstdlib>

int main( int argc, char *argv[] ) {
    system( "chcp 65001 >nul" );

    LOG_INFO << "default port: 8080, change port run: FileSafe [port]";

    utils::setAppDirectory( argc > 0 ? argv[0] : nullptr );
    unsigned short port = 8080;
    if ( argc > 1 ) {
        int value = std::atoi( argv[1] );
        if ( value > 0 && value < 65536 ) {
            port = static_cast<unsigned short>( value );
        }
    }

    App application( utils::appDirectory() );
    return application.run( port ) ? 0 : 1;
}