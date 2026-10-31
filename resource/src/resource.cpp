#include "resource.h"
#include "string.h"
#include "data.h"

#ifndef RES_DataSize
#define RES_DataSize 0
typedef struct {
    const char *name;
    int size;
    const unsigned char *const data;
} RES_Resource;

const RES_Resource RES_Data[RES_DataSize] = {};
#warning "resource data not generated"
#endif

int resource_count() {
    return RES_DataSize;
}

int resource_index( const char *name ) {
    for ( int i = 0; i < RES_DataSize; i++ ) {
        if ( strcmp( RES_Data[i].name, name ) == 0 ) {
            return i;
        }
    }
    return -1;
}

const char *resource_name( int index ) {
    if ( index < 0 || index >= RES_DataSize ) {
        return nullptr;
    }
    return RES_Data[index].name;
}

int resource_size( int index ) {
    if ( index < 0 || index >= RES_DataSize ) {
        return -1;
    }
    return RES_Data[index].size;
}

const unsigned char *resource_data( int index ) {
    if ( index < 0 || index >= RES_DataSize ) {
        return nullptr;
    }
    return RES_Data[index].data;
}

int resource_get( const char *name, const unsigned char **data ) {
    int index = resource_index( name );
    if ( index < 0 )
        return -1;
    if ( data )
        *data = RES_Data[index].data;
    return RES_Data[index].size;
}

int resource_exists( const char *name ) {
    return resource_index( name ) >= 0;
}