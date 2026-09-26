#ifndef GAME_CLIENT_COMPONENTS_MENU_BACKGROUND_H
#define GAME_CLIENT_COMPONENTS_MENU_BACKGROUND_H

#include <game/client/components/background.h>
#include <game/client/components/camera.h>

#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CMenuMap : public CBackgroundEngineMap
{
	MACRO_INTERFACE("menu_enginemap")
};

// themes
class CTheme
{
public:
	CTheme(const char *pName, bool HasDay, bool HasNight) :
		m_Name(pName), m_HasDay(HasDay), m_HasNight(HasNight) {}

	std::string m_Name;
	bool m_HasDay;
	bool m_HasNight;
	IGraphics::CTextureHandle m_IconTexture;
	std::string m_IconPath;
	bool m_IconLoadRequested = false;
	bool m_IconLoadFailed = false;
	bool operator<(const CTheme &Other) const { return m_Name < Other.m_Name; }
};

class CMenuBackground : public CBackground
{
	std::chrono::nanoseconds m_ThemeScanStartTime{0};
	class CThemeListLoadJob;
	class CThemeIconLoadJob;

public:
	enum
	{
		POS_START = 0,
		POS_DEMOS,
		POS_NEWS,
		POS_SETTINGS_LANGUAGE,
		POS_SETTINGS_GENERAL,
		POS_SETTINGS_PLAYER,
		POS_SETTINGS_TEE,
		POS_SETTINGS_APPEARANCE,
		POS_SETTINGS_CONTROLS,
		POS_SETTINGS_GRAPHICS,
		POS_SETTINGS_SOUND,
		POS_SETTINGS_DDNET,
		POS_SETTINGS_ASSETS,
		POS_SETTINGS_RESERVED0,
		POS_SETTINGS_RESERVED1,
		POS_BROWSER_INTERNET,
		POS_BROWSER_LAN,
		POS_BROWSER_FAVORITES,
		POS_BROWSER_CUSTOM0,
		POS_BROWSER_CUSTOM1,
		POS_BROWSER_CUSTOM2,
		POS_BROWSER_CUSTOM3,
		POS_BROWSER_CUSTOM4,
		POS_RESERVED0,
		POS_RESERVED1,
		POS_RESERVED2,

		NUM_POS,
	};

	enum
	{
		POS_BROWSER_CUSTOM_NUM = (POS_BROWSER_CUSTOM4 - POS_BROWSER_CUSTOM0) + 1,
		POS_SETTINGS_RESERVED_NUM = (POS_SETTINGS_RESERVED1 - POS_SETTINGS_RESERVED0) + 1,
		POS_RESERVED_NUM = (POS_RESERVED2 - POS_RESERVED0) + 1,
	};

	enum
	{
		PREDEFINED_THEMES_COUNT = 3,
	};

private:
	struct SPreviousBackground
	{
		std::unique_ptr<CMenuMap> m_pMap;
		std::unique_ptr<CLayers> m_pLayers;
		std::unique_ptr<CMapImages> m_pImages;
		std::unique_ptr<CMapLayers> m_pRenderer;
	};

	CCamera m_Camera;

protected:
	CBackgroundEngineMap *CreateBGMap() override;

private:
	vec2 m_RotationCenter;
	std::array<vec2, NUM_POS> m_aPositions;
	int m_CurrentPosition;
	vec2 m_CurrentDirection = vec2(1.0f, 0.0f);
	vec2 m_AnimationStartPos;
	bool m_ChangedPosition;
	float m_MoveTime;
	// 菜单相机上一次推进的时间戳。相机动画不能直接依赖 Client()->RenderFrameTime()：
	// 启动阶段（CClient::Run() 主循环之前的 GameClient()->OnInit()）里那个值恒为
	// client.h 的初值 0.0001f，会让旋转量小到看不见。详见 Render()。
	std::chrono::nanoseconds m_LastCameraFrameTime{0};

	bool m_IsInit;
	bool m_Loading;
	std::unique_ptr<SPreviousBackground> m_pPreviousBackground;

	void ResetPositions();

	void EnsureThemeEntries();
	void EnsureThemeListJob();
	void ProcessThemeListJob();
	void QueueNextThemeIconLoad();
	void QueueThemeIconLoad(CTheme &Theme);
	void ProcessThemeIconJobs();
	void UpdateThemeLoading();
	void InvalidateCurrentPosition();
	void PreserveCurrentBackground();
	void ReleasePreviousBackground();
	void InitializeLoadedMap();

	std::vector<CTheme> m_vThemes;
	std::shared_ptr<CThemeListLoadJob> m_pThemeListLoadJob;
	std::vector<std::shared_ptr<CThemeIconLoadJob>> m_vThemeIconLoadJobs;
	bool m_ThemeListLoaded = false;

public:
	CMenuBackground();
	int Sizeof() const override { return sizeof(*this); }

	void OnInterfacesInit(CGameClient *pClient) override;
	void OnInit() override;
	void OnMapLoad() override;
	void OnRender() override;

	void LoadMenuBackground(bool HasDayHint = true, bool HasNightHint = true);

	bool Render();
	bool IsLoading() const { return m_Loading; }
	// 推进分帧的图层初始化。加载完成时自行清除 m_Loading。
	// 调用方在 IsLoading() 期间跳过呈现前必须先调用它，否则加载永远走不完。
	void AdvanceLoading();

	class CCamera *GetCurCamera() override;

	void ChangePosition(int PositionNumber);

	void RefreshThemes();
	std::vector<CTheme> &GetThemes();
};

std::array<vec2, CMenuBackground::NUM_POS> GenerateMenuBackgroundPositions();

#endif
