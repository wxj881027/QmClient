#include <engine/client/checksum.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>

TEST(Checksum, GeneratedFileHashesStayInsideInlineStorage)
{
	struct SGuardedChecksum
	{
		CChecksumData m_Data{};
		std::array<unsigned, 1024> m_aGuard{};
	};
	auto pGuarded = std::make_unique<SGuardedChecksum>();
	pGuarded->m_aGuard.fill(0x5a5a5a5a);
	const auto ExpectedGuard = pGuarded->m_aGuard;

	pGuarded->m_Data.InitFiles();

	EXPECT_GE(pGuarded->m_Data.m_NumFiles, 0);
	EXPECT_GE(pGuarded->m_Data.m_NumExtra, 0);
	EXPECT_EQ(pGuarded->m_aGuard, ExpectedGuard);
}
