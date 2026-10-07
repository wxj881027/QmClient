// 字体家族与样式只忽略空格、连字符及 ASCII 大小写，保留所有其他字符。
#ifndef ENGINE_CLIENT_QM_FONT_NAME_MATCH_H
#define ENGINE_CLIENT_QM_FONT_NAME_MATCH_H
inline bool QmFontNamesEqual(const char *pLeft, const char *pRight)
{
	if(pLeft == nullptr || pRight == nullptr)
		return false;
	const auto Separator = [](char Chr) { return Chr == ' ' || Chr == '-'; };
	const auto Fold = [](unsigned char Chr) { return Chr >= 'A' && Chr <= 'Z' ? Chr + ('a' - 'A') : Chr; };
	while(true)
	{
		while(Separator(*pLeft))
			++pLeft;
		while(Separator(*pRight))
			++pRight;
		if(Fold(static_cast<unsigned char>(*pLeft)) != Fold(static_cast<unsigned char>(*pRight)))
			return false;
		if(*pLeft == 0)
			return true;
		++pLeft;
		++pRight;
	}
}
#endif
