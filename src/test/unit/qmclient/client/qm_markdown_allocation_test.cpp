#include <game/client/components/qmclient/qm_markdown.h>

#include <gtest/gtest.h>
#include <test/support/prediction_allocation_failure.h>

#include <string>

TEST(QmMarkdownAllocation, FailedInputCopyKeepsPreviousResultAndAllowsRetry)
{
	const std::string Markdown(1024, 'x');
	auto vBlocks = qm_md::Parse("previous");
	const auto *pStorage = vBlocks.data();
	bool Success, Triggered;
	{
		CPredictionAllocationFailure Fault;
		Success = qm_md::TryParse(Markdown.c_str(), vBlocks);
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_FALSE(Success);
	ASSERT_EQ(vBlocks.size(), 1u);
	EXPECT_EQ(vBlocks.data(), pStorage);
	EXPECT_EQ(vBlocks[0].m_vSpans[0].m_Text, "previous");
	ASSERT_TRUE(qm_md::TryParse(Markdown.c_str(), vBlocks));
	ASSERT_EQ(vBlocks.size(), 1u);
	EXPECT_EQ(vBlocks[0].m_vSpans[0].m_Text, Markdown);
}

TEST(QmMarkdownAllocation, PreparationFailuresNeverPublishPartialBlocks)
{
	const std::string Markdown = "# heading with sufficiently long text\n"
				     "[a link with a long label](https://example.test/long-link) "
				     "**bold text with a long label** [[settings:qm:lyrics|Open settings]]\n- tail";
	auto vBlocks = qm_md::Parse("previous");
	const auto *pStorage = vBlocks.data();
	size_t Failures = 0;
	bool Completed = false;
	for(size_t BeforeFailure = 0; BeforeFailure < 256; ++BeforeFailure)
	{
		SCOPED_TRACE(BeforeFailure);
		bool Success, Triggered;
		{
			CPredictionAllocationFailure Fault(BeforeFailure);
			Success = qm_md::TryParse(Markdown.c_str(), vBlocks);
			Triggered = Fault.Triggered();
		}
		if(!Triggered)
		{
			EXPECT_TRUE(Success);
			Completed = true;
			break;
		}
		++Failures;
		EXPECT_FALSE(Success);
		ASSERT_EQ(vBlocks.size(), 1u);
		EXPECT_EQ(vBlocks.data(), pStorage);
		EXPECT_EQ(vBlocks[0].m_vSpans[0].m_Text, "previous");
	}
	EXPECT_TRUE(Completed);
	EXPECT_GT(Failures, 1u);
	ASSERT_EQ(vBlocks.size(), 4u);
	EXPECT_EQ(vBlocks[0].m_Kind, qm_md::EBlockKind::HEADING1);
	EXPECT_EQ(vBlocks[2].m_SettingsCardId, "qm:lyrics");
	EXPECT_EQ(vBlocks[3].m_Kind, qm_md::EBlockKind::BULLET);
}

TEST(QmMarkdownAllocation, ConvenienceParseReturnsEmptyOnResourceFailure)
{
	const std::string Markdown(1024, 'x');
	std::vector<qm_md::SBlock> vBlocks;
	bool Triggered;
	{
		CPredictionAllocationFailure Fault;
		vBlocks = qm_md::Parse(Markdown.c_str());
		Triggered = Fault.Triggered();
	}
	EXPECT_TRUE(Triggered);
	EXPECT_TRUE(vBlocks.empty());
}
