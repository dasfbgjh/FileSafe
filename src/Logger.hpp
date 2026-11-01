#ifndef LOGGER_H
#define LOGGER_H

#include <mutex>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <vector>
#include <list>
#include <map>
#include <stack>
#include <queue>
#include <deque>
#include <set>
#include <thread>

#define LOG_DEBUG Logger::OStream( Logger::Level::DEBUG, __LINE__, __FILE__, __func__ )
#define LOG_LOG Logger::OStream( Logger::Level::LOG, __LINE__, __FILE__, __func__ )
#define LOG_INFO Logger::OStream( Logger::Level::INFO, __LINE__, __FILE__, __func__ )
#define LOG_WARN Logger::OStream( Logger::Level::WARN, __LINE__, __FILE__, __func__ )
#define LOG_ERROR Logger::OStream( Logger::Level::ERR, __LINE__, __FILE__, __func__ )

class Logger {
public:
    class OStream;
    enum class Level {
        DEBUG,
        LOG,
        INFO,
        WARN,
        ERR
    };

    enum class FileMode {
        FILE_OFF,
        SINGLE_FILE,
        MULTI_FILE
    };

private:
    std::mutex m_mutex;

    Level m_level;

    bool m_outputToConsole;

    FileMode m_outputMode;

    std::string m_logPath;

    bool m_addSpace = true;

#ifdef CMAKE_SOURCE_DIR
    std::filesystem::path m_sourceDir = std::filesystem::u8path( CMAKE_SOURCE_DIR );
#else
    std::filesystem::path m_sourceDir = std::filesystem::u8path( "./" );
#endif

public:
    static Logger &getInstance() {
        static Logger instance;
        return instance;
    }

    void setLevel( Level level ) {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_level = level;
    }

    void setOutputToConsole( bool enable ) {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_outputToConsole = enable;
    }

    void setOutputToFile( FileMode mode = FileMode::FILE_OFF, const std::string &path = "./logs" ) {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_outputMode = mode;
        m_logPath = path;
    }

    void setAddSpace( bool enable ) {
        m_addSpace = enable;
    }

private:
    Logger() : m_level( Level::DEBUG ),
               m_outputToConsole( true ),
               m_outputMode( FileMode::FILE_OFF ) {
    }

    Logger( const Logger & ) = delete;

    Logger( Logger && ) = delete;

    Logger &operator=( const Logger & ) = delete;

    Logger &operator=( Logger && ) = delete;

    ~Logger() = default;

    static std::string getLevelName( Level level ) {
        switch ( level ) {
        case Level::DEBUG:
            return " [D]:";
        case Level::LOG:
            return " [L]:";
        case Level::INFO:
            return " [I]:";
        case Level::WARN:
            return " [W]:";
        case Level::ERR:
            return " [E]:";
        default:
            return " [U]:";
        }
    }

    static std::string getCurrentTimeString( int flag = 0 ) {
        std::time_t now = std::time( nullptr );
        char buf[20];
        std::string format;
        if ( flag == 0 )
            format = "%Y-%m-%d %H:%M:%S";
        else if ( flag == 1 )
            format = "%Y-%m-%d";
        else if ( flag == 2 )
            format = "%H:%M:%S";
        else
            return "";
        std::strftime( buf, sizeof( buf ), format.c_str(), std::localtime( &now ) );
        return std::string( buf );
    }

    std::string consoleColor( const std::string &msg, int control = -1 ) {
        if ( control == -1 )
            return msg;
        return "\033[" + std::to_string( control ) + "m" + msg;
    }

    void log( const Level &level, const std::string &timestamp, const std::string &message, int line,
              const std::string &fileName, const std::string &func ) {
        std::lock_guard<std::mutex> lock( m_mutex );

        std::string levelName = getLevelName( level );
        auto threadId = std::this_thread::get_id();

        if ( m_outputToConsole ) {
            std::cout << consoleColor( timestamp, 35 );
            std::cout << consoleColor( levelName, 37 );
            switch ( level ) {
            case Level::DEBUG:
                std::cout << consoleColor( message, 32 );
                break;
            case Level::LOG:
                std::cout << consoleColor( message, 36 );
                break;
            case Level::INFO:
                std::cout << consoleColor( message, 34 );
                break;
            case Level::WARN:
                std::cout << consoleColor( message, 33 );
                break;
            case Level::ERR:
                std::cout << consoleColor( message, 31 );
                break;
            default:
                std::cout << consoleColor( message, 37 );
                break;
            }
            std::cout << consoleColor( " th=", 37 ) << threadId
                      << consoleColor( "", 0 ) << std::endl;
        }

        if ( m_outputMode != FileMode::FILE_OFF ) {
            std::filesystem::path filePath;
            if ( m_outputMode == FileMode::MULTI_FILE ) {
                std::filesystem::path dir = std::filesystem::u8path( m_logPath );
                std::filesystem::create_directories( dir );
                filePath = dir / std::filesystem::u8path( getCurrentTimeString( 1 ) + ".log" );
            } else {
                filePath = std::filesystem::u8path( m_logPath );
            }

            std::ofstream file( filePath, std::ios::app );
            if ( file.is_open() ) {
                file << timestamp
                     << levelName
                     << message
                     << " th=" << threadId
                     << " [" << fileName << ":" << line << "] " << func
                     << std::endl;
            }
        }
    }
};

class Logger::OStream {

private:
    int m_line;
    const char *m_file;
    const char *m_func;
    Level m_level;
    std::string m_timestamp;

    std::ostringstream m_outstream;

public:
    OStream( Level level, int line, const char *file, const char *func ) {
        m_line = line;
        m_file = file;
        m_func = func;
        m_level = level;
        m_timestamp = Logger::getCurrentTimeString(
            Logger::getInstance().m_outputMode == FileMode::SINGLE_FILE ? 0 : 2 );
    }

    ~OStream() {
        flush();
    }

    template <typename T>
    Logger::OStream &operator<<( const T &msg ) {
        return append( msg );
    }

    template <typename K, typename V>
    Logger::OStream &operator<<( const std::pair<K, V> &msg ) {
        this->operator<<( "(" );
        this->operator<<( msg.first );
        this->operator<<( ":" );
        this->operator<<( msg.second );
        return this->operator<<( ")" );
    }

    template <typename T>
    Logger::OStream &operator<<( const std::vector<T> &msg ) {
        this->operator<<( "[" );
        _for( msg.begin(), msg.end() );
        return this->operator<<( "]" );
    }

    template <typename T>
    Logger::OStream &operator<<( const std::list<T> &msg ) {
        this->operator<<( "[" );
        _for( msg.begin(), msg.end() );
        return this->operator<<( "]" );
    }

    template <typename K, typename V>
    Logger::OStream &operator<<( const std::map<K, V> &msg ) {
        this->operator<<( "{" );
        _for( msg.begin(), msg.end() );
        return this->operator<<( "}" );
    }

    template <typename K, typename V>
    Logger::OStream &operator<<( const std::multimap<K, V> &msg ) {
        this->operator<<( "{" );
        _for( msg.begin(), msg.end() );
        return this->operator<<( "}" );
    }

    template <typename T>
    Logger::OStream &operator<<( const std::initializer_list<T> &msg ) {
        this->operator<<( "{" );
        _for( msg.begin(), msg.end() );
        return this->operator<<( "}" );
    }

    template <typename... Argv>
    Logger::OStream &operator<<( const std::tuple<Argv...> msg ) {
        this->operator<<( "(" );
        _tuple<0, std::tuple_size<decltype( msg )>::value>( msg );
        return this->operator<<( ")" );
    }

    template <typename T>
    Logger::OStream &operator<<( const std::stack<T> &msg ) {
        this->operator<<( "stack{" );
        _kv( "top", msg.top(), "," );
        _kv( "size", msg.size() );
        return this->operator<<( "}" );
    }

    template <typename T>
    Logger::OStream &operator<<( const std::queue<T> &msg ) {
        this->operator<<( "queue{" );
        _kv( "front", msg.front(), "," );
        _kv( "back", msg.back(), "," );
        _kv( "size", msg.size() );
        return this->operator<<( "}" );
    }

    template <typename T>
    Logger::OStream &operator<<( const std::deque<T> &msg ) {
        this->operator<<( "deque[" );
        _for( msg.begin(), msg.end() );
        return this->operator<<( "]" );
    }

    template <typename T>
    Logger::OStream &operator<<( const std::set<T> &msg ) {
        this->operator<<( "set[" );
        _for( msg.begin(), msg.end() );
        return this->operator<<( "]" );
    }

    template <typename T>
    Logger::OStream &operator<<( const std::multiset<T> &msg ) {
        this->operator<<( "mset[" );
        _for( msg.begin(), msg.end() );
        return this->operator<<( "]" );
    }

    template <typename T>
    Logger::OStream &append( const T &msg ) {
        if ( m_level < getInstance().m_level )
            return *this;
        if ( getInstance().m_addSpace )
            m_outstream << " ";
        m_outstream << msg;
        return *this;
    }

    void flush() {
        if ( m_level < getInstance().m_level )
            return;
        std::string msg = m_outstream.str();

        std::string fileName = m_file;
        try {
            fileName = std::filesystem::relative(
                           std::filesystem::u8path( m_file ),
                           Logger::getInstance().m_sourceDir )
                           .u8string();
        } catch ( ... ) {
        }
        getInstance().log( m_level,
                           m_timestamp,
                           msg,
                           m_line,
                           fileName,
                           m_func );
        m_outstream.str( "" );
    }

    /******************************************************* */
    template <typename Iterator>
    void _for( Iterator begin, Iterator end ) {
        for ( auto item = begin; item != end; ++item ) {
            if ( item != begin )
                this->operator<<( "," );
            this->operator<<( *item );
        }
    }

    template <size_t index, size_t count, typename... Argv>
    void _tuple( const std::tuple<Argv...> &msg ) {
        if constexpr ( index < count ) {
            if constexpr ( index > 0 )
                this->operator<<( "," );
            this->operator<<( std::get<index>( msg ) );
        }
        if constexpr ( index < count - 1 )
            _tuple<index + 1, count>( msg );
    }

    template <typename T>
    void _kv( const char *key, const T &value, const std::string &delimiter = std::string() ) {
        this->operator<<( key );
        this->operator<<( "=" );
        this->operator<<( value );
        if ( !delimiter.empty() )
            this->operator<<( delimiter );
    }
};

#endif
// LOGGER_H