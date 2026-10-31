#ifndef APP_UTILS_H
#define APP_UTILS_H

#include <boost/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace utils {

void setAppDirectory( const char *argv0 );

const std::filesystem::path &appDirectory();

std::string randomHex( std::size_t bytes );

bool equalsIgnoreCase( const std::string &lhs, const std::string &rhs );

std::string findHeader( const std::map<std::string, std::string> &headers, const std::string &name );

enum class RangeKind {
    None,
    Unsatisfiable,
    Satisfiable
};

RangeKind parseRange( const std::string &header,
                      std::uint64_t size,
                      std::uint64_t &offset,
                      std::uint64_t &length );

bool parseJsonObject( const std::string &body, boost::json::object &out );

std::string jsonStringField( const boost::json::object &object, const char *key );

boost::json::value toJsonString( const std::string &text );

std::string percentEncode( const std::string &text );

std::string urlDecode( const std::string &text );

std::string contentDisposition( bool download, const std::string &path );

bool resourceExists( const std::string &path );

bool loadResource( const std::string &path, std::string &body );

} // namespace utils

#endif // APP_UTILS_H