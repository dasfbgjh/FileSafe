#ifndef RESOURCE_H
#define RESOURCE_H
  
/**
 * @brief 获取资源数量
 *
 * @return int 资源数量
 */
int resource_count();
 
/**
 * @brief 获取资源索引
 *
 * @param name 资源名称
 * @return int 资源索引,不存在返回-1
 */
int resource_index( const char *name );

/**
 * @brief 获取资源名称
 *
 * @param index 资源索引
 * @return const char* 资源名称
 */
const char *resource_name( int index );

/**
 * @brief 获取资源大小
 *
 * @param index 资源索引
 * @return int 资源大小
 */
int resource_size( int index );

/**
 * @brief 获取资源数据
 *
 * @param index 资源索引
 * @return const unsigned char* 资源数据指针
 */
const unsigned char *resource_data( int index );

/**
 * @brief 获取资源数据
 *
 * @param name 资源名称
 * @param data 资源数据指针
 * @return int 资源数据大小,失败返回-1
 */
int resource_get( const char *name, const unsigned char **data );

/**
 * @brief 检查资源是否存在
 *
 * @param name 资源名称
 * @return int 存在返回1,不存在返回0
 */
int resource_exists( const char *name );

#endif