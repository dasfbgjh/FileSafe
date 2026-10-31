#include "Utils.h"

#include "resource.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <vector>

#include <openssl/rand.h>

namespace utils {

namespace fs = std::filesystem;

namespace {

fs::path g_appDirectory;

} // namespace

void setAppDirectory( const char *argv0 ) {
    std::error_code ec;
    if ( argv0 && *argv0 ) {
        fs::path exe = fs::absolute( fs::path( argv0 ), ec );
        if ( !ec ) {
            g_appDirectory = exe.parent_path();
            return;
        }
        ec.clear();
    }
    fs::path cwd = fs::current_path( ec );
    g_appDirectory = ec ? fs::path( "." ) : cwd;
}

const fs::path &appDirectory() {
    if ( g_appDirectory.empty() ) {
        setAppDirectory( nullptr );
    }
    return g_appDirectory;
}

std::string randomHex( std::size_t bytes ) {
    std::vector<unsigned char> raw( bytes );
    if ( RAND_bytes( raw.data(), static_cast<int>( raw.size() ) ) != 1 ) {
        return {};
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill( '0' );
    for ( unsigned char byte : raw ) {
        oss << std::setw( 2 ) << static_cast<int>( byte );
    }
    return oss.str();
}

bool equalsIgnoreCase( const std::string &lhs, const std::string &rhs ) {
    if ( lhs.size() != rhs.size() ) {
        return false;
    }
    for ( std::size_t i = 0; i < lhs.size(); ++i ) {
        if ( std::tolower( static_cast<unsigned char>( lhs[i] ) ) !=
             std::tolower( static_cast<unsigned char>( rhs[i] ) ) ) {
            return false;
        }
    }
    return true;
}

std::string findHeader( const std::map<std::string, std::string> &headers, const std::string &name ) {
    for ( const auto &[key, value] : headers ) {
        if ( equalsIgnoreCase( key, name ) ) {
            return value;
        }
    }
    return {};
}

RangeKind parseRange( const std::string &header,
                      std::uint64_t size,
                      std::uint64_t &offset,
                      std::uint64_t &length ) {
    if ( header.empty() ) {
        return RangeKind::None;
    }

    const std::string prefix = "bytes=";
    if ( header.size() <= prefix.size() ||
         !equalsIgnoreCase( header.substr( 0, prefix.size() ), prefix ) ) {
        return RangeKind::None;
    }

    std::string spec = header.substr( prefix.size() );
    auto comma = spec.find( ',' );
    if ( comma != std::string::npos ) {
        spec = spec.substr( 0, comma );
    }

    auto trim = []( std::string text ) {
        auto begin = text.find_first_not_of( " \t" );
        if ( begin == std::string::npos ) {
            return std::string();
        }
        auto end = text.find_last_not_of( " \t" );
        return text.substr( begin, end - begin + 1 );
    };
    spec = trim( spec );

    auto dash = spec.find( '-' );
    if ( dash == std::string::npos ) {
        return RangeKind::None;
    }

    std::string startText = trim( spec.substr( 0, dash ) );
    std::string endText = trim( spec.substr( dash + 1 ) );

    auto toNumber = []( const std::string &text, std::uint64_t &out ) {
        if ( text.empty() ) {
            return false;
        }
        std::uint64_t value = 0;
        for ( char ch : text ) {
            if ( ch < '0' || ch > '9' ) {
                return false;
            }
            if ( value > ( UINT64_MAX - static_cast<std::uint64_t>( ch - '0' ) ) / 10 ) {
                return false;
            }
            value = value * 10 + static_cast<std::uint64_t>( ch - '0' );
        }
        out = value;
        return true;
    };

    if ( startText.empty() ) {
        std::uint64_t suffix = 0;
        if ( !toNumber( endText, suffix ) ) {
            return RangeKind::None;
        }
        if ( suffix == 0 || size == 0 ) {
            return RangeKind::Unsatisfiable;
        }
        length = std::min( suffix, size );
        offset = size - length;
        return RangeKind::Satisfiable;
    }

    std::uint64_t start = 0;
    if ( !toNumber( startText, start ) ) {
        return RangeKind::None;
    }
    if ( start >= size ) {
        return RangeKind::Unsatisfiable;
    }

    std::uint64_t end = size ? size - 1 : 0;
    if ( !endText.empty() ) {
        if ( !toNumber( endText, end ) ) {
            return RangeKind::None;
        }
        if ( end >= size ) {
            end = size - 1;
        }
    }
    if ( end < start ) {
        return RangeKind::Unsatisfiable;
    }

    offset = start;
    length = end - start + 1;
    return RangeKind::Satisfiable;
}

bool parseJsonObject( const std::string &body, boost::json::object &out ) {
    try {
        auto value = boost::json::parse( body );
        if ( !value.is_object() ) {
            return false;
        }
        out = value.as_object();
        return true;
    } catch ( const std::exception & ) {
        return false;
    }
}

std::string jsonStringField( const boost::json::object &object, const char *key ) {
    auto *value = object.if_contains( key );
    if ( !value || !value->is_string() ) {
        return {};
    }
    return std::string( value->as_string() );
}

boost::json::value toJsonString( const std::string &text ) {
    return boost::json::value( boost::json::string_view( text.data(), text.size() ) );
}

std::string percentEncode( const std::string &text ) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill( '0' );
    for ( unsigned char c : text ) {
        const bool unreserved = ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) ||
                                ( c >= '0' && c <= '9' ) || c == '-' || c == '_' ||
                                c == '.' || c == '~';
        if ( unreserved ) {
            oss << static_cast<char>( c );
        } else {
            oss << '%' << std::setw( 2 ) << static_cast<int>( c );
        }
    }
    return oss.str();
}

std::string urlDecode( const std::string &text ) {
    std::string result;
    result.reserve( text.size() );
    for ( std::size_t i = 0; i < text.size(); ++i ) {
        if ( text[i] == '%' && i + 2 < text.size() ) {
            char hex[3] = { text[i + 1], text[i + 2], '\0' };
            char *end = nullptr;
            long value = std::strtol( hex, &end, 16 );
            if ( end != hex ) {
                result += static_cast<char>( value );
                i += 2;
                continue;
            }
        } else if ( text[i] == '+' ) {
            result += ' ';
            continue;
        }
        result += text[i];
    }
    return result;
}

std::string contentDisposition( bool download, const std::string &path ) {
    std::string name = fs::path( path ).filename().string();
    if ( name.empty() ) {
        name = "file";
    }

    std::string fallback;
    for ( unsigned char c : name ) {
        if ( c >= 0x20 && c < 0x7f && c != '"' && c != '\\' ) {
            fallback += static_cast<char>( c );
        } else {
            fallback += '_';
        }
    }
    if ( fallback.empty() ) {
        fallback = "file";
    }

    std::string result = download ? "attachment" : "inline";
    result += "; filename=\"" + fallback + "\"";
    result += "; filename*=UTF-8''" + percentEncode( name );
    return result;
}

#ifdef RESOURCE_PATH
namespace {

fs::path resourceRoot() {
    static const fs::path root = fs::path( RESOURCE_PATH ).lexically_normal();
    return root;
}

bool resolveLocalResource( const std::string &resName, fs::path &fullPath ) {
    fs::path relative;
    for ( const fs::path &part : fs::path( resName ) ) {
        if ( part == ".." || part.has_root_name() ) {
            return false;
        }
        if ( part.has_root_directory() || part == "." ) {
            continue;
        }
        relative /= part;
    }

    const fs::path root = resourceRoot();
    fs::path candidate = ( root / relative ).lexically_normal();
    auto rootIt = root.begin();
    auto candidateIt = candidate.begin();
    for ( ; rootIt != root.end(); ++rootIt, ++candidateIt ) {
        if ( candidateIt == candidate.end() || *candidateIt != *rootIt ) {
            return false;
        }
    }

    fullPath = std::move( candidate );
    return true;
}

} // namespace
#endif

bool resourceExists( const std::string &path ) {
#ifdef RESOURCE_PATH
    fs::path fullPath;
    if ( !resolveLocalResource( path, fullPath ) ) {
        return false;
    }
    return fs::exists( fullPath );
#else
    return resource_exists( path.c_str() ) != 0;
#endif
}

bool loadResource( const std::string &path, std::string &body ) {
#ifdef RESOURCE_PATH
    fs::path fullPath;
    if ( !resolveLocalResource( path, fullPath ) ) {
        return false;
    }
    std::ifstream in( fullPath, std::ios::binary );
    if ( !in ) {
        return false;
    }
    body.assign( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
    return true;
#else
    const unsigned char *data = nullptr;
    int size = resource_get( path.c_str(), &data );
    if ( size < 0 || !data ) {
        return false;
    }
    body.assign( reinterpret_cast<const char *>( data ), static_cast<std::size_t>( size ) );
    return true;
#endif
}

} // namespace utils