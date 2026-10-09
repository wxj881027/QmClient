/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "linereader.h"

#include <base/system.h>

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
	if(!File)
	{
		return false;
	}
	char *pBuffer = io_read_all_str(File);
	io_close(File);
	if(pBuffer == nullptr)
	{
		return false;
	}
	if(RejectInvalidLines && !IsValidBuffer(pBuffer))
	{
		free(pBuffer);
		return false;
	}
	OpenBuffer(pBuffer);
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
