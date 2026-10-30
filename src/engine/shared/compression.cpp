/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "compression.h"

#include <base/system.h>

#include <iterator> // std::size

const unsigned char *CVariableInt::Unpack(const unsigned char *pSrc, int *pInOut, int SrcSize)
{
	if(SrcSize <= 0)
		return nullptr;

	const int Sign = (*pSrc >> 6) & 1;
	*pInOut = *pSrc & 0x3F;
	SrcSize--;

	static const int s_aMasks[] = {0x7F, 0x7F, 0x7F, 0x0F};
	static const int s_aShifts[] = {6, 6 + 7, 6 + 7 + 7, 6 + 7 + 7 + 7};

	for(unsigned i = 0; i < std::size(s_aMasks); i++)
	{
		if(!(*pSrc & 0x80))
			break;
		if(SrcSize <= 0)
			return nullptr;
		SrcSize--;
		pSrc++;
		*pInOut |= (*pSrc & s_aMasks[i]) << s_aShifts[i];
	}

	pSrc++;
	*pInOut ^= -Sign; // if(sign) *i = ~(*i)
	return pSrc;
}

long CVariableInt::Decompress(const void *pSrc, int SrcSize, void *pDst, int DstSize)
{
	if(SrcSize < 0 || DstSize < 0 || DstSize % sizeof(int) != 0 ||
		(SrcSize > 0 && !pSrc) || (DstSize > 0 && !pDst))
		return -1;

	const auto *pCharSrc = (const unsigned char *)pSrc;
	auto *pCharDst = (unsigned char *)pDst;
	int InputSize = 0;
	int OutputSize = 0;
	while(InputSize < SrcSize)
	{
		if(DstSize - OutputSize < (int)sizeof(int))
			return -1;
		int Value;
		const unsigned char *pNext = CVariableInt::Unpack(pCharSrc + InputSize, &Value, SrcSize - InputSize);
		if(!pNext)
			return -1;
		InputSize = pNext - pCharSrc;
		// 接口接收字节缓冲，不能依赖调用方的int对齐。
		mem_copy(pCharDst + OutputSize, &Value, sizeof(Value));
		OutputSize += sizeof(Value);
	}
	return OutputSize;
}

long CVariableInt::Compress(const void *pSrc, int SrcSize, void *pDst, int DstSize)
{
	if(SrcSize < 0 || DstSize < 0 || SrcSize % sizeof(int) != 0 ||
		(SrcSize > 0 && (!pSrc || DstSize == 0)) || (DstSize > 0 && !pDst))
		return -1;

	const auto *pCharSrc = (const unsigned char *)pSrc;
	auto *pCharDst = (unsigned char *)pDst;
	int OutputSize = 0;
	for(int InputSize = 0; InputSize < SrcSize; InputSize += sizeof(int))
	{
		int Value;
		mem_copy(&Value, pCharSrc + InputSize, sizeof(Value));
		unsigned char *pNext = CVariableInt::Pack(pCharDst + OutputSize, Value, DstSize - OutputSize);
		if(!pNext)
			return -1;
		OutputSize = pNext - pCharDst;
	}
	return OutputSize;
}
