#if !defined(CONF_OPENSSL)

#include "hash_ctxt.h"

#include <engine/external/md5/md5.h>

#include <algorithm>
#include <limits>

void md5_update(MD5_CTX *ctxt, const void *data, size_t data_len)
{
	// 内置实现用 int 表示字节数并左移计算位数。
	constexpr size_t MAX_CHUNK_SIZE = std::numeric_limits<int>::max() / 8;
	auto *pCurrent = static_cast<const md5_byte_t *>(data);
	while(data_len != 0)
	{
		const size_t chunk_size = std::min(data_len, MAX_CHUNK_SIZE);
		md5_append(ctxt, pCurrent, static_cast<int>(chunk_size));
		pCurrent += chunk_size;
		data_len -= chunk_size;
	}
}

MD5_DIGEST md5_finish(MD5_CTX *ctxt)
{
	MD5_DIGEST result;
	md5_finish_(ctxt, result.data);
	return result;
}

#endif
