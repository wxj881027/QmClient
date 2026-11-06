#include <game/client/QmUi/SettingsCardContentBridge.h>

#include <gtest/gtest.h>

#include <new>
#include <stdexcept>

namespace
{
	std::shared_ptr<const CSettingsContentLayout> Layout()
	{
		CSettingsContentLayoutCache Cache;
		return Cache.Resolve({{"row", "", 24.0f}}, 240.0f, 0, {});
	}

	void ExpectRect(const CUIRect &Actual, const CUIRect &Expected)
	{
		EXPECT_FLOAT_EQ(Actual.x, Expected.x);
		EXPECT_FLOAT_EQ(Actual.y, Expected.y);
		EXPECT_FLOAT_EQ(Actual.w, Expected.w);
		EXPECT_FLOAT_EQ(Actual.h, Expected.h);
	}

	void ExpectInteractive(const CUi &Ui)
	{
		EXPECT_FALSE(Ui.BackgroundRender());
		EXPECT_FALSE(Ui.RenderOnly());
		EXPECT_FALSE(Ui.MeasurementOnly());
		EXPECT_FALSE(Ui.AnimationsBlocked());
	}
}

TEST(QmCardContentDrawFailure, SuccessfulDrawKeepsInputBlockedAndPublishesLayoutHeight)
{
	// 仅使用真实 CUi 的状态入口；这些入口及其析构不需要窗口或文字设备。
	CUi Ui;
	const auto pLayout = Layout();
	ASSERT_FLOAT_EQ(pLayout->Height(), 24.0f);
	CUIRect Content{40, 60, 240, 100};
	const CUIRect Before = Content;
	int Draws = 0;
	SSettingsCardDefinition Definition;
	Definition.m_ContentLayoutDraw = [&](const CSettingsContentLayout &DrawLayout, CUIRect Rect) {
		++Draws;
		EXPECT_EQ(&DrawLayout, pLayout.get());
		ExpectRect(Rect, Before);
		ExpectRect(Content, Before);
		EXPECT_TRUE(Ui.BackgroundRender());
		EXPECT_TRUE(Ui.RenderOnly());
		EXPECT_FALSE(Ui.MeasurementOnly());
		EXPECT_FALSE(Ui.AnimationsBlocked());
	};
	qm_settings_content::RenderLayoutContent(*pLayout, Content, &Ui, Definition.m_ContentLayoutDraw);
	EXPECT_EQ(Draws, 1);
	ExpectInteractive(Ui);
	ExpectRect(Content, {Before.x, Before.y + pLayout->Height(), Before.w, Before.h - pLayout->Height()});
}

TEST(QmCardContentDrawFailure, ResourceExceptionRestoresModesWithoutPublishingHeightAndAllowsRetry)
{
	for(bool LengthError : {false, true})
	{
		SCOPED_TRACE(LengthError);
		CUi Ui;
		const auto pLayout = Layout();
		CUIRect Content{40, 60, 240, 100};
		const CUIRect Before = Content;
		int Draws = 0;
		bool Fail = true;
		SSettingsCardDefinition Definition;
		Definition.m_ContentLayoutDraw = [&](const CSettingsContentLayout &, CUIRect) {
			++Draws;
			EXPECT_TRUE(Ui.RenderOnly());
			EXPECT_FALSE(Ui.AnimationsBlocked());
			if(!Fail)
				return;
			if(LengthError)
				throw std::length_error("content draw");
			throw std::bad_alloc();
		};
		if(LengthError)
			EXPECT_THROW(qm_settings_content::RenderLayoutContent(*pLayout, Content, &Ui, Definition.m_ContentLayoutDraw), std::length_error);
		else
			EXPECT_THROW(qm_settings_content::RenderLayoutContent(*pLayout, Content, &Ui, Definition.m_ContentLayoutDraw), std::bad_alloc);
		EXPECT_EQ(Draws, 1);
		ExpectInteractive(Ui);
		ExpectRect(Content, Before);
		Fail = false;
		qm_settings_content::RenderLayoutContent(*pLayout, Content, &Ui, Definition.m_ContentLayoutDraw);
		EXPECT_EQ(Draws, 2);
		ExpectInteractive(Ui);
		ExpectRect(Content, {Before.x, Before.y + pLayout->Height(), Before.w, Before.h - pLayout->Height()});
	}
}

TEST(QmCardContentDrawFailure, NestedDeclaredDrawUnknownExceptionRestoresParentAndLayoutContext)
{
	CUi Ui;
	const auto pLayout = Layout();
	CUIRect Content{40, 60, 240, 100};
	const CUIRect Before = Content;
	SSettingsCardDefinition Definition;
	Definition.m_RenderMeasured = [&](CUIRect &) {
		EXPECT_TRUE(Ui.RenderOnly());
		EXPECT_TRUE(Ui.AnimationsBlocked());
		ASSERT_NE(CSettingsContentLayoutScope::Current(), nullptr);
		EXPECT_EQ(CSettingsContentLayoutScope::Current()->m_Pass, ESettingsContentPass::DRAW);
		throw 17;
	};
	qm_settings_content::UseDeclaredContent(Definition, &Ui, nullptr);
	std::vector<SSettingsContentRowDeclaration> vParentRows;
	{
		const CSettingsContentLayoutScope ParentLayout(vParentRows);
		const CUiScopedBackgroundRender ParentBackground(&Ui);
		auto *pParent = CSettingsContentLayoutScope::Current();
		int Error = 0;
		try
		{
			qm_settings_content::RenderLayoutContent(*pLayout, Content, &Ui, Definition.m_ContentLayoutDraw);
		}
		catch(int Value)
		{
			Error = Value;
		}
		EXPECT_EQ(Error, 17);
		EXPECT_EQ(CSettingsContentLayoutScope::Current(), pParent);
		EXPECT_TRUE(Ui.BackgroundRender());
		EXPECT_TRUE(Ui.RenderOnly());
		EXPECT_TRUE(Ui.AnimationsBlocked());
		ExpectRect(Content, Before);
	}
	ExpectInteractive(Ui);
	EXPECT_EQ(CSettingsContentLayoutScope::Current(), nullptr);
}

TEST(QmCardContentDrawFailure, NullUiStillDrawsOnceAndPublishesLayoutHeight)
{
	const auto pLayout = Layout();
	CUIRect Content{40, 60, 240, 100};
	const CUIRect Before = Content;
	int Draws = 0;
	SSettingsCardDefinition Definition;
	Definition.m_ContentLayoutDraw = [&](const CSettingsContentLayout &DrawLayout, CUIRect Rect) {
		++Draws;
		EXPECT_EQ(&DrawLayout, pLayout.get());
		ExpectRect(Rect, Before);
	};
	qm_settings_content::RenderLayoutContent(*pLayout, Content, nullptr, Definition.m_ContentLayoutDraw);
	EXPECT_EQ(Draws, 1);
	ExpectRect(Content, {Before.x, Before.y + pLayout->Height(), Before.w, Before.h - pLayout->Height()});
}
