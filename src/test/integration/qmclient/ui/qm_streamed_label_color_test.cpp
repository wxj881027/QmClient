// 设置页文字池（streamed 文本容器）的逐字色段：SLabelProperties::SetColor()
// 的颜色必须真的烘进容器顶点，且只在色段变化时重建容器；没有色段的标签继续走
// 「白底 + 绘制时乘调用方颜色」的旧模型。仅替换 GPU 设备边界。
#include <engine/textrender.h>

#include <game/client/qm_icon.h>

#include <test/support/qm_real_ui_fixture.h>

namespace
{
	class QmStreamedLabelColor : public qm_ui_test::CRealUiFixture
	{
	protected:
		CUIElement::SUIElementRect m_Label;
		const CUIRect m_Rect{10, 10, 240, 18};

		void TearDown() override
		{
			if(m_Ui.TextRender() != nullptr)
				m_Ui.TextRender()->DeleteTextContainer(m_Label.m_UITextContainer);
			CRealUiFixture::TearDown();
		}

		// 与设置页文字池同一条 streamed 容器路径，并回报这一帧是否重建了容器。
		bool DrawStreamed(const char *pText, const SLabelProperties &Props)
		{
			bool Recreated = false;
			m_Ui.DoLabelStreamed(m_Label, &m_Rect, pText, 12.0f, TEXTALIGN_ML, Props, -1, nullptr, true, &Recreated);
			return Recreated;
		}

		static SLabelProperties SolidColor(const ColorRGBA &Color)
		{
			SLabelProperties Props;
			Props.SetColor(Color);
			return Props;
		}

		static ColorRGBA RedColor() { return ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f); }
		static ColorRGBA BlueColor() { return ColorRGBA(0.0f, 0.0f, 1.0f, 1.0f); }
		static ColorRGBA WhiteColor() { return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f); }
	};
}

// 关掉文字缓冲后引擎把每个字形的最终填充色交给设备上色，填充色 = 容器顶点色 × 绘制
// 底色。修复前色段被忽略、顶点仍是默认白，这里读到的就是纯白。
TEST_F(QmStreamedLabelColor, ColorSplitBakesIntoGlyphFillColors)
{
	UiGraphics()->m_TextBuffering = false;
	UiGraphics()->m_vQuadVertexColors.clear();
	DrawStreamed("Match", SolidColor(RedColor()));

	ASSERT_TRUE(m_Label.m_UITextContainer.Valid());
	ASSERT_FALSE(UiGraphics()->m_vQuadVertexColors.empty()) << "没有产生任何逐字填充色提交";
	for(const auto &Vertex : UiGraphics()->m_vQuadVertexColors)
	{
		SCOPED_TRACE(Vertex.m_QuadOffset);
		EXPECT_EQ(Vertex.m_r, 255) << "色段没有烘进容器顶点（修复前这里是默认白）";
		EXPECT_EQ(Vertex.m_g, 0);
		EXPECT_EQ(Vertex.m_b, 0);
		EXPECT_EQ(Vertex.m_a, 255);
	}
}

// 烘色容器的绘制底色必须是白色，否则色段会被调用方颜色再乘一次。这里故意让调用方
// 颜色与色段不同，白色底和「被乘过的底」才能区分开；父层显隐系数仍要生效。
TEST_F(QmStreamedLabelColor, ColorSplitDrawsWithWhiteTintAndControlAlpha)
{
	m_Ui.TextRender()->TextColor(ColorRGBA(0.5f, 0.25f, 0.125f, 1.0f));
	UiGraphics()->m_vTextSubmissions.clear();
	DrawStreamed("Match", SolidColor(RedColor()));
	ASSERT_EQ(UiGraphics()->m_vTextSubmissions.size(), 1u);
	const auto BakedSubmission = UiGraphics()->m_vTextSubmissions.back();
	EXPECT_FLOAT_EQ(BakedSubmission.m_TextColor.r, 1.0f);
	EXPECT_FLOAT_EQ(BakedSubmission.m_TextColor.g, 1.0f);
	EXPECT_FLOAT_EQ(BakedSubmission.m_TextColor.b, 1.0f);
	EXPECT_FLOAT_EQ(BakedSubmission.m_TextColor.a, 1.0f);

	{
		const CUiScopedControlAlphaScale AlphaScale(&m_Ui, 0.5f);
		UiGraphics()->m_vTextSubmissions.clear();
		DrawStreamed("Match", SolidColor(RedColor()));
		ASSERT_EQ(UiGraphics()->m_vTextSubmissions.size(), 1u);
		const auto FadedSubmission = UiGraphics()->m_vTextSubmissions.back();
		EXPECT_FLOAT_EQ(FadedSubmission.m_TextColor.r, 1.0f);
		EXPECT_FLOAT_EQ(FadedSubmission.m_TextColor.a, 0.5f) << "淡入淡出必须继续作用在烘色容器上";
	}
	m_Ui.TextRender()->TextColor(WhiteColor());
}

// 文本、尺寸、字号完全不变，只有色段换了：容器必须重建，否则颜色会冻在旧色段上。
TEST_F(QmStreamedLabelColor, ColorSplitChangeRebuildsContainer)
{
	ASSERT_TRUE(DrawStreamed("Match", SolidColor(RedColor()))) << "首帧必须建容器";
	ASSERT_FALSE(DrawStreamed("Match", SolidColor(RedColor())));
	EXPECT_TRUE(DrawStreamed("Match", SolidColor(BlueColor()))) << "色段变化没有触发重建";
	EXPECT_FALSE(DrawStreamed("Match", SolidColor(BlueColor())));
	// 色段被摘掉也是变化：必须回到「白底 + 乘色」的模型。
	EXPECT_TRUE(DrawStreamed("Match", SLabelProperties{})) << "色段消失没有触发重建";
	EXPECT_FALSE(DrawStreamed("Match", SLabelProperties{}));
}

// 色段不变时必须继续复用容器：缓存不能被色段判定变成逐帧重建。
TEST_F(QmStreamedLabelColor, StableColorSplitReusesContainer)
{
	ASSERT_TRUE(DrawStreamed("Match DDNet CHN6", SolidColor(RedColor())));
	const int FirstIndex = m_Label.m_UITextContainer.m_Index;
	for(int Frame = 0; Frame < 4; ++Frame)
	{
		SCOPED_TRACE(Frame);
		EXPECT_FALSE(DrawStreamed("Match DDNet CHN6", SolidColor(RedColor())));
		// 重建会先归还旧槽位，引擎的空闲表会原地复用同一个索引，
		// 所以索引相等只是旁证，重建与否由 Recreated 判定。
		EXPECT_EQ(m_Label.m_UITextContainer.m_Index, FirstIndex);
	}
}

// 重建出来的容器必须带新颜色：只把判定改对、容器内容没刷新同样是坏的。
TEST_F(QmStreamedLabelColor, RebuiltContainerShowsNewColor)
{
	UiGraphics()->m_TextBuffering = false;
	DrawStreamed("Match", SolidColor(RedColor()));
	ASSERT_FALSE(UiGraphics()->m_vQuadVertexColors.empty());

	UiGraphics()->m_vQuadVertexColors.clear();
	DrawStreamed("Match", SolidColor(BlueColor()));
	ASSERT_FALSE(UiGraphics()->m_vQuadVertexColors.empty());
	for(const auto &Vertex : UiGraphics()->m_vQuadVertexColors)
	{
		SCOPED_TRACE(Vertex.m_QuadOffset);
		EXPECT_EQ(Vertex.m_r, 0);
		EXPECT_EQ(Vertex.m_g, 0);
		EXPECT_EQ(Vertex.m_b, 255);
	}
}

// 没有色段的标签仍是旧模型：容器顶点保持默认白，颜色由绘制底色乘出来。
TEST_F(QmStreamedLabelColor, PlainLabelKeepsDrawTimeTint)
{
	m_Ui.TextRender()->TextColor(ColorRGBA(0.25f, 0.5f, 0.75f, 1.0f));
	UiGraphics()->m_vTextSubmissions.clear();
	DrawStreamed("Match", SLabelProperties{});
	ASSERT_EQ(UiGraphics()->m_vTextSubmissions.size(), 1u);
	const auto PlainSubmission = UiGraphics()->m_vTextSubmissions.back();
	EXPECT_FLOAT_EQ(PlainSubmission.m_TextColor.r, 0.25f) << "无色段标签的底色必须等于调用方颜色";
	EXPECT_FLOAT_EQ(PlainSubmission.m_TextColor.g, 0.5f);
	EXPECT_FLOAT_EQ(PlainSubmission.m_TextColor.b, 0.75f);

	UiGraphics()->m_TextBuffering = false;
	m_Ui.TextRender()->TextColor(WhiteColor());
	UiGraphics()->m_vQuadVertexColors.clear();
	DrawStreamed("Match", SLabelProperties{});
	ASSERT_FALSE(UiGraphics()->m_vQuadVertexColors.empty());
	for(const auto &Vertex : UiGraphics()->m_vQuadVertexColors)
	{
		SCOPED_TRACE(Vertex.m_QuadOffset);
		EXPECT_EQ(Vertex.m_r, 255) << "无色段容器的顶点应保持默认白";
		EXPECT_EQ(Vertex.m_g, 255);
		EXPECT_EQ(Vertex.m_b, 255);
	}
}

// 立即路径是本次修复的对照：同样的色段在 DoLabel(const CUIRect *) 上一直生效。
TEST_F(QmStreamedLabelColor, ImmediateLabelKeepsColorSplit)
{
	UiGraphics()->m_TextBuffering = false;
	UiGraphics()->m_vQuadVertexColors.clear();
	m_Ui.DoLabel(&m_Rect, "Match", 12.0f, TEXTALIGN_ML, SolidColor(RedColor()));

	ASSERT_FALSE(UiGraphics()->m_vQuadVertexColors.empty());
	for(const auto &Vertex : UiGraphics()->m_vQuadVertexColors)
	{
		SCOPED_TRACE(Vertex.m_QuadOffset);
		EXPECT_EQ(Vertex.m_r, 255);
		EXPECT_EQ(Vertex.m_g, 0);
		EXPECT_EQ(Vertex.m_b, 0);
	}
}

// 单图标标签：未烘色时绘制色由全局图标预设决定；烘了色段后图标本体和其它文字一样由
// 色段决定，绝不能再乘一层图标预设色，否则就是双重染色。
TEST_F(QmStreamedLabelColor, BakedIconLabelDropsIconPresetTint)
{
	g_Config.m_QmUiIconColor = 2; // 预设 2 = 纯黑，与白色底明显可区分
	g_Config.m_QmUiIconCustomColorEnabled = 0;
	char aGlyph[8] = "";
	str_utf8_encode(aGlyph, CQmIconRegistry::Codepoint(EQmIcon::GEAR));
	m_Ui.TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	m_Ui.TextRender()->TextColor(WhiteColor());

	UiGraphics()->m_vTextSubmissions.clear();
	DrawStreamed(aGlyph, SLabelProperties{});
	ASSERT_EQ(m_Label.m_NumQmIcons, 1);
	ASSERT_EQ(UiGraphics()->m_vTextSubmissions.size(), 1u);
	const auto IconSubmission = UiGraphics()->m_vTextSubmissions.back();
	EXPECT_FLOAT_EQ(IconSubmission.m_TextColor.r, 0.0f) << "未烘色时图标预设色仍然生效";
	EXPECT_FLOAT_EQ(IconSubmission.m_TextColor.g, 0.0f);
	EXPECT_FLOAT_EQ(IconSubmission.m_TextColor.b, 0.0f);

	UiGraphics()->m_vTextSubmissions.clear();
	DrawStreamed(aGlyph, SolidColor(RedColor()));
	ASSERT_EQ(m_Label.m_NumQmIcons, 1);
	ASSERT_EQ(UiGraphics()->m_vTextSubmissions.size(), 1u);
	const auto BakedIconSubmission = UiGraphics()->m_vTextSubmissions.back();
	EXPECT_FLOAT_EQ(BakedIconSubmission.m_TextColor.r, 1.0f) << "烘色后不能再叠加图标预设色";
	EXPECT_FLOAT_EQ(BakedIconSubmission.m_TextColor.g, 1.0f);
	EXPECT_FLOAT_EQ(BakedIconSubmission.m_TextColor.b, 1.0f);
	EXPECT_FLOAT_EQ(BakedIconSubmission.m_TextColor.a, 1.0f);

	m_Ui.TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}
