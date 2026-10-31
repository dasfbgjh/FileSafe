#ifndef APPEXTENSION_HPP
#define APPEXTENSION_HPP

#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

#if ( WIN32 )
#include <winsock2.h>
#include <windows.h>
#endif

class AppExtension : public std::streambuf {
private:
    std::ostream &m_stream;
    std::streambuf *const m_original;
    std::string m_buffer;

private:
    AppExtension( std::ostream &stream ) : m_stream( stream ),
                                           m_original( stream.rdbuf() ) {
    }

    AppExtension( AppExtension &other ) = delete;
    AppExtension( AppExtension &&other ) = delete;
    AppExtension &operator=( AppExtension &other ) = delete;
    AppExtension &operator=( AppExtension &&other ) = delete;

    ~AppExtension() {
        sync();
        m_stream.rdbuf( m_original );
    }

public:
    virtual int overflow( int c ) override {
        m_buffer += static_cast<unsigned char>( c );
        if ( c == '\n' ) {
            sync();
        }
        return c;
    }

    int sync() override {
        if ( !m_buffer.empty() ) {
            std::string gbkText = utf8ToGbk( m_buffer );
            m_original->sputn( gbkText.c_str(), gbkText.size() );
            m_original->pubsync();
            m_buffer = "";
        }
        return 0;
    }

    void setEnable( bool enable ) {
        if ( enable ) {
            m_stream.rdbuf( this );
        } else {
            m_stream.rdbuf( m_original );
        }
    }

public:
    static void setOutputGBKCode( bool enable ) {
#if ( WIN32 )
        static AppExtension out( std::cout );
        static AppExtension err( std::cerr );
        static AppExtension log( std::clog );

        out.setEnable( enable );
        err.setEnable( enable );
        log.setEnable( enable );

        HANDLE hOut = GetStdHandle( STD_OUTPUT_HANDLE );
        DWORD dwMode = 0;
        GetConsoleMode( hOut, &dwMode );
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode( hOut, dwMode );
#else
        std::cerr << "setOutputGBKCode only support windows" << std::endl;
#endif
    }

    static std::string utf8ToGbk( const std::string &utf8Str ) {
        if ( !isValidUtf8( utf8Str ) )
            return utf8Str;

#if ( WIN32 )
        // UTF-8 -> UTF-16 (宽字符)
        int utf16Len = MultiByteToWideChar( CP_UTF8, 0, utf8Str.c_str(), -1, nullptr, 0 );
        if ( utf16Len == 0 ) {
            return "";
        }

        std::wstring utf16Str( utf16Len, L'\0' );
        MultiByteToWideChar( CP_UTF8, 0, utf8Str.c_str(), -1, &utf16Str[0], utf16Len );

        // UTF-16 -> GBK (多字节字符)
        int gbkLen = WideCharToMultiByte( CP_ACP, 0, utf16Str.c_str(), -1, nullptr, 0, nullptr, nullptr );
        if ( gbkLen == 0 ) {
            return "";
        }

        std::string gbkStr( gbkLen, '\0' );
        WideCharToMultiByte( CP_ACP, 0, utf16Str.c_str(), -1, &gbkStr[0], gbkLen, nullptr, nullptr );

        // 去除结尾的'\0'（因转换时包含终止符）
        if ( !gbkStr.empty() && gbkStr.back() == '\0' ) {
            gbkStr.pop_back();
        }

        return gbkStr;
#else
        std::cerr << "utf8ToGbk only support windows" << std::endl;
        return "";
#endif
    }

    static bool isValidUtf8( const std::string &str ) {
        int i = 0;
        while ( i < str.size() ) {
            if ( ( str[i] & 0x80 ) == 0 ) {
                i++;
            } else if ( ( str[i] & 0xE0 ) == 0xC0 ) {
                if ( i + 1 >= str.size() )
                    return false;
                if ( ( str[i + 1] & 0xC0 ) != 0x80 )
                    return false;
                i += 2;
            } else if ( ( str[i] & 0xF0 ) == 0xE0 ) {
                if ( i + 2 >= str.size() )
                    return false;
                if ( ( str[i + 1] & 0xC0 ) != 0x80 )
                    return false;
                if ( ( str[i + 2] & 0xC0 ) != 0x80 )
                    return false;
                i += 3;
            } else if ( ( str[i] & 0xF8 ) == 0xF0 ) {
                if ( i + 3 >= str.size() )
                    return false;
                if ( ( str[i + 1] & 0xC0 ) != 0x80 )
                    return false;
                if ( ( str[i + 2] & 0xC0 ) != 0x80 )
                    return false;
                if ( ( str[i + 3] & 0xC0 ) != 0x80 )
                    return false;
                i += 4;
            } else {
                return false;
            }
        }
        return true;
    }

    static std::string gbkToUtf8( const std::string &gbkStr ) {
        if ( isValidUtf8( gbkStr ) )
            return gbkStr;

#if ( WIN32 )
        int utf16Len = MultiByteToWideChar( CP_ACP, 0, gbkStr.c_str(), -1, nullptr, 0 );
        if ( utf16Len == 0 ) {
            return gbkStr;
        }

        std::wstring utf16Str( utf16Len, L'\0' );
        MultiByteToWideChar( CP_ACP, 0, gbkStr.c_str(), -1, &utf16Str[0], utf16Len );

        int utf8Len = WideCharToMultiByte( CP_UTF8, 0, utf16Str.c_str(), -1, nullptr, 0, nullptr, nullptr );
        if ( utf8Len == 0 ) {
            return gbkStr;
        }

        std::string utf8Str( utf8Len, '\0' );
        WideCharToMultiByte( CP_UTF8, 0, utf16Str.c_str(), -1, &utf8Str[0], utf8Len, nullptr, nullptr );

        if ( !utf8Str.empty() && utf8Str.back() == '\0' ) {
            utf8Str.pop_back();
        }

        return utf8Str;
#else
        std::cerr << "gbkToUtf8 only support windows" << std::endl;
        return gbkStr;
#endif
    }
    static inline const std::tm &buildTime() {
        static std::tm tm_obj = {};
        static bool first = true;
        if ( first ) {
            first = false;
            std::string time_str = __DATE__ + std::string( " " ) + __TIME__;
            std::istringstream iss( time_str );
            iss >> std::get_time( &tm_obj, "%b %d %Y %H:%M:%S" );
            if ( iss.fail() )
                tm_obj = {};
        }
        return tm_obj;
    }

    static std::string buildTimeStr( const std::string &format = "%Y-%m-%d %H:%M:%S" ) {
        std::ostringstream oss;
        oss << std::put_time( &buildTime(), format.c_str() );
        return oss.str();
    }
};

#endif
// APPEXTENSION_HPP