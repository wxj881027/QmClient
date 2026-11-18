#ifndef BASE_BYTES_H
#define BASE_BYTES_H

/**
 * 字节序与整数打包工具。
 *
 * @defgroup Bytes Bytes
 */

/**
 * 按元素反转字节序。
 *
 * @ingroup Bytes
 *
 * @param data 待转换数据。
 * @param elem_size 单个元素字节数。
 * @param num 元素数量。
 *
 * @remark 调用方必须保证数据至少为 `elem_size * num` 字节。
 */
void swap_endian(void *data, unsigned elem_size, unsigned num);

/**
 * 将 4 个大端字节打包为无符号整数。
 *
 * @ingroup Bytes
 *
 * @param bytes 至少 4 字节的输入数组。
 *
 * @return 打包后的无符号整数。
 *
 * @see uint_to_bytes_be
 */
unsigned bytes_be_to_uint(const unsigned char *bytes);

/**
 * 将无符号整数打包为 4 个大端字节。
 *
 * @ingroup Bytes
 *
 * @param bytes 至少 4 字节的输出数组。
 * @param value 待打包的无符号整数。
 *
 * @see bytes_be_to_uint
 */
void uint_to_bytes_be(unsigned char *bytes, unsigned value);

#endif
