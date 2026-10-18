// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef ENGINE_CLIENT_FONT_SIZE_CACHE_H
#define ENGINE_CLIENT_FONT_SIZE_CACHE_H

// 只记住最近一次成功的字号设置；所有绕过此入口的修改必须先失效。
class CQmFontSizeCache
{
	const void *m_pFace = nullptr;
	int m_Size = -1;

public:
	void Reset()
	{
		m_pFace = nullptr;
		m_Size = -1;
	}

	template<typename TSetSize>
	bool Ensure(const void *pFace, int Size, TSetSize &&SetSize)
	{
		if(pFace == nullptr)
			return false;
		if(pFace == m_pFace && Size == m_Size)
			return true;
		// 失败可能改变底层状态，不能保留之前的成功记录。
		Reset();
		if(SetSize() == 0)
		{
			m_pFace = pFace;
			m_Size = Size;
			return true;
		}
		return false;
	}
};

#endif
