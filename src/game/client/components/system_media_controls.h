#ifndef GAME_CLIENT_COMPONENTS_SYSTEM_MEDIA_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_SYSTEM_MEDIA_CONTROLS_H

#include <game/client/component.h>

#include <engine/graphics.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

class CSystemMediaControls : public CComponent
{
public:
	struct SState
	{
		bool m_CanPlay = false;
		bool m_CanPause = false;
		bool m_CanPrev = false;
		bool m_CanNext = false;
		bool m_Playing = false;
		char m_aTitle[128] = {};
		char m_aArtist[128] = {};
		char m_aAlbum[128] = {};
		int64_t m_PositionMs = 0;
		int64_t m_DurationMs = 0;
		IGraphics::CTextureHandle m_AlbumArt;
		int m_AlbumArtWidth = 0;
		int m_AlbumArtHeight = 0;
	};

#if defined(CONF_FAMILY_WINDOWS)
	struct SWinrt;
	struct SShared;
#endif

	CSystemMediaControls();
	~CSystemMediaControls() override;
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnUpdate() override;

	bool GetStateSnapshot(SState &State) const;
	void Previous();
	void PlayPause();
	void Next();

private:
#if defined(CONF_FAMILY_WINDOWS)
	std::unique_ptr<SWinrt> m_pWinrt;
	std::unique_ptr<SShared> m_pShared;
	std::thread m_Thread;
	std::atomic_bool m_StopThread{false};

	void ThreadMain();
#endif

};

#endif
