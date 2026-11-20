/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_COMPRESSION_H
#define ENGINE_SHARED_COMPRESSION_H

// variable int packing
class CVariableInt
{
public:
	enum
	{
		MAX_BYTES_PACKED = 5, // maximum number of bytes in a packed int
	};

	static unsigned char *Pack(unsigned char *pDst, int i, int DstSize)
	{
		if(DstSize <= 0)
			return nullptr;

		DstSize--;
		*pDst = 0;
		if(i < 0)
		{
			*pDst |= 0x40;
			i = ~i;
		}

		*pDst |= i & 0x3F;
		i >>= 6;
		while(i)
		{
			if(DstSize <= 0)
				return nullptr;
			*pDst |= 0x80;
			DstSize--;
			pDst++;
			*pDst = i & 0x7F;
			i >>= 7;
		}

		pDst++;
		return pDst;
	}
	static const unsigned char *Unpack(const unsigned char *pSrc, int *pInOut, int SrcSize);

	// 批量接口接收不重叠的字节缓冲，无int对齐要求；长度无效或容量不足返回-1。
	// Compress的输入长度、Decompress的输出容量必须为sizeof(int)的倍数。
	// 失败时输出不可消费；成功返回写入字节数，空缓冲可使用nullptr。
	static long Compress(const void *pSrc, int SrcSize, void *pDst, int DstSize);
	static long Decompress(const void *pSrc, int SrcSize, void *pDst, int DstSize);
};

#endif
