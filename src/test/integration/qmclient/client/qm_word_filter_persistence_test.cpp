#include <base/str.h>

#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/client/components/qmclient/word_filter_list.h>

#include <gtest/gtest.h>

#include <array>

namespace
{

	struct SInlineList
	{
		std::array<char, QmWordFilterList::INLINE_CAPACITY> m_aText{};
		bool m_AppendFailed = false;
	};

	void SetList(IConsole::IResult *pResult, void *pUser)
	{
		auto &List = *static_cast<SInlineList *>(pUser);
		str_copy(List.m_aText.data(), pResult->GetString(0), List.m_aText.size());
	}

	void AppendList(IConsole::IResult *pResult, void *pUser)
	{
		auto &List = *static_cast<SInlineList *>(pUser);
		List.m_AppendFailed |= !QmWordFilterList::AppendInlineHexChunk(List.m_aText.data(), List.m_aText.size(), pResult->GetString(0));
	}

	void RoundTrip(const std::string &Original)
	{
		auto pConsole = CreateConsole(CFGFLAG_CLIENT);
		SInlineList List;
		pConsole->Register("qm_block_words_list", "?r", CFGFLAG_CLIENT, SetList, &List, "");
		pConsole->Register("qm_block_words_list_append", "s", CFGFLAG_CLIENT, AppendList, &List, "");
		std::vector<std::string> vLines;
		ASSERT_TRUE(QmWordFilterList::SerializeInline(Original.c_str(), vLines));
		for(const std::string &Line : vLines)
		{
			EXPECT_LT(Line.size(), 16384u);
			pConsole->ExecuteLine(Line.c_str());
		}
		EXPECT_FALSE(List.m_AppendFailed);
		EXPECT_EQ(std::string(List.m_aText.data()), Original);
	}

}

TEST(QmWordFilterPersistence, RestoresLargeUtf8ListsThroughTheRealConsoleParser)
{
	std::string Original;
	while(Original.size() + 10 < QmWordFilterList::INLINE_CAPACITY)
		Original += "测试词,";
	Original.append(QmWordFilterList::INLINE_CAPACITY - 1 - Original.size(), 'a');
	RoundTrip(Original);
}

TEST(QmWordFilterPersistence, PreservesQuotesBackslashesAndNewlines)
{
	RoundTrip("a\\d+,a\"b,;quit\n下一行\r\n最后一行");
	RoundTrip("small\\list,\"quoted\",;quit");
	RoundTrip("");
}
