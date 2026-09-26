// QmNewUi 菜单源码合同：文本输入与渲染状态域：行输入活动文本单次渲染、缓冲文本上传、光标内部状态、控制台文本渲染状态恢复。
// 运行时行为保留在 qm_new_ui_menu_branch_test.cpp。
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/plausible_sizes.h>
#include <engine/client/rounded_rect_geometry.h>
#include <engine/storage.h>

#include <game/client/QmUi/UiSurface.h>
#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/menus.h>
#include <game/client/components/nameplate_text_effects.h>
#include <game/client/components/nameplates.h>
#include <game/client/components/qmclient/axiom_auto_login.h>
#include <game/client/components/tclient/statusbar.h>
#include <game/client/components/tooltips.h>
#include <game/client/prediction/gameworld.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/qmclient_source_contract_test.h>
#include <test/test.h>

#include <string>

TEST(QmNewUiMenuSettingsTextInputContract, LineInputRendersActiveTextOnlyOnce)
{
	const std::string Source = ReadTextFile("src/game/client/lineinput.cpp");
	const std::string Header = ReadTextFile("src/engine/textrender.h");
	const std::string TextSource = ReadTextFile("src/engine/client/text.cpp");
	const std::string Render = FunctionBody(Source, "STextBoundingBox CLineInput::Render(");
	const std::string RenderSelection = FunctionBody(Source, "void CLineInput::RenderSelection(");
	const std::string RenderCaret = FunctionBody(Source, "void CLineInput::RenderCaret(");
	ASSERT_FALSE(Render.empty());
	ASSERT_FALSE(RenderSelection.empty());
	ASSERT_FALSE(RenderCaret.empty());
	const auto CountOccurrences = [](const std::string &Text, const char *pNeedle) {
		size_t Count = 0;
		for(size_t Position = Text.find(pNeedle); Position != std::string::npos; Position = Text.find(pNeedle, Position + 1))
			++Count;
		return Count;
	};

	EXPECT_NE(Render.find("m_CaretPosition = Cursor.m_CursorRenderedPosition;"), std::string::npos);
	EXPECT_NE(Render.find("SetCompositionWindowPosition(m_CaretPosition + vec2"), std::string::npos);
	EXPECT_NE(Render.find("Cursor.m_RenderCursor = false;"), std::string::npos);
	EXPECT_NE(Render.find("Cursor.m_RenderSelection = false;"), std::string::npos);
	const size_t SelectionPrepass = Render.find("CTextCursor SelectionCursor = Cursor;");
	const size_t SelectionUnderlay = Render.find("RenderSelection(SelectionCursor, TextRender()->GetTextSelectionColor());", SelectionPrepass);
	const size_t TextPass = Render.find("TextRender()->TextEx(&Cursor, pDisplayStr);", SelectionPrepass);
	ASSERT_NE(SelectionPrepass, std::string::npos);
	ASSERT_NE(SelectionUnderlay, std::string::npos);
	ASSERT_NE(TextPass, std::string::npos);
	EXPECT_LT(SelectionPrepass, SelectionUnderlay);
	EXPECT_LT(SelectionUnderlay, TextPass);
	EXPECT_NE(Render.find("if(Cursor.m_HasCursorRenderedPosition)"), std::string::npos);
	EXPECT_NE(Render.find("RenderCaret(Cursor, Cursor.m_ForceCursorRendering"), std::string::npos);
	EXPECT_EQ(Render.find("CTextCursor CaretCursor;"), std::string::npos);
	EXPECT_EQ(Render.find("TextRender()->TextEx(&CaretCursor, pDisplayStr);"), std::string::npos);
	EXPECT_EQ(CountOccurrences(Render, "TextRender()->TextEx(&Cursor, pDisplayStr);"), 2u);
	EXPECT_NE(Header.find("bool m_RenderCursor = true;"), std::string::npos);
	EXPECT_NE(Header.find("bool m_RenderSelection = true;"), std::string::npos);
	EXPECT_NE(Header.find("bool m_HasCursorRenderedPosition = false;"), std::string::npos);
	EXPECT_NE(TextSource.find("const bool HasRenderedCursor = HasCursor && pCursor->m_RenderCursor;"), std::string::npos);
	EXPECT_NE(TextSource.find("const bool HasRenderedSelection = HasSelection && pCursor->m_RenderSelection;"), std::string::npos);
	const size_t SelectionRenderPos = TextSource.find("if(TextContainer.m_HasSelection)");
	const size_t TextRenderPos = TextSource.find("if(!TextContainer.m_StringInfo.m_vCharacterQuads.empty())");
	ASSERT_NE(SelectionRenderPos, std::string::npos);
	ASSERT_NE(TextRenderPos, std::string::npos);
	EXPECT_GT(SelectionRenderPos, TextRenderPos);
	EXPECT_NE(TextSource.find("if(SelectionStarted)"), std::string::npos);
	EXPECT_NE(TextSource.find("pCursor->m_HasCursorRenderedPosition = true;"), std::string::npos);
	const size_t TextExPos = TextSource.find("void TextEx(CTextCursor *pCursor, const char *pText, int Length = -1) override");
	const size_t LayoutOnlyGuard = TextSource.find("if((pCursor->m_Flags & TEXTFLAG_RENDER) == 0)", TextExPos);
	const size_t LayoutContainer = TextSource.find("STextContainer LayoutContainer;", LayoutOnlyGuard);
	const size_t LayoutPass = TextSource.find("AppendTextContainerImpl(LayoutContainer, pCursor, pText, Length);", LayoutContainer);
	const size_t RenderContainer = TextSource.find("STextContainerIndex TextCont;", LayoutPass);
	ASSERT_NE(TextExPos, std::string::npos);
	ASSERT_NE(LayoutOnlyGuard, std::string::npos);
	ASSERT_NE(LayoutContainer, std::string::npos);
	ASSERT_NE(LayoutPass, std::string::npos);
	ASSERT_NE(RenderContainer, std::string::npos);
	EXPECT_LT(LayoutOnlyGuard, LayoutContainer);
	EXPECT_LT(LayoutContainer, LayoutPass);
	EXPECT_LT(LayoutPass, RenderContainer);
	EXPECT_EQ(TextSource.find("CreateTextContainer(LayoutContainer", LayoutOnlyGuard), std::string::npos);
	EXPECT_NE(RenderSelection.find("Graphics()->TextureClear();"), std::string::npos);
	EXPECT_NE(RenderSelection.find("Graphics()->QuadsBegin();"), std::string::npos);
	EXPECT_NE(RenderSelection.find("Graphics()->QuadsEnd();"), std::string::npos);
	EXPECT_NE(RenderSelection.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);"), std::string::npos);
	EXPECT_EQ(RenderSelection.find("RenderQuadContainerEx"), std::string::npos);
	EXPECT_NE(RenderCaret.find("Graphics()->QuadsBegin();"), std::string::npos);
	EXPECT_NE(RenderCaret.find("Graphics()->QuadsEnd();"), std::string::npos);
	EXPECT_EQ(RenderCaret.find("RenderQuadContainerEx"), std::string::npos);
	EXPECT_NE(RenderCaret.find("if(!Cursor.m_HasCursorRenderedPosition)"), std::string::npos);
	EXPECT_EQ(RenderCaret.find("m_CursorRenderedPosition.x < 0.0f"), std::string::npos);
	EXPECT_EQ(RenderCaret.find("m_CursorRenderedPosition.y < 0.0f"), std::string::npos);
	const size_t TextureClear = RenderCaret.find("Graphics()->TextureClear();");
	const size_t HiddenReturn = RenderCaret.find("if(!ForceVisible && !qm_lineinput::CaretVisibleForElapsed");
	ASSERT_NE(TextureClear, std::string::npos);
	ASSERT_NE(HiddenReturn, std::string::npos);
	EXPECT_LT(TextureClear, HiddenReturn);
}

TEST(QmNewUiMenuSettingsTextInputContract, BufferedTextUploadsMissingGpuContainerWithoutUsingImmediateQuads)
{
	const std::string TextSource = ReadTextFile("src/engine/client/text.cpp");
	const std::string Render = FunctionBody(TextSource, "void RenderTextContainer(STextContainerIndex TextContainerIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor) override");
	const std::string Upload = FunctionBody(TextSource, "void UploadTextContainer(STextContainerIndex TextContainerIndex) override");

	ASSERT_FALSE(Render.empty());
	ASSERT_FALSE(Upload.empty());
	const size_t BufferedPath = Render.find("if(Graphics()->IsTextBufferingEnabled())");
	const size_t MissingContainer = Render.find("if(TextContainer.m_StringInfo.m_QuadBufferContainerIndex == -1)", BufferedPath);
	const size_t UploadMissingContainer = Render.find("UploadTextContainer(TextContainerIndex);", MissingContainer);
	const size_t BufferedRender = Render.find("Graphics()->RenderText(", UploadMissingContainer);
	const size_t ImmediateFallback = Render.find("else\n\t\t\t{\n\t\t\t\t// render tiles", BufferedPath);
	ASSERT_NE(BufferedPath, std::string::npos);
	ASSERT_NE(MissingContainer, std::string::npos);
	ASSERT_NE(UploadMissingContainer, std::string::npos);
	ASSERT_NE(BufferedRender, std::string::npos);
	ASSERT_NE(ImmediateFallback, std::string::npos);
	EXPECT_LT(BufferedPath, MissingContainer);
	EXPECT_LT(MissingContainer, UploadMissingContainer);
	EXPECT_LT(UploadMissingContainer, BufferedRender);
	EXPECT_LT(BufferedRender, ImmediateFallback);
	EXPECT_EQ(Render.find("Graphics()->IsTextBufferingEnabled() &&"), std::string::npos);
	EXPECT_NE(Render.find("Graphics()->QuadsBegin();"), std::string::npos);
	const size_t CreateBuffer = Upload.find("Graphics()->CreateBufferObject(");
	const size_t RecreateBuffer = Upload.find("Graphics()->RecreateBufferObject(");
	const size_t CreateContainer = Upload.find("Graphics()->CreateBufferContainer(&m_DefaultTextContainerInfo);");
	const size_t EmptyTextReturn = Upload.find("if(TextContainer.m_StringInfo.m_vCharacterQuads.empty())");
	ASSERT_NE(CreateBuffer, std::string::npos);
	ASSERT_NE(RecreateBuffer, std::string::npos);
	ASSERT_NE(CreateContainer, std::string::npos);
	ASSERT_NE(EmptyTextReturn, std::string::npos);
	EXPECT_LT(EmptyTextReturn, CreateBuffer);
	EXPECT_LT(CreateBuffer, RecreateBuffer);
	EXPECT_LT(RecreateBuffer, CreateContainer);
	EXPECT_EQ(Upload.find("Graphics()->DeleteBufferContainer("), std::string::npos);
}

TEST(QmNewUiMenuSettingsTextInputContract, TextRendererKeepsInternalCaretStateSelfContained)
{
	const std::string Source = ReadTextFile("src/engine/client/text.cpp");
	const std::string Render = FunctionBody(Source, "void RenderTextContainer(STextContainerIndex TextContainerIndex, const ColorRGBA &TextColor, const ColorRGBA &TextOutlineColor) override");
	ASSERT_FALSE(Render.empty());

	const size_t CursorBlock = Render.find("if(TextContainer.m_HasCursor)");
	ASSERT_NE(CursorBlock, std::string::npos);
	EXPECT_NE(Render.find("Graphics()->TextureClear();", CursorBlock), std::string::npos);
	EXPECT_NE(Render.find("Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);", CursorBlock), std::string::npos);
}

TEST(QmNewUiMenuSettingsTextInputContract, ConsoleRestoresCompleteTextRenderState)
{
	const std::string Source = ReadTextFile("src/game/client/components/console.cpp");
	const std::string Render = FunctionBody(Source, "void CGameConsole::OnRender()");
	ASSERT_FALSE(Render.empty());

	for(const char *pState : {
		    "const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();",
		    "const ColorRGBA PreviousTextOutlineColor = TextRender()->GetTextOutlineColor();",
		    "const ColorRGBA PreviousTextSelectionColor = TextRender()->GetTextSelectionColor();",
		    "const unsigned PreviousRenderFlags = TextRender()->GetRenderFlags();",
		    "const EFontPreset PreviousFontPreset = TextRender()->GetFontPreset();",
		    "TextRender()->SetRenderFlags(PreviousRenderFlags);",
		    "TextRender()->SetFontPreset(PreviousFontPreset);",
		    "TextRender()->TextOutlineColor(PreviousTextOutlineColor);",
		    "TextRender()->TextSelectionColor(PreviousTextSelectionColor);",
		    "TextRender()->TextColor(PreviousTextColor);",
	    })
		EXPECT_NE(Render.find(pState), std::string::npos) << pState;

	EXPECT_LT(Render.find("Ui()->SetEnabled(false);"), Render.find("TextRender()->SetRenderFlags(PreviousRenderFlags);"));
}
