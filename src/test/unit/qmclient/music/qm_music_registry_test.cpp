#include <game/client/components/qmclient/qm_music_hook_registry.h>

#include <gtest/gtest.h>

TEST(QmMusicRegistry, KugouAndQQMusicHaveIndependentSources)
{
	size_t Count = 0;
	const SQmMusicHookEntry *pEntries = QmMusicHookRegistry(&Count);
	const uint64_t Kugou = QmMusicHookMaskForProcess(L"kugou.exe", pEntries, Count);
	const uint64_t QQMusic = QmMusicHookMaskForProcess(L"QQMusic.exe", pEntries, Count);
	EXPECT_NE(Kugou, uint64_t(0));
	EXPECT_NE(QQMusic, uint64_t(0));
	EXPECT_EQ(Kugou & QQMusic, uint64_t(0));
	EXPECT_EQ(QmMusicHookMaskForProcess(L"KUGOU.EXE", pEntries, Count), Kugou);
	EXPECT_EQ(QmMusicHookMaskForProcess(L"QQMusicHelper.exe", pEntries, Count), uint64_t(0));
	EXPECT_EQ(QmMusicHookMaskForProcess(L"KuGou.exe.bak", pEntries, Count), uint64_t(0));
}

TEST(QmMusicRegistry, NewSourcesUseTheirOwnSavedSettings)
{
	size_t Count = 0;
	const SQmMusicHookEntry *pEntries = QmMusicHookRegistry(&Count);
	bool FoundKugou = false;
	bool FoundQQMusic = false;
	for(size_t Index = 0; Index < Count; ++Index)
	{
		const uint64_t Mask = uint64_t(1) << Index;
		if(QmMusicHookMaskForProcess(L"KuGou.exe", pEntries, Count) == Mask)
		{
			EXPECT_EQ(pEntries[Index].m_pEnableConfig, &g_Config.m_QmKugouHookEnable);
			FoundKugou = true;
		}
		if(QmMusicHookMaskForProcess(L"QQMusic.exe", pEntries, Count) == Mask)
		{
			EXPECT_EQ(pEntries[Index].m_pEnableConfig, &g_Config.m_QmQQMusicHookEnable);
			FoundQQMusic = true;
		}
	}
	EXPECT_TRUE(FoundKugou);
	EXPECT_TRUE(FoundQQMusic);
}
