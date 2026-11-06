#include <engine/shared/ringbuffer.h>

#include <game/client/components/qmclient/colored_parts.h>
#include <game/client/components/qmclient/console_search_results.h>

#include <gtest/gtest.h>

TEST(QmConsoleSearchPipeline, ParsedUtf8TextPublishesCaseInsensitiveByteMatches)
{
	QmConsoleSearch::CResults<64> Results;
	const auto Status = Results.Prepare("hit", [](auto &Candidate) {
		const auto Text = CColoredParts::ParseOrBorrow("中文 [[red]]hit HIT", true);
		if(Text.ParseFailed())
			return false;
		std::vector<QmConsoleText::SRange> vRanges;
		if(!QmConsoleSearch::TryCollect(Text.Text(), "hit", vRanges))
			return false;
		for(const auto &Range : vRanges)
			if(!Candidate.Append(Range.m_StartByte, Range.m_EndByte - Range.m_StartByte, 0, 0, 0))
				return false;
		return true;
	});
	ASSERT_EQ(Status, decltype(Results)::EUpdate::CHANGED_QUERY);
	ASSERT_EQ(Results.Matches().size(), 2u);
	EXPECT_EQ(Results.Matches()[0].m_Pos, 7);
	EXPECT_EQ(Results.Matches()[1].m_Pos, 11);
	EXPECT_EQ(Results.Matches()[0].m_Length, 3);
	EXPECT_TRUE(Results.Ready());
}

TEST(QmConsoleSearchPipeline, RecyclingOneLogForOneLogInvalidatesResultsWithoutChangingRowCount)
{
	struct SLog
	{
		int m_Lines;
		char m_aText[512];
	};
	CStaticRingBuffer<SLog, 1024, CRingBufferBase::FLAG_RECYCLE> Backlog;
	QmConsoleSearch::CResults<64> Results;
	int Rows = 0, Pops = 0;
	Backlog.SetPopCallback([&](SLog *pLog) {
		Rows -= pLog->m_Lines;
		++Pops;
		Results.Invalidate();
	});
	const auto Prepare = [&] {
		return Results.Prepare("old", [&](auto &Candidate) {
			for(SLog *pLog = Backlog.Last(); pLog; pLog = Backlog.Prev(pLog))
			{
				std::vector<QmConsoleText::SRange> vRanges;
				if(!QmConsoleSearch::TryCollect(pLog->m_aText, "old", vRanges))
					return false;
				for(const auto &Range : vRanges)
					if(!Candidate.Append(Range.m_StartByte, Range.m_EndByte - Range.m_StartByte, 0, 0, 0))
						return false;
			}
			return true;
		});
	};
	SLog *pFirst = Backlog.Allocate(sizeof(SLog));
	ASSERT_NE(pFirst, nullptr);
	pFirst->m_Lines = 1;
	str_copy(pFirst->m_aText, "old");
	++Rows;
	ASSERT_EQ(Prepare(), decltype(Results)::EUpdate::CHANGED_QUERY);
	ASSERT_EQ(Results.Matches().size(), 1u);
	SLog *pSecond = Backlog.Allocate(sizeof(SLog));
	ASSERT_NE(pSecond, nullptr);
	pSecond->m_Lines = 1;
	str_copy(pSecond->m_aText, "new");
	++Rows;
	Results.Invalidate();
	EXPECT_EQ(Pops, 1);
	EXPECT_EQ(Rows, 1);
	EXPECT_TRUE(Results.Pending());
	EXPECT_EQ(Results.CurrentMatch(), nullptr);
	ASSERT_EQ(Prepare(), decltype(Results)::EUpdate::SAME_QUERY);
	EXPECT_TRUE(Results.Ready());
	EXPECT_TRUE(Results.Matches().empty());
}
