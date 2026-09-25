#ifndef GAME_CLIENT_SKIN_H
#define GAME_CLIENT_SKIN_H

#include <base/color.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <memory>

class CQmSkinOutline;

namespace QmChatAvatar
{
	struct SSource;
}

// do this better and nicer
class CSkin
{
	char m_aName[MAX_SKIN_LENGTH];

public:
	class CSkinTextures
	{
	public:
		IGraphics::CTextureHandle m_Body;
		IGraphics::CTextureHandle m_BodyOutline;

		IGraphics::CTextureHandle m_Feet;
		IGraphics::CTextureHandle m_FeetOutline;
		std::shared_ptr<CQmSkinOutline> m_QmBodyOutline;
		std::shared_ptr<CQmSkinOutline> m_QmFeetOutline;
		// 聊天导出的头像素材：加载时从皮肤图缩好的 CPU 副本，
		// 旧消息不依赖纹理句柄或皮肤缓存存活。
		std::shared_ptr<const QmChatAvatar::SSource> m_QmChatAvatar;

		std::shared_ptr<const QmChatAvatar::SSource> m_pChatAvatar;

		std::shared_ptr<CQmSkinOutline> m_pBodyOutline;
		std::shared_ptr<CQmSkinOutline> m_pFeetOutline;

		IGraphics::CTextureHandle m_Hands;
		IGraphics::CTextureHandle m_HandsOutline;

		IGraphics::CTextureHandle m_aEyes[6];

		void Reset();
		void Unload(IGraphics *pGraphics);
	};

	CSkinTextures m_OriginalSkin;
	CSkinTextures m_ColorableSkin;
	ColorRGBA m_BloodColor;

	class CSkinMetricVariableInt
	{
	public:
		int m_Value;

		operator int() const;
		CSkinMetricVariableInt &operator=(int NewVal);
		CSkinMetricVariableInt();
		void Reset();
	};

	class CSkinMetricVariableSize
	{
	public:
		int m_Value;

		operator int() const;
		CSkinMetricVariableSize &operator=(int NewVal);
		CSkinMetricVariableSize();
		void Reset();
	};

	class CSkinMetricVariable
	{
	public:
		CSkinMetricVariableSize m_Width;
		CSkinMetricVariableSize m_Height;
		CSkinMetricVariableInt m_OffsetX;
		CSkinMetricVariableInt m_OffsetY;

		// these can be used to normalize the metrics
		CSkinMetricVariableSize m_MaxWidth;
		CSkinMetricVariableSize m_MaxHeight;

		float WidthNormalized() const;
		float HeightNormalized() const;
		float OffsetXNormalized() const;
		float OffsetYNormalized() const;
		void Reset();
	};

	class CSkinMetrics
	{
	public:
		CSkinMetricVariable m_Body;
		CSkinMetricVariable m_Feet;

		CSkinMetrics();
		void Reset();
	};
	CSkinMetrics m_Metrics;

	bool operator<(const CSkin &Other) const;
	bool operator==(const CSkin &Other) const;

	CSkin(const char *pName);
	CSkin(CSkin &&) = default;
	CSkin &operator=(CSkin &&) = default;

	const char *GetName() const { return m_aName; }

	static bool IsValidName(const char *pName);
	static const char m_aSkinNameRestrictions[];
};

#endif
