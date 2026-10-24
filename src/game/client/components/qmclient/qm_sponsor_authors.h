#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_SPONSOR_AUTHORS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_SPONSOR_AUTHORS_H

#include <base/color.h>
#include <base/vmath.h>

#include <game/client/ui_rect.h>

#include <array>

namespace QmSponsorAuthors
{
	struct SAuthor
	{
		const char *m_pTextId;
		const char *m_pName;
		// 作者指定皮肤，缺失时回退到 default。
		const char *m_pSkin;
		// 是否用自定义颜色渲染（可着色皮肤如 default_v2 需要显式上色）。
		bool m_CustomColors;
		// 仅 m_CustomColors 为 true 时生效；ColorRGBA()（alpha=0）表示未指定。
		ColorRGBA m_BodyColor;
		ColorRGBA m_FeetColor;
		bool m_Glitch = false;
	};

	struct SNamePresentation
	{
		const char *m_pText;
		float m_Blur;
		bool m_Redacted;
	};

	struct STeePresentation
	{
		vec2 m_Offset = vec2(0.0f, 0.0f);
		float m_Scale = 1.0f;
	};

	const std::array<SAuthor, 4> &Authors();
	float RowsHeight(float TeeSize, float LineSpacing, float LabelHeight);
	SNamePresentation NamePresentation(const SAuthor &Author, const CUIRect &NameBounds, const CUIRect &RedactedBounds, vec2 Mouse, bool PointerActive, bool WasRedacted);
	STeePresentation TeePresentation(const SAuthor &Author, double TimeSec, int MotionLevel);
}

#endif
