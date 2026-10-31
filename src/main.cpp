#include "App.h"
#include "Utils.h"
#include <iostream>

int main( int argc, char *argv[] ) {
    system( "chcp 65001 >nul" );

    std::cout << "default port: 8080, change port run: "
              << "FileSafe [port]" << std::endl;

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