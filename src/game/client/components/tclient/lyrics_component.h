#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_LYRICS_COMPONENT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_LYRICS_COMPONENT_H

#include <game/client/component.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CHttpRequest;
typedef struct _json_value json_value;

class CLyrics : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnUpdate() override;

	bool HasLyrics() const { return m_State == EState::READY; }
	bool GetCurrentLine(char *pBuf, size_t BufSize, int64_t PositionMs) const;

private:
	struct CLyricLine
	{
		int64_t m_TimeMs = 0;
		std::string m_Text;
	};

	struct STrackInfo
	{
		char m_aTitle[128] = {};
		char m_aArtist[128] = {};
		char m_aAlbum[128] = {};
		int m_DurationSec = 0;
	};

	enum class EState
	{
		IDLE,
		REQUESTING,
		READY,
		ERROR,
	};

	enum class EEndpoint
	{
		CACHED,
		FULL,
		SEARCH,
	};

	static const char *EndpointName(EEndpoint Endpoint);
	void ResetState();
	void ClearLyrics();
	void CancelRequest();
	void StartRequest(EEndpoint Endpoint);
	void HandleRequestDone();
	bool ParseLyricsResponse(const json_value *pObj, char *pErr, size_t ErrSize);
	bool ParseSearchResponse(const json_value *pObj, char *pErr, size_t ErrSize);
	bool ApplyLyrics(const std::string &Plain, const std::string &Synced, bool Instrumental, char *pErr, size_t ErrSize);
	bool ParseSyncedLyrics(const std::string &Text, std::vector<CLyricLine> &OutLines, char *pErr, size_t ErrSize) const;
	std::string BuildSignature(const STrackInfo &Info) const;
	bool IsValidTrack(const STrackInfo &Info) const;

	std::shared_ptr<CHttpRequest> m_pRequest;
	EEndpoint m_RequestEndpoint = EEndpoint::CACHED;
	EState m_State = EState::IDLE;
	bool m_IsSynced = false;

	STrackInfo m_Track;
	std::string m_LastSignature;
	std::string m_RequestSignature;
	std::vector<CLyricLine> m_Lines;
	std::string m_PlainLine;
	char m_aLastError[128] = {};
};

#endif
