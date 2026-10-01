#include <gtest/gtest.h>
#include <qm/music/qm_kugou_protocol.h>

#include <cstring>

using namespace QmMusicHook;

namespace
{
	auto PatchReader(bool Patched, int CorruptIndex = -1)
	{
		return [Patched, CorruptIndex](uint64_t Offset, void *pOut, size_t Size) {
			const auto &Patches = KugouPatches();
			for(size_t Index = 0; Index < Patches.size(); ++Index)
			{
				const auto &Patch = Patches[Index];
				if(Patch.m_Offset != Offset || Patch.m_Original.size() != Size)
					continue;
				std::memcpy(pOut, (Patched ? Patch.m_Patched : Patch.m_Original).data(), Size);
				if((int)Index == CorruptIndex)
					static_cast<char *>(pOut)[0] ^= 0x7f;
				return true;
			}
			return false;
		};
	}
}

TEST(QmKugouPatch, RequiresAllNineKnownFingerprints)
{
	ASSERT_EQ(KugouPatches().size(), 9u);
	EXPECT_EQ(ClassifyKugouPatch(PatchReader(false)), EKugouPatchState::ORIGINAL);
	EXPECT_EQ(ClassifyKugouPatch(PatchReader(true)), EKugouPatchState::PATCHED);
	for(size_t Index = 0; Index < KugouPatches().size(); ++Index)
		EXPECT_EQ(ClassifyKugouPatch(PatchReader(false, (int)Index)), EKugouPatchState::UNSUPPORTED);
	EXPECT_EQ(ClassifyKugouPatch([](uint64_t, void *, size_t) { return false; }), EKugouPatchState::UNSUPPORTED);
}

TEST(QmKugouPatch, RejectsPartialPatchWithoutPristineBackup)
{
	const auto ReadOriginal = PatchReader(false);
	const auto ReadPatched = PatchReader(true);
	EXPECT_EQ(ClassifyKugouPatch([&](uint64_t Offset, void *pOut, size_t Size) {
		return Offset == KugouPatches()[0].m_Offset ? ReadPatched(Offset, pOut, Size) : ReadOriginal(Offset, pOut, Size);
	}),
		EKugouPatchState::UNSUPPORTED);
}

TEST(QmKugouPatch, RestoreRefusesUpgradedDllEvenIfPatchOffsetsStillMatch)
{
	EXPECT_TRUE(IsKugouRestoreChunk(0, "same bytes", "same bytes"));
	EXPECT_FALSE(IsKugouRestoreChunk(0, "old bytes", "new bytes"));
	EXPECT_FALSE(IsKugouRestoreChunk(0, "short", "different size"));
	for(const auto &Patch : KugouPatches())
	{
		EXPECT_TRUE(IsKugouRestoreChunk(Patch.m_Offset, Patch.m_Original, Patch.m_Patched));
		EXPECT_TRUE(IsKugouRestoreChunk(Patch.m_Offset + 1, Patch.m_Original.substr(1), Patch.m_Patched.substr(1)));
		EXPECT_FALSE(IsKugouRestoreChunk(Patch.m_Offset, Patch.m_Patched, Patch.m_Patched));
	}
}

TEST(QmKugouPatch, OriginalBackupAllowsRecoveryAfterInterruptedPatch)
{
	const auto &Patch = KugouPatches().front();
	std::string Partial = Patch.m_Original;
	Partial[2] = Patch.m_Patched[2];
	EXPECT_FALSE(IsKugouRestoreChunk(Patch.m_Offset, Patch.m_Original, Partial));
	EXPECT_TRUE(IsKugouRestoreChunk(Patch.m_Offset, Patch.m_Original, Partial, true));
	Partial[2] = '\x42';
	EXPECT_FALSE(IsKugouRestoreChunk(Patch.m_Offset, Patch.m_Original, Partial, true));
	EXPECT_FALSE(IsKugouRestoreChunk(0, "old bytes", "new bytes", true));
}
