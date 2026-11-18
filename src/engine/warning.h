#ifndef ENGINE_WARNING_H
#define ENGINE_WARNING_H

struct SWarning
{
	SWarning() = default;
	SWarning(const SWarning &Other);
	SWarning(const char *pMsg);
	SWarning(const char *pTitle, const char *pMsg);

	SWarning &operator=(const SWarning &Other);

	char m_aWarningTitle[128] = "";
	// 客户端退出时的保存失败提示用 1024 字节缓冲区生成。
	char m_aWarningMsg[1024] = "";
	bool m_AutoHide = true;
};

#endif
