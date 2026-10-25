#include <engine/shared/packer.h>

#include <generated/protocol.h>

#include <game/client/components/qmclient/spectator_selection.h>

#include <gtest/gtest.h>

#include <limits>

TEST(SpectatorSelectionInputs, SecureFreeviewIdIsNotAnArrayIndex)
{
	CNetObj_SpectatorInfo Input{};
	Input.m_SpectatorId = SPEC_FREEVIEW;
	CUnpacker Unpacker;
	Unpacker.Reset(&Input, sizeof(Input));
	CNetObjHandler Handler;
	const auto *pSecured = static_cast<const CNetObj_SpectatorInfo *>(Handler.SecureUnpackObj(NETOBJTYPE_SPECTATORINFO, &Unpacker));
	ASSERT_NE(pSecured, nullptr);
	ASSERT_EQ(pSecured->m_SpectatorId, SPEC_FREEVIEW);
	const bool aSelected[] = {true, true};
	EXPECT_TRUE(QmSpectatorSelection::NeedsRetarget(aSelected, pSecured->m_SpectatorId));
}

TEST(SpectatorSelectionInputs, SecureExtremeCharacterPositionsCanBeComparedWithoutIntDistance)
{
	CNetObj_Character Input{};
	Input.m_X = std::numeric_limits<int>::max();
	Input.m_Y = std::numeric_limits<int>::max();
	CUnpacker Unpacker;
	Unpacker.Reset(&Input, sizeof(Input));
	CNetObjHandler Handler;
	const auto *pSecured = static_cast<const CNetObj_Character *>(Handler.SecureUnpackObj(NETOBJTYPE_CHARACTER, &Unpacker));
	ASSERT_NE(pSecured, nullptr);
	QmSpectatorSelection::CClosest Closest(vec2(0.0f, 0.0f));
	Closest.Consider(1, vec2(pSecured->m_X, pSecured->m_Y));
	Closest.Consider(2, vec2(2000000000.0f, 2000000000.0f));
	EXPECT_EQ(Closest.SelectedId(), 2);
}
