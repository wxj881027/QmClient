#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_QM_MOD_WEAPON_TEXT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_QM_MOD_WEAPON_TEXT_H

#include <engine/textrender.h>

#include <optional>

// 返回已绘制文字的高度；创建时的临时标志不泄漏到绘制和后续组件。
template<typename TTextRender>
std::optional<float> QmRenderModWeaponText(TTextRender &Text, const char *pText, float FontSize, vec2 Position, ColorRGBA Color)
{
	STextContainerIndex Container;
	std::optional<float> Height;
	try
	{
		{
			struct SRestoreFlags
			{
				TTextRender &m_Text;
				unsigned m_Previous;
				~SRestoreFlags() { m_Text.SetRenderFlags(m_Previous); }
			};
			const SRestoreFlags Restore{Text, Text.GetRenderFlags()};
			CTextCursor Cursor;
			Cursor.m_FontSize = FontSize;
			Text.SetRenderFlags(TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | TEXT_RENDER_FLAG_ONE_TIME_USE);
			Text.CreateTextContainer(Container, &Cursor, pText);
		}
		if(Container.Valid())
		{
			const auto BoundingBox = Text.GetBoundingBoxTextContainer(Container);
			Text.RenderTextContainer(Container, Color, Text.DefaultTextOutlineColor(), Position.x - BoundingBox.m_W / 2.0f, Position.y);
			Height = BoundingBox.m_H;
		}
		Text.DeleteTextContainer(Container);
	}
	catch(...)
	{
		// 只转交现有独占槽位；展开期间不申请内存，也不再次提交设备删除。
		Text.ReleaseTemporaryTextContainer(Container);
		throw;
	}
	return Height;
}

#endif
