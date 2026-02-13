#include "lyrics_component.h"

#include <base/log.h>
#include <base/str.h>

#include <engine/shared/http.h>
#include <engine/shared/json.h>

#include <game/client/components/system_media_controls.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

static void TrimString(std::string &Str)
{
	while(!Str.empty() && isspace((unsigned char)Str.back()))
		Str.pop_back();
	size_t Start = 0;
	while(Start < Str.size() && isspace((unsigned char)Str[Start]))
		Start++;
	if(Start > 0)
		Str.erase(0, Start);
}

static bool ParseTimestampMs(const char *pStart, const char *pEnd, int64_t &OutMs)
{
	if(pStart >= pEnd || !isdigit((unsigned char)*pStart))
		return false;
	int Minutes = 0;
	const char *p = pStart;
	while(p < pEnd && isdigit((unsigned char)*p))
	{
		Minutes = Minutes * 10 + (*p - '0');
		p++;
	}
	if(p >= pEnd || *p != ':')
		return false;
	p++;
	if(p >= pEnd || !isdigit((unsigned char)*p))
		return false;
	int Seconds = 0;
	while(p < pEnd && isdigit((unsigned char)*p))
	{
		Seconds = Seconds * 10 + (*p - '0');
		p++;
	}
	int Milli = 0;
	if(p < pEnd && (*p == '.' || *p == ','))
	{
		p++;
		int Fraction = 0;
		int Digits = 0;
		while(p < pEnd && isdigit((unsigned char)*p) && Digits < 3)
		{
			Fraction = Fraction * 10 + (*p - '0');
			p++;
			Digits++;
		}
		if(Digits == 1)
			Milli = Fraction * 100;
		else if(Digits == 2)
			Milli = Fraction * 10;
		else
			Milli = Fraction;
	}
	OutMs = (int64_t)Minutes * 60000 + (int64_t)Seconds * 1000 + Milli;
	return true;
}

const char *CLyrics::EndpointName(EEndpoint Endpoint)
{
	switch(Endpoint)
	{
	case EEndpoint::CACHED:
		return "get-cached";
	case EEndpoint::FULL:
		return "get";
	case EEndpoint::SEARCH:
		return "search";
	}
	return "unknown";
}

void CLyrics::OnInit()
{
	ResetState();
}

void CLyrics::OnShutdown()
{
	CancelRequest();
	ClearLyrics();
	m_LastSignature.clear();
}

void CLyrics::ResetState()
{
	CancelRequest();
	ClearLyrics();
	m_LastSignature.clear();
	m_RequestSignature.clear();
}

void CLyrics::ClearLyrics()
{
	m_State = EState::IDLE;
	m_IsSynced = false;
	m_Lines.clear();
	m_PlainLine.clear();
	m_aLastError[0] = '\0';
}

void CLyrics::CancelRequest()
{
	if(m_pRequest)
	{
		m_pRequest->Abort();
		m_pRequest.reset();
	}
}

bool CLyrics::IsValidTrack(const STrackInfo &Info) const
{
	return Info.m_aTitle[0] != '\0' && Info.m_aArtist[0] != '\0';
}

std::string CLyrics::BuildSignature(const STrackInfo &Info) const
{
	std::string Key;
	Key.reserve(512);
	Key.append(Info.m_aTitle);
	Key.push_back('\n');
	Key.append(Info.m_aArtist);
	Key.push_back('\n');
	Key.append(Info.m_aAlbum);
	return Key;
}

void CLyrics::StartRequest(EEndpoint Endpoint)
{
	char aTitle[256];
	char aArtist[256];
	char aAlbum[256];
	EscapeUrl(aTitle, sizeof(aTitle), m_Track.m_aTitle);
	EscapeUrl(aArtist, sizeof(aArtist), m_Track.m_aArtist);
	EscapeUrl(aAlbum, sizeof(aAlbum), m_Track.m_aAlbum);

	char aUrl[1024];
	if(Endpoint == EEndpoint::SEARCH)
	{
		str_format(aUrl, sizeof(aUrl),
			"https://lrclib.net/api/search?track_name=%s&artist_name=%s",
			aTitle, aArtist);
	}
	else
	{
		const char *pEndpoint = Endpoint == EEndpoint::CACHED ? "get-cached" : "get";
		str_format(aUrl, sizeof(aUrl),
			"https://lrclib.net/api/%s?track_name=%s&artist_name=%s&album_name=%s&duration=%d",
			pEndpoint, aTitle, aArtist, aAlbum, m_Track.m_DurationSec);
	}

	auto pRequest = std::make_shared<CHttpRequest>(aUrl);
	pRequest->LogProgress(HTTPLOG::FAILURE);
	pRequest->FailOnErrorStatus(false);
	pRequest->Timeout(CTimeout{10000, 0, 500, 10});
	pRequest->HeaderString("User-Agent", "QimenGClient");
	pRequest->HeaderString("Accept", "application/json");

	m_RequestEndpoint = Endpoint;
	m_RequestSignature = m_LastSignature;
	m_pRequest = pRequest;
	m_State = EState::REQUESTING;

	Http()->Run(pRequest);
	log_info("lyrics", "request start endpoint=%s title='%s' artist='%s' album='%s' duration=%d",
		EndpointName(Endpoint), m_Track.m_aTitle, m_Track.m_aArtist, m_Track.m_aAlbum, m_Track.m_DurationSec);
}

void CLyrics::HandleRequestDone()
{
	std::shared_ptr<CHttpRequest> pRequest = m_pRequest;
	m_pRequest.reset();

	if(m_RequestSignature != m_LastSignature)
		return;

	if(pRequest->State() == EHttpState::ABORTED)
	{
		str_copy(m_aLastError, "Aborted", sizeof(m_aLastError));
		m_State = EState::ERROR;
		return;
	}
	if(pRequest->State() != EHttpState::DONE)
	{
		str_copy(m_aLastError, "Curl error", sizeof(m_aLastError));
		m_State = EState::ERROR;
		return;
	}

	const int StatusCode = pRequest->StatusCode();
	if(StatusCode == 404 && m_RequestEndpoint == EEndpoint::CACHED)
	{
		StartRequest(EEndpoint::FULL);
		return;
	}
	if(StatusCode != 200)
	{
		str_format(m_aLastError, sizeof(m_aLastError), "HTTP %d", StatusCode);
		m_State = EState::ERROR;
		log_error("lyrics", "request fail endpoint=%s http=%d", EndpointName(m_RequestEndpoint), StatusCode);
		return;
	}

	const json_value *pObj = pRequest->ResultJson();
	const bool Ok = m_RequestEndpoint == EEndpoint::SEARCH ?
		ParseSearchResponse(pObj, m_aLastError, sizeof(m_aLastError)) :
		ParseLyricsResponse(pObj, m_aLastError, sizeof(m_aLastError));
	if(!Ok)
	{
		m_State = EState::ERROR;
		log_error("lyrics", "request fail endpoint=%s error='%s'", EndpointName(m_RequestEndpoint), m_aLastError);
		return;
	}

	m_State = EState::READY;
	log_info("lyrics", "request ok endpoint=%s lines=%zu synced=%d",
		EndpointName(m_RequestEndpoint), m_Lines.size(), m_IsSynced ? 1 : 0);
}

bool CLyrics::ParseSyncedLyrics(const std::string &Text, std::vector<CLyricLine> &OutLines, char *pErr, size_t ErrSize) const
{
	OutLines.clear();
	const char *p = Text.c_str();
	const char *pEnd = p + Text.size();
	while(p < pEnd)
	{
		const char *pLineEnd = (const char *)memchr(p, '\n', pEnd - p);
		if(!pLineEnd)
			pLineEnd = pEnd;

		std::string Line(p, pLineEnd);
		if(!Line.empty() && Line.back() == '\r')
			Line.pop_back();

		std::vector<int64_t> Times;
		size_t Pos = 0;
		while(Pos < Line.size() && Line[Pos] == '[')
		{
			const size_t Close = Line.find(']', Pos);
			if(Close == std::string::npos)
				break;
			int64_t TimeMs = 0;
			if(ParseTimestampMs(Line.c_str() + Pos + 1, Line.c_str() + Close, TimeMs))
				Times.push_back(TimeMs);
			Pos = Close + 1;
		}

		std::string TextPart = Line.substr(Pos);
		TrimString(TextPart);
		if(!Times.empty() && !TextPart.empty())
		{
			for(int64_t TimeMs : Times)
			{
				CLyricLine LineEntry;
				LineEntry.m_TimeMs = TimeMs;
				LineEntry.m_Text = TextPart;
				OutLines.push_back(std::move(LineEntry));
			}
		}

		p = pLineEnd + (pLineEnd < pEnd ? 1 : 0);
	}

	if(OutLines.empty())
	{
		str_copy(pErr, "No synced lyrics", ErrSize);
		return false;
	}

	std::sort(OutLines.begin(), OutLines.end(), [](const CLyricLine &A, const CLyricLine &B) {
		if(A.m_TimeMs != B.m_TimeMs)
			return A.m_TimeMs < B.m_TimeMs;
		return A.m_Text < B.m_Text;
	});

	return true;
}

bool CLyrics::ApplyLyrics(const std::string &Plain, const std::string &Synced, bool Instrumental, char *pErr, size_t ErrSize)
{
	if(Instrumental || (Plain.empty() && Synced.empty()))
	{
		str_copy(pErr, "No lyric in response", ErrSize);
		return false;
	}

	if(!Synced.empty())
	{
		char aParseErr[128];
		if(ParseSyncedLyrics(Synced, m_Lines, aParseErr, sizeof(aParseErr)))
		{
			m_IsSynced = true;
			return true;
		}
	}

	if(!Plain.empty())
	{
		const char *pLineStart = Plain.c_str();
		while(*pLineStart)
		{
			const char *pLineEnd = str_find(pLineStart, "\n");
			if(!pLineEnd)
				pLineEnd = pLineStart + str_length(pLineStart);
			std::string Line(pLineStart, pLineEnd);
			if(!Line.empty() && Line.back() == '\r')
				Line.pop_back();
			TrimString(Line);
			if(!Line.empty())
			{
				m_PlainLine = Line;
				return true;
			}
			pLineStart = *pLineEnd ? pLineEnd + 1 : pLineEnd;
		}
	}

	str_copy(pErr, "No usable lyric", ErrSize);
	return false;
}

bool CLyrics::ParseLyricsResponse(const json_value *pObj, char *pErr, size_t ErrSize)
{
	ClearLyrics();
	if(!pObj || pObj->type != json_object)
	{
		str_copy(pErr, "Response is not object", ErrSize);
		return false;
	}

	const json_value *pInstr = json_object_get(pObj, "instrumental");
	const bool Instrumental = pInstr != &json_value_none && pInstr->type == json_boolean ? json_boolean_get(pInstr) != 0 : false;
	std::string Plain;
	std::string Synced;
	const json_value *pPlain = json_object_get(pObj, "plainLyrics");
	if(pPlain != &json_value_none && pPlain->type == json_string)
		Plain = json_string_get(pPlain);
	const json_value *pSynced = json_object_get(pObj, "syncedLyrics");
	if(pSynced != &json_value_none && pSynced->type == json_string)
		Synced = json_string_get(pSynced);

	return ApplyLyrics(Plain, Synced, Instrumental, pErr, ErrSize);
}

bool CLyrics::ParseSearchResponse(const json_value *pObj, char *pErr, size_t ErrSize)
{
	ClearLyrics();
	if(!pObj || pObj->type != json_array)
	{
		str_copy(pErr, "Response is not array", ErrSize);
		return false;
	}

	struct SCandidate
	{
		std::string m_Track;
		std::string m_Artist;
		std::string m_Album;
		std::string m_Plain;
		std::string m_Synced;
		bool m_Instrumental = false;
	};

	const int Count = json_array_length(pObj);
	if(Count <= 0)
	{
		str_copy(pErr, "No matching lyrics", ErrSize);
		return false;
	}

	SCandidate Best;
	bool HasBest = false;
	int BestScore = -1000000;

	for(int i = 0; i < Count; ++i)
	{
		const json_value *pItem = json_array_get(pObj, i);
		if(!pItem || pItem->type != json_object)
			continue;

		SCandidate Candidate;
		const json_value *pTrack = json_object_get(pItem, "trackName");
		if(pTrack != &json_value_none && pTrack->type == json_string)
			Candidate.m_Track = json_string_get(pTrack);
		const json_value *pArtist = json_object_get(pItem, "artistName");
		if(pArtist != &json_value_none && pArtist->type == json_string)
			Candidate.m_Artist = json_string_get(pArtist);
		const json_value *pAlbum = json_object_get(pItem, "albumName");
		if(pAlbum != &json_value_none && pAlbum->type == json_string)
			Candidate.m_Album = json_string_get(pAlbum);
		const json_value *pInstr = json_object_get(pItem, "instrumental");
		if(pInstr != &json_value_none && pInstr->type == json_boolean)
			Candidate.m_Instrumental = json_boolean_get(pInstr) != 0;
		const json_value *pPlain = json_object_get(pItem, "plainLyrics");
		if(pPlain != &json_value_none && pPlain->type == json_string)
			Candidate.m_Plain = json_string_get(pPlain);
		const json_value *pSynced = json_object_get(pItem, "syncedLyrics");
		if(pSynced != &json_value_none && pSynced->type == json_string)
			Candidate.m_Synced = json_string_get(pSynced);

		if(Candidate.m_Instrumental || (Candidate.m_Plain.empty() && Candidate.m_Synced.empty()))
			continue;

		int Score = 0;
		if(!Candidate.m_Track.empty() && str_comp_nocase(Candidate.m_Track.c_str(), m_Track.m_aTitle) == 0)
			Score += 1000;
		if(!Candidate.m_Artist.empty() && str_comp_nocase(Candidate.m_Artist.c_str(), m_Track.m_aArtist) == 0)
			Score += 800;
		if(m_Track.m_aAlbum[0] != '\0' && !Candidate.m_Album.empty() &&
			str_comp_nocase(Candidate.m_Album.c_str(), m_Track.m_aAlbum) == 0)
			Score += 200;

		if(!Candidate.m_Synced.empty())
			Score += 50;
		else if(!Candidate.m_Plain.empty())
			Score += 10;

		if(!HasBest || Score > BestScore)
		{
			Best = std::move(Candidate);
			BestScore = Score;
			HasBest = true;
		}
	}

	if(!HasBest)
	{
		str_copy(pErr, "No matching lyrics", ErrSize);
		return false;
	}

	return ApplyLyrics(Best.m_Plain, Best.m_Synced, Best.m_Instrumental, pErr, ErrSize);
}

void CLyrics::OnUpdate()
{
	CSystemMediaControls::SState MediaState;
	if(!GameClient()->m_SystemMediaControls.GetStateSnapshot(MediaState))
	{
		if(!m_LastSignature.empty())
			ResetState();
		return;
	}

	STrackInfo CurrentTrack;
	str_copy(CurrentTrack.m_aTitle, MediaState.m_aTitle, sizeof(CurrentTrack.m_aTitle));
	str_copy(CurrentTrack.m_aArtist, MediaState.m_aArtist, sizeof(CurrentTrack.m_aArtist));
	str_copy(CurrentTrack.m_aAlbum, MediaState.m_aAlbum, sizeof(CurrentTrack.m_aAlbum));
	CurrentTrack.m_DurationSec = (int)((MediaState.m_DurationMs + 500) / 1000);

	if(!IsValidTrack(CurrentTrack))
	{
		if(!m_LastSignature.empty())
			ResetState();
		return;
	}

	const std::string Signature = BuildSignature(CurrentTrack);
	if(Signature != m_LastSignature)
	{
		CancelRequest();
		ClearLyrics();
		m_Track = CurrentTrack;
		m_LastSignature = Signature;
		StartRequest(EEndpoint::SEARCH);
		return;
	}

	if(!m_pRequest)
		return;

	if(m_pRequest->State() == EHttpState::RUNNING || m_pRequest->State() == EHttpState::QUEUED)
		return;

	HandleRequestDone();
}

bool CLyrics::GetCurrentLine(char *pBuf, size_t BufSize, int64_t PositionMs) const
{
	if(m_State != EState::READY || BufSize == 0)
		return false;

	if(m_IsSynced && !m_Lines.empty())
	{
		auto It = std::upper_bound(m_Lines.begin(), m_Lines.end(), PositionMs, [](int64_t Time, const CLyricLine &Line) {
			return Time < Line.m_TimeMs;
		});
		if(It == m_Lines.begin())
			return false;
		--It;
		if(It->m_Text.empty())
			return false;
		str_copy(pBuf, It->m_Text.c_str(), BufSize);
		return true;
	}

	if(!m_PlainLine.empty())
	{
		str_copy(pBuf, m_PlainLine.c_str(), BufSize);
		return true;
	}

	return false;
}
