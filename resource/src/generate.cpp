#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

struct FileInfo {
    std::string arrayName;
    std::string relativePath;
    fs::path filePath;
    size_t fileSize;
};

std::string escapePath( const std::string &path ) {
    std::string result = path;
    for ( char &c : result ) {
        if ( c == '\\' ) {
            c = '/';
        }
    }
    return result;
}

void generateData( const FileInfo &file, std::ostream &out ) {
    std::ifstream ifs( file.filePath, std::ios::binary );
    if ( !ifs ) {
        std::cerr << "Warning: Cannot read file: " << file.filePath << std::endl;
        return;
    }

    int n;
    const char *typeStr;
    const char *suffix;
    if ( file.fileSize % sizeof( unsigned long long ) == 0 ) {
        n = sizeof( unsigned long long );
        typeStr = "unsigned long long";
        suffix = "ULL";
    } else if ( file.fileSize % sizeof( unsigned int ) == 0 ) {
        n = sizeof( unsigned int );
        typeStr = "unsigned int";
        suffix = "U";
    } else if ( file.fileSize % sizeof( unsigned short ) == 0 ) {
        n = sizeof( unsigned short );
        typeStr = "unsigned short";
        suffix = "";
    } else {
        n = sizeof( unsigned char );
        typeStr = "unsigned char";
        suffix = "";
    }

    out << "static const " << typeStr << " " << file.arrayName << "[]={";

    constexpr size_t CHUNK_SIZE = 8192; // 值必须能被n整除
    std::vector<unsigned char> chunk( CHUNK_SIZE );
    bool first = true;

    while ( ifs ) {
        ifs.read( reinterpret_cast<char *>( chunk.data() ), CHUNK_SIZE );
        auto bytesRead = static_cast<size_t>( ifs.gcount() );
        if ( bytesRead == 0 )
            break;

        for ( size_t i = 0; i < bytesRead; i += n ) {
            if ( !first )
                out << ",";
            else
                first = false;

            if ( n == sizeof( unsigned long long ) ) {
                unsigned long long value;
                std::memcpy( &value, &chunk[i], n );
                out << value << suffix;
            } else if ( n == sizeof( unsigned int ) ) {
                unsigned int value;
                std::memcpy( &value, &chunk[i], n );
                out << value << suffix;
            } else if ( n == sizeof( unsigned short ) ) {
                unsigned short value;
                std::memcpy( &value, &chunk[i], n );
                out << value;
            } else {
                out << static_cast<int>( chunk[i] );
            }
        }
    }

    out << "};\n";
}

bool generateResourceHeader( const std::string &inputDir, const std::string &outputPath ) {
    static int number = 0;
    std::vector<FileInfo> files;

    if ( !fs::exists( inputDir ) || !fs::is_directory( inputDir ) ) {
        std::cerr << "Error: Input directory does not exist: " << inputDir << std::endl;
        return false;
    }

    for ( const auto &entry : fs::recursive_directory_iterator( inputDir ) ) {
        if ( entry.is_regular_file() ) {
            FileInfo info;

            fs::path relPath = fs::relative( entry.path(), inputDir );
            info.relativePath = "/" + escapePath( relPath.string() );
            info.arrayName = "D" + std::to_string( number++ );
            info.filePath = entry.path();
            info.fileSize = fs::file_size( entry.path() );

            files.push_back( info );
        }
    }

    std::sort( files.begin(), files.end(), []( const FileInfo &a, const FileInfo &b ) {
        return a.relativePath < b.relativePath;
    } );

    std::ofstream out( outputPath );
    if ( !out ) {
        std::cerr << "Error: Cannot create output file: " << outputPath << std::endl;
        return false;
    }

    fs::path headerName = fs::path( outputPath ).filename();
    std::string guardName = headerName.string();
    for ( char &c : guardName ) {
        if ( c == '.' ) {
            c = '_';
        } else {
            c = std::toupper( c );
        }
    }

    std::time_t now = std::time( nullptr );
    std::tm *local = std::localtime( &now );
    out << "// Generated on: " << std::asctime( local ) << "\n";
    out << "#ifndef " << guardName << "\n";
    out << "#define " << guardName << "\n\n";

    for ( const auto &file : files ) {
        generateData( file, out );
    }

    out << "// ===========================================\n";
    out << "typedef struct{\n";
    out << "    const char *name;\n";
    out << "    const unsigned char * const data;\n";
    out << "    const int size;\n";
    out << "}RES_Resource;\n";

    out << "#define RES_DataSize " << files.size() << "\n";
    out << "const RES_Resource RES_Data[RES_DataSize]={\n";

    for ( size_t i = 0; i < files.size(); ++i ) {
        out << "    {\""
            << files[i].relativePath << "\","
            << "(const unsigned char*)" << files[i].arrayName << ","
            << files[i].fileSize
            << "}";

        if ( i < files.size() - 1 ) {
            out << ",\n";
        }
    }

    out << "\n};\n\n";
    out << "#endif\n";

    std::cout << "Successfully generated: " << outputPath << std::endl;
    std::cout << "Total files processed: " << files.size() << std::endl;

    return true;
}

int main( int argc, char *argv[] ) {
    if ( generateResourceHeader( INPUT_DIR, OUTPUT_NAME ) )
        return 0;
    return 1;
}