/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SKINS7_H
#define GAME_CLIENT_COMPONENTS_SKINS7_H

#include <base/color.h>
#include <base/lock.h>
#include <base/vmath.h>

#include <engine/client/enums.h>
#include <engine/graphics.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/qm_chat_avatar.h>
#include <game/client/components/qmclient/skin7_part_load_job.h>
#include <game/client/components/qmclient/skin7_part_publisher.h>
#include <game/client/components/qmclient/skin7_refresh_request.h>
#include <game/client/components/qmclient/skin7_resource_lifecycle.h>
#include <game/client/render.h>

#include <chrono>
#include <deque>
#include <vector>

class CQmSkin7InfoJob;

class CSkins7 : public CComponent
{
public:
	enum
	{
		SKINFLAG_SPECIAL = 1 << 0,
		SKINFLAG_STANDARD = 1 << 1,

		NUM_COLOR_COMPONENTS = 4,

		HAT_NUM = 2,
		HAT_OFFSET_SIDE = 2,
	};

	typedef std::function<void()> TSkinLoadedCallback;

	class CSkinPart
	{
	public:
		int m_Type;
		int m_Flags;
		char m_aName[24];
		IGraphics::CTextureHandle m_OriginalTexture;
		IGraphics::CTextureHandle m_ColorableTexture;
		// 部件描边：与纹理句柄解耦的 CPU 遮罩，句柄失效时按需重建；卸载部件时一并释放。
		std::shared_ptr<CQmSkinOutline> m_QmSkinOutline;
		ColorRGBA m_BloodColor;
		// 聊天导出的部件头像素材：加载时缩好的 CPU 副本，与纹理句柄解耦。
		std::shared_ptr<const QmChatAvatar::SSource> m_pChatAvatarOriginal;
		std::shared_ptr<const QmChatAvatar::SSource> m_pChatAvatarColorable;

		void ApplyTo(CTeeRenderInfo::CSixup &SixupRenderInfo) const;

		bool operator<(const CSkinPart &Other) const;
	};

	class CSkin
	{
	public:
		int m_Flags;
		char m_aName[24];
		const CSkinPart *m_apParts[protocol7::NUM_SKINPARTS];
		int m_aUseCustomColors[protocol7::NUM_SKINPARTS];
		unsigned m_aPartColors[protocol7::NUM_SKINPARTS];

		bool operator<(const CSkin &Other) const;
		bool operator==(const CSkin &Other) const;
	};

	using CSkinPartLoadJob = CQmSkin7PartLoadJob;

	static const char *const ms_apSkinPartNames[protocol7::NUM_SKINPARTS];
	static const char *const ms_apSkinPartNamesLocalized[protocol7::NUM_SKINPARTS];
	static const char *const ms_apColorComponents[NUM_COLOR_COMPONENTS];

	static char *ms_apSkinNameVariables[NUM_DUMMIES];
	static char *ms_apSkinVariables[NUM_DUMMIES][protocol7::NUM_SKINPARTS];
	static int *ms_apUCCVariables[NUM_DUMMIES][protocol7::NUM_SKINPARTS]; // use custom color
	static unsigned *ms_apColorVariables[NUM_DUMMIES][protocol7::NUM_SKINPARTS];

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnUpdate() override;
	void OnReset() override;

	void Refresh(TSkinLoadedCallback &&SkinLoadedCallback);
	std::chrono::nanoseconds LastRefreshTime() const { return m_LastRefreshTime; }
	bool IsLoading() const { return m_Loading; }

	const std::vector<CSkin> &GetSkins() const;
	const std::vector<CSkinPart> &GetSkinParts(int Part) const;
	const CSkin *FindSkin(const char *pName, bool AllowSpecialSkin) const;
	const CSkinPart *FindSkinPartOrNullptr(int Part, const char *pName, bool AllowSpecialPart) const;
	const CSkinPart *FindDefaultSkinPart(int Part) const;
	const CSkinPart *FindSkinPart(int Part, const char *pName, bool AllowSpecialPart) const;
	void RandomizeSkin(int Dummy) const;

	ColorRGBA GetColor(int Value, bool UseAlpha) const;
	void ApplyColorTo(CTeeRenderInfo::CSixup &SixupRenderInfo, bool UseCustomColors, int Value, int Part) const;
	ColorRGBA GetTeamColor(int UseCustomColors, int PartColor, int Team, int Part) const;

	// returns true if everything was valid and nothing changed
	bool ValidateSkinParts(char *apPartNames[protocol7::NUM_SKINPARTS], int *pUseCustomColors, int *pPartColors, int GameFlags) const;

	bool SaveSkinfile(const char *pName, int Dummy);
	bool RemoveSkin(const char *pName);

	IGraphics::CTextureHandle XmasHatTexture() const { return m_XmasHatTexture; }
	IGraphics::CTextureHandle BotDecorationTexture() const { return m_BotTexture; }

	static bool IsSpecialSkin(const char *pName);

private:
	std::chrono::nanoseconds m_LastRefreshTime;
	bool m_Loading = false;

	std::vector<CSkinPart> m_avSkinParts[protocol7::NUM_SKINPARTS];
	CSkinPart m_aPlaceholderSkinParts[protocol7::NUM_SKINPARTS];
	std::vector<CSkin> m_vSkins;

	std::deque<std::shared_ptr<CSkinPartLoadJob>> m_PendingSkinPartJobs;
	std::shared_ptr<CQmSkin7InfoJob> m_pSkinInfoJob;
	size_t m_NextPart = 0;
	size_t m_NextSkin = 0;
	int m_NextAccessory = 0;
	CQmSkin7ResourceLifecycle m_ResourceLifecycle;
	CQmSkin7PartPublisher m_PartPublisher;
	CQmSkin7RefreshRequest<CSkinPart> m_RefreshRequest;
	TSkinLoadedCallback m_SkinLoadedCallback;

	IGraphics::CTextureHandle m_XmasHatTexture;
	IGraphics::CTextureHandle m_BotTexture;

	std::shared_ptr<CSkinPartLoadJob> CreateSkinPartLoadJob(int PartType, const char *pName, int DirType);
	void ProcessCompletedJobs();
	void CancelPendingLoads();
	void TryStartRefresh();
	bool RebuildSkins();
	bool LoadSkin(const char *pName, int DirType, const json_value &Info);

	void InitPlaceholderSkinParts();

	void PrepareSkinFromConfigVariables(std::vector<CSkin> &Skins, const char *pName, int Dummy) const;
};

#endif
