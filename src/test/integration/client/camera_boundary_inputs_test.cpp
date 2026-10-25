#include <engine/console.h>
#include <engine/shared/config.h>
#include <engine/shared/packer.h>

#include <generated/protocol.h>

#include <game/client/components/camera.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
	struct SPositionArgument
	{
		ivec2 m_Position{};
		int m_Calls = 0;
	};

	void CapturePosition(IConsole::IResult *pResult, void *pUser)
	{
		auto &Argument = *static_cast<SPositionArgument *>(pUser);
		Argument.m_Position = ivec2(pResult->GetInteger(0), pResult->GetInteger(1));
		++Argument.m_Calls;
	}
}

TEST(CameraBoundaryInputs, ConsoleTilePositionUsesSmallAxisCenterAndNormalMargin)
{
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	SPositionArgument Argument;
	pConsole->Register("camera_position", "ii", CFGFLAG_CLIENT, CapturePosition, &Argument, "");
	pConsole->ExecuteLine("camera_position 1000 -1000");
	ASSERT_EQ(Argument.m_Calls, 1);
	EXPECT_EQ(QmCameraEffects::ViewTarget(Argument.m_Position, vec2(), false, 12, 100), vec2(192, 200));
}

TEST(CameraBoundaryInputs, QuantizedZoomKeepsValidSpectatorCameraObject)
{
	struct SCase
	{
		float m_Zoom;
		int m_Encoded;
	};
	const SCase aCases[] = {
		{1.234f, 1234},
		{2147483.75f, std::numeric_limits<int>::max()},
		{std::numeric_limits<float>::max(), std::numeric_limits<int>::max()},
	};
	for(const auto &Case : aCases)
	{
		SCOPED_TRACE(Case.m_Zoom);
		CNetObj_DDNetSpectatorInfo Input{};
		Input.m_HasCameraInfo = true;
		Input.m_Zoom = QmCameraEffects::EncodeDemoZoom(Case.m_Zoom);
		CUnpacker Unpacker;
		Unpacker.Reset(&Input, sizeof(Input));
		CNetObjHandler Handler;
		const auto *pSecured = static_cast<const CNetObj_DDNetSpectatorInfo *>(Handler.SecureUnpackObj(NETOBJTYPE_DDNETSPECTATORINFO, &Unpacker));
		ASSERT_NE(pSecured, nullptr);
		EXPECT_TRUE(pSecured->m_HasCameraInfo);
		EXPECT_EQ(pSecured->m_Zoom, Case.m_Encoded);
	}
}
