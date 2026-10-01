// CNetObjHandler::SecureUnpackObj 返回内部共享暂存缓冲区：
// 后续再解包任何对象都会覆盖先前结果。rank ghost 曾因此在解包
// Character 后又解包 DDNetCharacter，导致 X/Y/HookState 等字段
// 被覆盖成 DDNetCharacter 的值（影子只剩钩链、Tee 位置错乱）。
// 本测试固定这一契约，防止调用方再次长期持有返回指针。
#include <base/system.h>

#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>

#include <generated/protocol.h>

#include <gtest/gtest.h>

namespace
{
	CNetObj_Character MakeCharacter()
	{
		CNetObj_Character Character{};
		Character.m_Tick = 6536;
		Character.m_X = 32276;
		Character.m_Y = 5155;
		Character.m_VelX = 1280;
		Character.m_Angle = 460;
		Character.m_Direction = 1;
		Character.m_HookState = 5;
		Character.m_HookX = 32276;
		Character.m_HookY = 5155;
		Character.m_Weapon = 0;
		Character.m_AttackTick = 32;
		return Character;
	}
} // namespace

TEST(CNetObjHandler, SecureUnpackObjReturnsSharedScratchBuffer)
{
	CNetObjHandler Handler;
	const CNetObj_Character Character = MakeCharacter();

	CUnpacker Unpacker;
	Unpacker.Reset(&Character, sizeof(Character));
	const auto *pUnpacked = static_cast<const CNetObj_Character *>(Handler.SecureUnpackObj(NETOBJTYPE_CHARACTER, &Unpacker));
	ASSERT_NE(pUnpacked, nullptr);
	EXPECT_EQ(pUnpacked->m_X, Character.m_X);
	EXPECT_EQ(pUnpacked->m_HookState, Character.m_HookState);

	CNetObj_DDNetCharacter DDNetCharacter{};
	DDNetCharacter.m_FreezeEnd = 123;
	DDNetCharacter.m_Jumps = 2;
	CUnpacker ExtUnpacker;
	ExtUnpacker.Reset(&DDNetCharacter, sizeof(DDNetCharacter));
	ASSERT_NE(Handler.SecureUnpackObj(NETOBJTYPE_DDNETCHARACTER, &ExtUnpacker), nullptr);

	// 同一暂存缓冲区被覆盖：DDNetCharacter 的 11 个 int 恰好落在
	// CNetObj_Character 的 Tick..TuneZoneOverride 区间上。
	EXPECT_EQ(pUnpacked->m_X, DDNetCharacter.m_FreezeEnd);
	EXPECT_EQ(pUnpacked->m_Y, DDNetCharacter.m_Jumps);
	EXPECT_NE(pUnpacked->m_X, Character.m_X);
}

TEST(CNetObjHandler, SecureUnpackObjCopyBeforeLaterUnpacksKeepsData)
{
	CNetObjHandler Handler;
	const CNetObj_Character Character = MakeCharacter();

	CUnpacker Unpacker;
	Unpacker.Reset(&Character, sizeof(Character));
	const auto *pUnpacked = static_cast<const CNetObj_Character *>(Handler.SecureUnpackObj(NETOBJTYPE_CHARACTER, &Unpacker));
	ASSERT_NE(pUnpacked, nullptr);

	// rank ghost 的修复模式：立即拷贝，再解包扩展对象。
	CNetObj_Character Copy;
	mem_copy(&Copy, pUnpacked, sizeof(Copy));

	CNetObj_DDNetCharacter DDNetCharacter{};
	DDNetCharacter.m_FreezeEnd = 123;
	DDNetCharacter.m_Jumps = 2;
	CUnpacker ExtUnpacker;
	ExtUnpacker.Reset(&DDNetCharacter, sizeof(DDNetCharacter));
	const auto *pExtUnpacked = static_cast<const CNetObj_DDNetCharacter *>(Handler.SecureUnpackObj(NETOBJTYPE_DDNETCHARACTER, &ExtUnpacker));
	ASSERT_NE(pExtUnpacked, nullptr);

	EXPECT_EQ(Copy.m_X, Character.m_X);
	EXPECT_EQ(Copy.m_Y, Character.m_Y);
	EXPECT_EQ(Copy.m_HookState, Character.m_HookState);
	EXPECT_EQ(Copy.m_AttackTick, Character.m_AttackTick);
	// 扩展数据本身仍可读（用于冻结忍者判定）。
	EXPECT_EQ(pExtUnpacked->m_FreezeEnd, DDNetCharacter.m_FreezeEnd);
}
