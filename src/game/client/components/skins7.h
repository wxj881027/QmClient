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

#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/component.h>
#include <game/client/render.h>

#include <chrono>
#include <deque>
#include <vector>

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
		ColorRGBA m_BloodColor;

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

	class CSkinPartLoadJob : public IJob
	{
	public:
		struct SResult
		{
			CImageInfo m_OriginalImage;
			CImageInfo m_GrayscaleImage;
			ColorRGBA m_BloodColor;
			int m_PartType;
			int m_Flags;
			char m_aName[24];
			bool m_Success = false;
		};

	private:
		std::string m_Path;
		std::string m_PartName;
		IStorage *m_pStorage;
		int m_StorageType;
		[[maybe_unused]] int m_PartType;
		[[maybe_unused]] int m_Flags;
		mutable CLock m_Mutex;
		SResult m_Result;
		bool m_Completed = false;

		void Run() override REQUIRES(!m_Mutex);

	public:
		CSkinPartLoadJob(const char *pPath, const char *pPartName, IStorage *pStorage, int StorageType, int PartType, int Flags);
		~CSkinPartLoadJob() override;

		bool IsCompleted() const REQUIRES(!m_Mutex)
		{
			CLockScope Lock(m_Mutex);
			return m_Completed;
		}

		SResult GetResult() REQUIRES(!m_Mutex)
		{
			CLockScope Lock(m_Mutex);
			SResult Result = std::move(m_Result);
			m_Result = SResult();
			return Result;
		}
	};

	static const char *const ms_apSkinPartNames[protocol7::NUM_SKINPARTS];
	static const char *const ms_apSkinPartNamesLocalized[protocol7::NUM_SKINPARTS];
	static const char *const ms_apColorComponents[NUM_COLOR_COMPONENTS];

	static char *ms_apSkinNameVariables[NUM_DUMMIES];
	static char *ms_apSkinVariables[NUM_DUMMIES][protocol7::NUM_SKINPARTS];
	static int *ms_apUCCVariables[NUM_DUMMIES][protocol7::NUM_SKINPARTS]; // use custom color
	static unsigned *ms_apColorVariables[NUM_DUMMIES][protocol7::NUM_SKINPARTS];

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;

	void Refresh(TSkinLoadedCallback &&SkinLoadedCallback);
	std::chrono::nanoseconds LastRefreshTime() const { return m_LastRefreshTime; }
	bool IsLoading() const { return m_Loading; }

	const std::vector<CSkin> &GetSkins() const;
	const std::vector<CSkinPart> &GetSkinParts(int Part) const;
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
	bool RemoveSkin(const CSkin *pSkin);

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
	TSkinLoadedCallback m_SkinLoadedCallback;

	IGraphics::CTextureHandle m_XmasHatTexture;
	IGraphics::CTextureHandle m_BotTexture;

	static int SkinPartScan(const char *pName, int IsDir, int DirType, void *pUser);
	bool LoadSkinPart(int PartType, const char *pName, int DirType);
	void StartSkinPartLoadJob(int PartType, const char *pName, int DirType);
	void ProcessCompletedJobs();
	static int SkinScan(const char *pName, int IsDir, int DirType, void *pUser);
	bool LoadSkin(const char *pName, int DirType);

	void InitPlaceholderSkinParts();
	void LoadXmasHat();
	void LoadBotDecoration();

	void AddSkinFromConfigVariables(const char *pName, int Dummy);
};

#endif
