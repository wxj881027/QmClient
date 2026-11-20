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
	CLineReader(const CLineReader &) = delete;
	CLineReader &operator=(const CLineReader &) = delete;
	~CLineReader();

	// 接管并关闭非空文件句柄。识别 UTF-8 与 UTF-16（BOM 或 NUL 分布），统一转成 UTF-8。
	bool OpenFile(IOHANDLE File, bool RejectInvalidLines = false);
	// 同样接管文件句柄。MaxBufferBytes 包含原始缓冲的末尾 NUL，0 沿用既有限制。
	// UTF-16 的解码输出另行分配，最多为原始输入长度的 1.5 倍加一个 NUL。
	bool OpenFileWithBudget(IOHANDLE File, unsigned MaxBufferBytes, bool RejectInvalidLines = false);
	// 接管 malloc 分配的可写缓冲，失败时也释放。RawLength 处须保留一个额外的 NUL 字节。
	bool OpenTextBuffer(void *pRaw, unsigned RawLength, bool RejectInvalidLines);
	// 返回最近一次 OpenFile、OpenFileWithBudget 或 OpenTextBuffer 的结果。
	EOpenResult LastOpenResult() const { return m_LastOpenResult; }
	// 校验解码后、以 NUL 结尾的 UTF-8 文本，供严格模式在读取任何行之前使用。
	static bool IsValidBuffer(const char *pBuffer);
	// 接管 malloc 分配、以 NUL 结尾的可写 UTF-8 缓冲。
	void OpenBuffer(char *pBuffer);

	// 成功打开后可调用。返回值借用内部缓冲，有效期到下一次成功打开或析构为止。
	const char *Get();
};
#endif
