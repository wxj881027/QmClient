/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_LINEREADER_H
#define ENGINE_SHARED_LINEREADER_H
#include <base/types.h>

// buffered stream for reading lines
class CLineReader
{
public:
	// 调用方需要区分“文件打不开”和“文件能读但不是支持的文本”，才能给出准确提示。
	enum class EOpenResult
	{
		OK,
		CANNOT_READ,
		UNSUPPORTED_ENCODING,
		INVALID_CONTENT,
	};

private:
	char *m_pBuffer;
	unsigned m_BufferPos;
	bool m_ReadLastLine;
	EOpenResult m_LastOpenResult = EOpenResult::OK;

public:
	CLineReader();
	~CLineReader();

	// 识别 UTF-8 与 UTF-16（BOM 或 NUL 分布）并统一转成 UTF-8 后按行读取。
	bool OpenFile(IOHANDLE File, bool RejectInvalidLines = false);
	// 限制原始读取缓冲（含NUL），0沿用既有限制；解码另有最多1.5倍缓冲。
	bool OpenFileWithBudget(IOHANDLE File, unsigned MaxBufferBytes, bool RejectInvalidLines = false);
	// 最近一次 OpenFile 的结果，失败原因据此区分，不改变 OpenFile 的布尔语义。
	EOpenResult LastOpenResult() const { return m_LastOpenResult; }
	// 配置加载先校验整份文本，不能执行一部分后才发现被跳过的坏行。
	static bool IsValidBuffer(const char *pBuffer);
	void OpenBuffer(char *pBuffer); // Buffer must have been allocated with malloc, will be freed by the line reader

	const char *Get(); // Returned string is valid until the line reader is destroyed
};
#endif
