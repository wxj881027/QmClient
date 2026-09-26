#include <gtest/gtest.h>
#include <test/test.h>

#include <algorithm>
#include <string>

namespace
{
	std::string ReadTextFile(const char *pPath)
	{
		std::string Content = ReadTestSourceFile(pPath);
		Content.erase(std::remove(Content.begin(), Content.end(), '\r'), Content.end());
		return Content;
	}

	std::string FunctionBody(const std::string &Source, const std::string &Signature)
	{
		const size_t FunctionStart = Source.find(Signature);
		EXPECT_NE(FunctionStart, std::string::npos) << Signature;
		const size_t BodyStart = Source.find("{", FunctionStart);
		EXPECT_NE(BodyStart, std::string::npos) << Signature;
		int Depth = 0;
		for(size_t Index = BodyStart; Index < Source.size(); ++Index)
		{
			if(Source[Index] == '{')
				++Depth;
			else if(Source[Index] == '}')
			{
				--Depth;
				if(Depth == 0)
					return Source.substr(BodyStart, Index - BodyStart);
			}
		}
		ADD_FAILURE() << Signature;
		return {};
	}
}

TEST(TClientStatusBarScoreContract, RegistersUniqueScoreSchemeCode)
{
	const std::string Header = ReadTextFile("src/game/client/components/tclient/statusbar.h");
	const std::string Source = ReadTextFile("src/game/client/components/tclient/statusbar.cpp");
	const std::string Config = ReadTextFile("src/engine/shared/config_variables_tclient.h");
	const std::string ApplyScheme = FunctionBody(Source, "void CStatusBar::ApplyStatusBarScheme(const char *pScheme)");
	const std::string UpdateScheme = FunctionBody(Source, "void CStatusBar::UpdateStatusBarScheme(char *pScheme)");
	const std::string ScoreRegistration = "\"s\", \"Points\", \"Points\", \"Displays the DDNet Points of the current player\"";

	const size_t RegistrationPos = Header.find(ScoreRegistration);
	ASSERT_NE(RegistrationPos, std::string::npos);
	EXPECT_EQ(Header.find(ScoreRegistration, RegistrationPos + 1), std::string::npos);
	EXPECT_NE(Header.find("m_Zoom, m_Score, m_Downstream"), std::string::npos);
	EXPECT_NE(ApplyScheme.find("for(char ItemLetter : ItemType.m_aLetters)"), std::string::npos);
	EXPECT_NE(ApplyScheme.find("m_StatusBarItems.push_back(&ItemType);"), std::string::npos);
	EXPECT_NE(UpdateScheme.find("pScheme[Index++] = pItem->m_aLetters[0];"), std::string::npos);
	EXPECT_NE(Config.find("MACRO_CONFIG_STR(TcStatusBarScheme, tc_statusbar_scheme, 129,"), std::string::npos);
	EXPECT_NE(Header.find("STATUSBAR_MAX_SIZE = 128"), std::string::npos);
}

TEST(TClientStatusBarContract, IgnoresPlayerItemsForInvalidSpectatorIds)
{
	const std::string Source = ReadTextFile("src/game/client/components/tclient/statusbar.cpp");
	const std::string AngleWidth = FunctionBody(Source, "float CStatusBar::AngleWidth()");
	const std::string PingWidth = FunctionBody(Source, "float CStatusBar::PingWidth()");
	const std::string PositionWidth = FunctionBody(Source, "float CStatusBar::PositionWidth()");
	const std::string VelocityWidth = FunctionBody(Source, "float CStatusBar::VelocityWidth()");

	EXPECT_NE(Source.find("static_assert(STATUSBAR_MAX_SIZE < sizeof(g_Config.m_TcStatusBarScheme));"), std::string::npos);
	EXPECT_NE(AngleWidth.find("if(!tclient_statusbar::IsValidPlayerId(m_PlayerId))"), std::string::npos);
	EXPECT_NE(PingWidth.find("if(!tclient_statusbar::IsValidPlayerId(m_PlayerId) || !GameClient()->m_Snap.m_apPlayerInfos[m_PlayerId])"), std::string::npos);
	EXPECT_NE(PositionWidth.find("if(!tclient_statusbar::IsValidPlayerId(m_PlayerId) || !GameClient()->m_Snap.m_apPlayerInfos[m_PlayerId])"), std::string::npos);
	EXPECT_NE(VelocityWidth.find("if(!tclient_statusbar::IsValidPlayerId(m_PlayerId) || !GameClient()->m_Snap.m_apPlayerInfos[m_PlayerId])"), std::string::npos);
}

TEST(TClientStatusBarContract, RefreshesDynamicLayoutInputsBeforeRendering)
{
	const std::string Header = ReadTextFile("src/game/client/components/tclient/statusbar.h");
	const std::string Source = ReadTextFile("src/game/client/components/tclient/statusbar.cpp");
	const std::string Render = FunctionBody(Source, "void CStatusBar::OnRender()");
	const std::string RaceRender = FunctionBody(Source, "void CStatusBar::RaceTimeRender()");
	const std::string ApplyScheme = FunctionBody(Source, "void CStatusBar::ApplyStatusBarScheme(const char *pScheme)");
	const std::string UpdateScheme = FunctionBody(Source, "void CStatusBar::UpdateStatusBarScheme(char *pScheme)");
	const std::string ConnectionWidth = FunctionBody(Source, "float CStatusBar::ConnectionGradeWidth()");

	EXPECT_NE(Header.find("int CalculateRaceTime();"), std::string::npos);
	const size_t RaceTimeUpdate = Render.find("m_CurrentRaceTime = CalculateRaceTime();");
	const size_t LayoutWidth = Render.find("LayoutItem.m_ItemWidth = pItem->m_GetWidth();");
	ASSERT_NE(RaceTimeUpdate, std::string::npos);
	ASSERT_NE(LayoutWidth, std::string::npos);
	EXPECT_LT(RaceTimeUpdate, LayoutWidth);
	EXPECT_NE(RaceRender.find("const int RaceTime = m_CurrentRaceTime;"), std::string::npos);
	EXPECT_NE(Header.find("m_aAppliedStatusBarScheme"), std::string::npos);
	EXPECT_NE(Render.find("str_comp(m_aAppliedStatusBarScheme, g_Config.m_TcStatusBarScheme) != 0"), std::string::npos);
	EXPECT_NE(ApplyScheme.find("str_copy(m_aAppliedStatusBarScheme, pScheme"), std::string::npos);
	EXPECT_NE(UpdateScheme.find("str_copy(m_aAppliedStatusBarScheme, pScheme"), std::string::npos);
	EXPECT_NE(ConnectionWidth.find("ConnectionGradeLabel(GameClient()->m_QmMonitoring.Snapshot().m_Verdict.m_Grade)"), std::string::npos);
	EXPECT_EQ(ConnectionWidth.find("Localize(\"Severe\")"), std::string::npos);
}
