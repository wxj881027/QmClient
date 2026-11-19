#include "bytes.h"

#include <algorithm>

void swap_endian(void *data, unsigned elem_size, unsigned num)
{
	if(elem_size <= 1 || num == 0)
		return;

	auto *pBegin = static_cast<char *>(data);
	for(unsigned i = 0; i < num; ++i)
	{
		auto *pEnd = pBegin + elem_size;
		std::reverse(pBegin, pEnd);
		pBegin = pEnd;
	}
}

static_assert(sizeof(unsigned) == 4, "unsigned must be 4 bytes in size");
static_assert(sizeof(unsigned) == sizeof(int), "unsigned and int must have the same size");

unsigned bytes_be_to_uint(const unsigned char *bytes)
{
	return ((bytes[0] & 0xffu) << 24u) | ((bytes[1] & 0xffu) << 16u) | ((bytes[2] & 0xffu) << 8u) | (bytes[3] & 0xffu);
}

void uint_to_bytes_be(unsigned char *bytes, unsigned value)
{
	bytes[0] = (value >> 24u) & 0xffu;
	bytes[1] = (value >> 16u) & 0xffu;
	bytes[2] = (value >> 8u) & 0xffu;
	bytes[3] = value & 0xffu;
}
