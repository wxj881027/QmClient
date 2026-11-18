// 取色器弹窗的共享测试夹具：真实 UI 与弹层渲染，只替换输入设备、时钟和 GPU。
// 文本输入与调色板生命周期两个功能域共用这里的输入、开窗与坐标辅助。
#ifndef TEST_SUPPORT_QM_COLOR_PICKER_UI_FIXTURE_H
#define TEST_SUPPORT_QM_COLOR_PICKER_UI_FIXTURE_H

#include <engine/keys.h>

#include <game/client/QmUi/ColorPickerPalette.h>
#include <game/client/ui_scrollregion.h>

#include <test/support/qm_real_ui_fixture.h>
#include <test/support/qm_ui_test_client.h>
#include <test/support/qm_ui_test_input.h>

#include <string>
#include <vector>

namespace qm_ui_test
{
	class CColorPickerUiFixture : public CRealUiFixture
	{
	protected:
		qm_ui_test::CTestInput m_Input;
		qm_ui_test::CTestUiClient m_Client;
		CUi::SColorPickerPopupContext m_Picker;
		unsigned m_Color = 0;

		IInput *UiInput() override { return &m_Input; }
		IClient *UiClient() override { return &m_Client; }

		void Frame()
		{
			m_Client.AdvanceFrame();
			m_Ui.Update();
			m_Ui.RenderPopupMenus();
		}

		// 需要在指定位置点击时使用：坐标是屏幕逻辑像素，转换与真实窗口一致。
		void FrameAt(vec2 Position, bool Pressed)
		{
			const vec2 Pixels = Position * vec2(m_Ui.Graphics()->WindowWidth(), m_Ui.Graphics()->WindowHeight()) / m_Ui.Screen()->Size();
			const vec2 Delta = Pixels - m_Ui.UpdatedMousePos();
			m_Ui.OnCursorMove(Delta.x, Delta.y);
			m_Input.m_HeldKey = Pressed ? KEY_MOUSE_1 : 0;
			Frame();
		}

		void Click(const vec2 &Position)
		{
			// 热项来自上一帧渲染：先把指针移过去，再按下，否则这一次点击仍按旧位置判定。
			FrameAt(Position, false);
			FrameAt(Position, true);
			FrameAt(Position, false);
		}

		void PressKey(int Key)
		{
			IInput::CEvent Event{};
			Event.m_Flags = IInput::FLAG_PRESS;
			Event.m_Key = Key;
			ASSERT_TRUE(m_Ui.OnInput(Event));
			Frame();
		}

		void TypeText(const char *pText)
		{
			IInput::CEvent Event{};
			Event.m_Flags = IInput::FLAG_TEXT;
			str_copy(Event.m_aText, pText);
			ASSERT_TRUE(m_Ui.OnInput(Event));
		}

		void SetPaletteConfig(const char *pConfig)
		{
			str_copy(g_Config.m_QmColorPickerPalette, pConfig, (int)sizeof(g_Config.m_QmColorPickerPalette));
		}

		void Open(bool Alpha = true, bool AlphaPercent = false, const char *pPaletteConfig = "")
		{
			g_Config.m_QmColorPickerShape = CUi::SColorPickerPopupContext::SHAPE_SQUARE;
			SetPaletteConfig(pPaletteConfig);
			m_Picker.m_ColorMode = CUi::SColorPickerPopupContext::MODE_RGBA;
			m_Picker.m_Alpha = Alpha;
			m_Picker.m_AlphaPercent = AlphaPercent;
			m_Picker.m_RgbaColor = ColorRGBA(55.0f / 255.0f, 1.0f, 1.0f / 255.0f, 1.0f);
			m_Picker.m_HslaColor = color_cast<ColorHSLA>(m_Picker.m_RgbaColor);
			m_Picker.m_HsvaColor = color_cast<ColorHSVA>(m_Picker.m_HslaColor);
			m_Color = m_Picker.m_HslaColor.Pack(Alpha);
			m_Picker.m_pHslaColor = &m_Color;
			m_Ui.ShowPopupColorPicker(0.0f, 0.0f, &m_Picker);
			Frame();
			ASSERT_TRUE(m_Ui.IsPopupOpen(&m_Picker));
		}

		// 48 格满调色板：取“色 0”..“色 47”，颜色各不相同。
		void OpenWithMaxPalette()
		{
			std::vector<qm_picker_palette::SSwatch> vSwatches;
			for(int i = 0; i < qm_picker_palette::MAX_SWATCHES; i++)
			{
				qm_picker_palette::SSwatch Swatch;
				str_format(Swatch.m_aName, sizeof(Swatch.m_aName), "色 %d", i);
				Swatch.m_Color = ColorRGBA(i / 255.0f, (255 - i) / 255.0f, ((i * 37) % 256) / 255.0f, 1.0f);
				vSwatches.push_back(Swatch);
			}
			char aBuf[4096];
			qm_picker_palette::Serialize(aBuf, (int)sizeof(aBuf), vSwatches);
			Open(true, false, aBuf);
		}

		void Focus(CLineInput &Input)
		{
			m_Ui.SetActiveItem(&Input);
			Frame();
			ASSERT_TRUE(Input.IsActive());
		}

		void Paste(CLineInput &Input, const char *pText)
		{
			Input.SelectAll();
			m_Input.m_Clipboard = pText;
			m_Input.m_Modifier = true;
			IInput::CEvent Event{};
			Event.m_Flags = IInput::FLAG_PRESS;
			Event.m_Key = KEY_V;
			const bool Handled = m_Ui.OnInput(Event);
			m_Input.m_Modifier = false;
			ASSERT_TRUE(Handled);
			Frame();
		}

		void Blur(CLineInput &Input)
		{
			m_Ui.ReleaseActiveTextInput(&Input);
			Frame();
			EXPECT_FALSE(Input.IsActive());
		}

		void ClearText(CLineInput &Input)
		{
			Input.SelectAll();
			PressKey(KEY_BACKSPACE);
		}

		// 持久化合同：工作副本与配置串解析结果逐项一致（名字 + 打包颜色）。
		void ExpectPaletteSaved() const
		{
			const auto vPersisted = qm_picker_palette::Parse(g_Config.m_QmColorPickerPalette);
			ASSERT_EQ(vPersisted.size(), m_Picker.m_vPalette.size());
			for(size_t i = 0; i < vPersisted.size(); i++)
			{
				SCOPED_TRACE(i);
				EXPECT_STREQ(vPersisted[i].m_aName, m_Picker.m_vPalette[i].m_aName);
				EXPECT_EQ(vPersisted[i].m_Color.PackAlphaLast(), m_Picker.m_vPalette[i].m_Color.PackAlphaLast());
			}
		}

		const CUIRect *PopupRect() const
		{
			const CUIRect *pPopup = m_Ui.GetPopupMenuRect(&m_Picker);
			if(pPopup == nullptr)
				ADD_FAILURE() << "取色器弹窗未打开";
			return pPopup;
		}

		// 标题栏右侧：右起第一枚是 ✕，第二枚是 ✓（✓ 比 ✕ 宽 2px）。
		vec2 CloseButtonCenter() const
		{
			const CUIRect *pPopup = PopupRect();
			if(pPopup == nullptr)
				return vec2(-1.0e6f, -1.0e6f);
			const float Inset = m_Ui.PopupMenuContentInset() * 0.5f;
			const float TitleHeight = 15.0f;
			return {pPopup->x + pPopup->w - Inset - TitleHeight * 0.5f, pPopup->y + Inset + TitleHeight * 0.5f};
		}

		vec2 AcceptButtonCenter() const
		{
			const CUIRect *pPopup = PopupRect();
			if(pPopup == nullptr)
				return vec2(-1.0e6f, -1.0e6f);
			const float Inset = m_Ui.PopupMenuContentInset() * 0.5f;
			const float TitleHeight = 15.0f;
			const float AcceptWidth = TitleHeight + 2.0f;
			return {pPopup->x + pPopup->w - Inset - TitleHeight - 2.0f - AcceptWidth * 0.5f, pPopup->y + Inset + TitleHeight * 0.5f};
		}

		// 页签行：0=HSVA、1=RGBA、2=HSLA；右端 46px 是方图/色环切换。
		vec2 ModeTabCenter(int ModeIndex) const
		{
			const CUIRect *pPopup = PopupRect();
			if(pPopup == nullptr)
				return vec2(-1.0e6f, -1.0e6f);
			const float Inset = m_Ui.PopupMenuContentInset() * 0.5f;
			const float TabTop = pPopup->y + Inset + 15.0f + 6.0f;
			return {pPopup->x + Inset + ModeIndex * (36.0f + 2.0f) + 18.0f, TabTop + 7.5f};
		}

		vec2 ShapeToggleCenter() const
		{
			const CUIRect *pPopup = PopupRect();
			if(pPopup == nullptr)
				return vec2(-1.0e6f, -1.0e6f);
			const float Inset = m_Ui.PopupMenuContentInset() * 0.5f;
			const float TabTop = pPopup->y + Inset + 15.0f + 6.0f;
			return {pPopup->x + pPopup->w - Inset - 23.0f, TabTop + 7.5f};
		}

		// 调色板标题行右侧按钮：0=＋、1=改名、2=删除、3=撤回。
		vec2 PaletteHeaderButtonCenter(int SlotFromRight) const
		{
			const CUIRect *pPopup = PopupRect();
			if(pPopup == nullptr)
				return vec2(-1.0e6f, -1.0e6f);
			const float Inset = m_Ui.PopupMenuContentInset() * 0.5f;
			const float Right = pPopup->x + pPopup->w - Inset;
			const float ButtonSize = 14.0f;
			const float Gap = 4.0f;
			const float PaletteAreaHeight = 16.0f + 2.0f + 128.0f;
			const float HeaderCenterY = pPopup->y + pPopup->h - Inset - PaletteAreaHeight + 16.0f * 0.5f;
			return {Right - ButtonSize * 0.5f - SlotFromRight * (ButtonSize + Gap), HeaderCenterY};
		}

		// 用户调色板第 Index 个色块的圆心；Index=-1 是首格“当前颜色”。
		// 视口宽度要扣掉常驻滚动条，否则第 7 列起会偏到相邻格子。
		vec2 PaletteSwatchCenter(int Index) const
		{
			const CUIRect *pPopup = PopupRect();
			if(pPopup == nullptr)
				return vec2(-1.0e6f, -1.0e6f);
			const float Inset = m_Ui.PopupMenuContentInset() * 0.5f;
			const float ScrollbarWidth = QmScrollContainerStyleForSize(EQmScrollSize::SMALL).m_ScrollbarWidth;
			const float ContentX = pPopup->x + Inset;
			const float ContentW = pPopup->w - 2.0f * Inset - ScrollbarWidth;
			const float CellW = (ContentW - 7.0f * 4.0f) / 8.0f;
			const float ViewportY = pPopup->y + pPopup->h - Inset - (16.0f + 2.0f + 128.0f) + 16.0f + 2.0f;
			const int Slot = Index + 1;
			return {ContentX + (Slot % 8) * (CellW + 4.0f) + CellW * 0.5f, ViewportY + (Slot / 8) * (22.0f + 11.0f + 4.0f) + 11.0f};
		}

		// 预设第一组第一枚：用户行（一行或空）之后是组标题，再一行预设。
		vec2 FirstPresetCenter(int UserSwatchCount) const
		{
			const CUIRect *pPopup = PopupRect();
			if(pPopup == nullptr)
				return vec2(-1.0e6f, -1.0e6f);
			const float Inset = m_Ui.PopupMenuContentInset() * 0.5f;
			const float ScrollbarWidth = QmScrollContainerStyleForSize(EQmScrollSize::SMALL).m_ScrollbarWidth;
			const float ContentX = pPopup->x + Inset;
			const float ContentW = pPopup->w - 2.0f * Inset - ScrollbarWidth;
			const float CellW = (ContentW - 7.0f * 4.0f) / 8.0f;
			const float ViewportY = pPopup->y + pPopup->h - Inset - (16.0f + 2.0f + 128.0f) + 16.0f + 2.0f;
			const int UserRows = (UserSwatchCount + 1 + 7) / 8;
			const float PresetRowY = ViewportY + UserRows * (22.0f + 11.0f + 4.0f) + 14.0f;
			return {ContentX + CellW * 0.5f, PresetRowY + 11.0f};
		}
	};
}

#endif
