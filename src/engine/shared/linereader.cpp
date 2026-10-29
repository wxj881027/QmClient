/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "linereader.h"

#include <base/mem.h>
#include <base/str.h>
#include <base/system.h>

#include <cstdlib>

static bool IsValidLine(const char *pLine)
{
	if(!str_utf8_check(pLine))
	{
		// Skip lines containing invalid UTF-8
		return false;
	}

	// Skip lines containing any control character except `\t`
	for(size_t Index = 0; pLine[Index] != '\0'; ++Index)
	{
		if((unsigned char)pLine[Index] < ' ' && pLine[Index] != '\t')
		{
			return false;
		}
	}

	return true;
}

// UTF-16 按码点转成 UTF-8。无法配对的代理项写成替换字符，避免整份脚本被当成二进制拒绝。
static char *DecodeUtf16(const unsigned char *pData, unsigned Length, bool BigEndian)
{
	// 每 2 字节最多产生 3 字节 UTF-8。
	const unsigned Capacity = Length / 2 * 3 + 1;
	auto *pBuffer = static_cast<char *>(malloc(Capacity));
	if(pBuffer == nullptr)
		return nullptr;

	const auto ReadUnit = [pData, BigEndian](unsigned Index) {
		return BigEndian ? (unsigned)(pData[Index] << 8 | pData[Index + 1]) : (unsigned)(pData[Index + 1] << 8 | pData[Index]);
	};

	unsigned Out = 0;
	for(unsigned Index = 0; Index + 1 < Length; Index += 2)
	{
		const unsigned Unit = ReadUnit(Index);
		unsigned CodePoint = Unit;
		if(Unit >= 0xD800 && Unit <= 0xDBFF)
		{
			const bool HasNext = Index + 3 < Length;
			const unsigned Next = HasNext ? ReadUnit(Index + 2) : 0;
			if(HasNext && Next >= 0xDC00 && Next <= 0xDFFF)
			{
				CodePoint = 0x10000 + ((Unit - 0xD800) << 10) + (Next - 0xDC00);
				Index += 2;
			}
			else
			{
				CodePoint = 0xFFFD;
			}
		}
		else if(Unit >= 0xDC00 && Unit <= 0xDFFF)
		{
			CodePoint = 0xFFFD;
		}
		// 文本中的 NUL 会截断行缓冲，统一换成替换字符。
		if(CodePoint == 0)
			CodePoint = 0xFFFD;
		Out += str_utf8_encode(&pBuffer[Out], (int)CodePoint);
	}
	pBuffer[Out] = '\0';
	return pBuffer;
}

// 无 BOM 的 UTF-16 靠 NUL 分布识别：ASCII 字符在 UTF-16 中固定带一个 0 字节。
// 要求所有 NUL 落在同一位序且占比够高，避免把含零星 NUL 的二进制文件误判成 UTF-16。
static bool LooksLikeUtf16(const unsigned char *pData, unsigned Length, bool *pBigEndian)
{
	unsigned Nulls = 0;
	unsigned EvenNulls = 0;
	for(unsigned Index = 0; Index < Length; ++Index)
	{
		if(pData[Index] != 0)
			continue;
		++Nulls;
		if(Index % 2 == 0)
			++EvenNulls;
	}
	if(Length < 2 || Nulls < Length / 4)
		return false;
	if(EvenNulls == 0)
	{
		// NUL 全在奇数字节：小端 UTF-16。
		*pBigEndian = false;
		return true;
	}
	if(EvenNulls == Nulls)
	{
		// NUL 全在偶数字节：大端 UTF-16。
		*pBigEndian = true;
		return true;
	}
	return false;
}

CLineReader::CLineReader()
{
	m_pBuffer = nullptr;
}

CLineReader::~CLineReader()
{
	free(m_pBuffer);
}

bool CLineReader::IsValidBuffer(const char *pBuffer)
{
	if(!str_utf8_check(pBuffer))
		return false;
	for(size_t Index = 0; pBuffer[Index] != '\0'; ++Index)
	{
		const unsigned char Character = pBuffer[Index];
		if(Character == '\r' && pBuffer[Index + 1] == '\n')
			continue;
		if(Character < ' ' && Character != '\t' && Character != '\n')
			return false;
	}
	return true;
}

bool CLineReader::OpenFile(IOHANDLE File, bool RejectInvalidLines)
{
	return OpenFileWithBudget(File, 0, RejectInvalidLines);
}

bool CLineReader::OpenFileWithBudget(IOHANDLE File, unsigned MaxBufferBytes, bool RejectInvalidLines)
{
	const auto Fail = [this](EOpenResult Result) {
		m_LastOpenResult = Result;
		return false;
	};

	if(!File)
	{
		return Fail(EOpenResult::CANNOT_READ);
	}
	void *pRaw = nullptr;
	unsigned RawLength = 0;
	const SIOReadAllAllocator BoundedAllocator{
		[](void *pBuffer, size_t Size, void *pUser) -> void * {
			return Size <= *static_cast<unsigned *>(pUser) ? realloc(pBuffer, Size) : nullptr;
		},
		[](void *pBuffer, void *) { free(pBuffer); },
		&MaxBufferBytes};
	const bool Read = MaxBufferBytes ? io_read_all_with_allocator(File, &pRaw, &RawLength, BoundedAllocator) : io_read_all(File, &pRaw, &RawLength);
	io_close(File);
	if(!Read)
	{
		return Fail(EOpenResult::CANNOT_READ);
	}

	const auto *pData = static_cast<const unsigned char *>(pRaw);
	char *pBuffer = nullptr;
	if(RawLength >= 4 && ((pData[0] == 0xFF && pData[1] == 0xFE && pData[2] == 0x00 && pData[3] == 0x00) ||
				     (pData[0] == 0x00 && pData[1] == 0x00 && pData[2] == 0xFE && pData[3] == 0xFF)))
	{
		// UTF-32 脚本不常见，明确拒绝，避免按 UTF-16 解出乱码命令。
		free(pRaw);
		return Fail(EOpenResult::UNSUPPORTED_ENCODING);
	}
	else if(RawLength >= 2 && pData[0] == 0xFF && pData[1] == 0xFE)
	{
		pBuffer = DecodeUtf16(pData + 2, RawLength - 2, false);
	}
	else if(RawLength >= 2 && pData[0] == 0xFE && pData[1] == 0xFF)
	{
		pBuffer = DecodeUtf16(pData + 2, RawLength - 2, true);
	}
	else
	{
		bool BigEndian = false;
		if(LooksLikeUtf16(pData, RawLength, &BigEndian))
			pBuffer = DecodeUtf16(pData, RawLength, BigEndian);
		else if(mem_has_null(pData, RawLength))
		{
			free(pRaw);
			return Fail(EOpenResult::UNSUPPORTED_ENCODING);
		}
		else
		{
			// 已经是文本：直接接管原始缓冲，保留原有的 UTF-8 BOM 处理。
			pBuffer = static_cast<char *>(pRaw);
			pRaw = nullptr;
		}
	}
	free(pRaw);
	if(pBuffer == nullptr)
	{
		return Fail(EOpenResult::CANNOT_READ);
	}
	if(RejectInvalidLines && !IsValidBuffer(pBuffer))
	{
		free(pBuffer);
		return Fail(EOpenResult::INVALID_CONTENT);
	}
	OpenBuffer(pBuffer);
	m_LastOpenResult = EOpenResult::OK;
	return true;
}

void CLineReader::OpenBuffer(char *pBuffer)
{
	dbg_assert(pBuffer != nullptr, "Line reader initialized without valid buffer");

	m_pBuffer = pBuffer;
	m_BufferPos = 0;
	m_ReadLastLine = false;

	// Skip UTF-8 BOM
	if(m_pBuffer[0] == '\xEF' && m_pBuffer[1] == '\xBB' && m_pBuffer[2] == '\xBF')
	{
		m_BufferPos += 3;
	}
}

const char *CLineReader::Get()
{
	dbg_assert(m_pBuffer != nullptr, "Line reader not initialized");
	if(m_ReadLastLine)
	{
		return nullptr;
	}

	unsigned LineStart = m_BufferPos;
	while(true)
	{
		if(m_pBuffer[m_BufferPos] == '\0' || m_pBuffer[m_BufferPos] == '\n' || (m_pBuffer[m_BufferPos] == '\r' && m_pBuffer[m_BufferPos + 1] == '\n'))
		{
			if(m_pBuffer[m_BufferPos] == '\0')
			{
				m_ReadLastLine = true;
			}
			else
			{
				if(m_pBuffer[m_BufferPos] == '\r')
				{
					m_pBuffer[m_BufferPos] = '\0';
					++m_BufferPos;
				}
				m_pBuffer[m_BufferPos] = '\0';
				++m_BufferPos;
			}

			if(!IsValidLine(&m_pBuffer[LineStart]))
			{
				if(m_ReadLastLine)
				{
					return nullptr;
				}
				LineStart = m_BufferPos;
				continue;
			}
			// Skip trailing empty line
			if(m_ReadLastLine && m_pBuffer[LineStart] == '\0')
			{
				return nullptr;
			}
			return &m_pBuffer[LineStart];
		}
		++m_BufferPos;
	}
}
