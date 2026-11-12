#include "map_tags.h"

#include <base/str.h>

#include <engine/http.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/localization.h>

#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace
{
	constexpr size_t MAX_RESPONSE_BYTES = 64 * 1024;
	constexpr const char *ENDPOINT = "https://qmclient.icu/api/v1/map-tags";
	using CJson = std::unique_ptr<json_value, decltype(&json_value_free)>;

	bool ReadString(const json_value *pObject, const char *pKey, std::string &Out, size_t Max)
	{
		const json_value *pValue = json_object_get(pObject, pKey);
		if(pValue->type != json_string || pValue->u.string.length > Max ||
			str_length(pValue->u.string.ptr) != (int)pValue->u.string.length || !str_utf8_check(pValue->u.string.ptr))
			return false;
		Out = pValue->u.string.ptr;
		return true;
	}

	void Prepare(IHttpRequest &Request)
	{
		Request.Timeout({3000, 12000, 256, 5});
		Request.MaxResponseSize(MAX_RESPONSE_BYTES);
		Request.FailOnErrorStatus(false);
		Request.LogProgress(HTTPLOG::NONE);
	}
}

bool QmMapTags::ValidTag(const char *pText)
{
	if(!pText || !pText[0] || !str_utf8_check(pText))
		return false;
	int Count = 0;
	while(*pText)
	{
		const int Code = str_utf8_decode(&pText);
		if(Code < 0x20 || Code == 0x7f || ++Count > MAX_CHARACTERS)
			return false;
	}
	return true;
}

bool QmMapTags::ParseSnapshot(const char *pData, size_t Size, SSnapshot &Out)
{
	if(!pData || Size == 0 || Size > MAX_RESPONSE_BYTES)
		return false;
	CJson Root(JsonParse(pData, Size), json_value_free);
	if(!Root || Root->type != json_object)
		return false;
	const json_value *pOk = json_object_get(Root.get(), "ok");
	const json_value *pEligible = json_object_get(Root.get(), "eligible");
	const json_value *pTags = json_object_get(Root.get(), "tags");
	SSnapshot Next;
	std::string Error;
	if(pOk->type != json_boolean || !pOk->u.boolean || pEligible->type != json_boolean ||
		pTags->type != json_array || pTags->u.array.length > MAX_TAGS ||
		!ReadString(Root.get(), "community", Next.m_Community, 32) ||
		!ReadString(Root.get(), "map", Next.m_Map, 512) ||
		!ReadString(Root.get(), "player", Next.m_Player, 128) ||
		!ReadString(Root.get(), "eligibility_error", Error, 64))
		return false;
	Next.m_Eligible = pEligible->u.boolean;
	Next.m_EligibilityUnavailable = !Error.empty();
	std::set<std::string> Ids;
	std::set<int> Colors;
	for(unsigned Index = 0; Index < pTags->u.array.length; ++Index)
	{
		const json_value *pItem = pTags->u.array.values[Index];
		const json_value *pColor = json_object_get(pItem, "color");
		const json_value *pCount = json_object_get(pItem, "count");
		const json_value *pVoted = json_object_get(pItem, "voted");
		STag Tag;
		if(pItem->type != json_object || !ReadString(pItem, "id", Tag.m_Id, 64) || Tag.m_Id.empty() ||
			!ReadString(pItem, "text", Tag.m_Text, 40) || !ValidTag(Tag.m_Text.c_str()) ||
			pColor->type != json_integer || pColor->u.integer < 0 || pColor->u.integer >= MAX_TAGS ||
			pCount->type != json_integer || pCount->u.integer < 0 || pCount->u.integer > std::numeric_limits<int>::max() || pVoted->type != json_boolean)
			return false;
		Tag.m_Color = (int)pColor->u.integer;
		Tag.m_Count = (int)pCount->u.integer;
		Tag.m_Voted = pVoted->u.boolean;
		if(!Ids.insert(Tag.m_Id).second || !Colors.insert(Tag.m_Color).second)
			return false;
		Next.m_vTags.push_back(std::move(Tag));
	}
	Out = std::move(Next);
	return true;
}

float QmMapTags::ColorHue(int Color)
{
	// 固定序号映射到不同色相；审核、排序和赞同人数变化都不改变颜色。
	return std::fmod((float)Color * 0.61803398875f, 1.0f);
}

QmMapTags::CClient::CClient(TReadFactory ReadFactory, TWriteFactory WriteFactory) :
	m_CreateRead(ReadFactory ? ReadFactory : HttpGet),
	m_CreateWrite(WriteFactory ? WriteFactory : HttpPostJson)
{
}

QmMapTags::CClient::~CClient()
{
	Clear();
}

void QmMapTags::CClient::Clear()
{
	if(m_pRead)
		m_pRead->Abort();
	if(m_pWrite)
		m_pWrite->Abort();
	m_pRead.reset();
	m_pWrite.reset();
	m_Community.clear();
	m_Map.clear();
	m_Player.clear();
	m_Snapshot = {};
	m_Ready = false;
	m_ReadFailed = false;
	m_NextRefresh = 0;
	m_pStatus = "";
}

void QmMapTags::CClient::SetContext(const char *pCommunity, const char *pMap, const char *pPlayer)
{
	if(m_Community == pCommunity && m_Map == pMap && m_Player == pPlayer)
		return;
	// 地图、社区或昵称改变时，中止并丢弃旧请求，不把旧结果发布到新上下文。
	Clear();
	m_Community = pCommunity;
	m_Map = pMap;
	m_Player = pPlayer;
}

void QmMapTags::CClient::Poll()
{
	if(m_pRead && m_pRead->Done())
	{
		unsigned char *pData = nullptr;
		size_t Size = 0;
		if(m_pRead->State() == EHttpState::DONE)
			m_pRead->Result(&pData, &Size);
		SSnapshot Next;
		if(m_pRead->State() == EHttpState::DONE && m_pRead->StatusCode() == 200 &&
			ParseSnapshot((const char *)pData, Size, Next) && Next.m_Community == m_Community && Next.m_Map == m_Map && Next.m_Player == m_Player)
		{
			m_Snapshot = std::move(Next);
			m_Ready = true;
			if(m_ReadFailed)
				m_pStatus = "";
			m_ReadFailed = false;
		}
		else
		{
			m_pStatus = Localizable("Cannot load map tags. Please retry.");
			m_ReadFailed = true;
		}
		m_pRead.reset();
	}
	if(m_pWrite && m_pWrite->Done())
	{
		m_ReadFailed = false;
		CJson Root(m_pWrite->State() == EHttpState::DONE ? m_pWrite->ResultJson() : nullptr, json_value_free);
		const json_value *pOk = Root ? json_object_get(Root.get(), "ok") : nullptr;
		if(m_pWrite->State() == EHttpState::DONE && m_pWrite->StatusCode() == 200 && pOk && pOk->type == json_boolean && pOk->u.boolean)
		{
			const json_value *pPending = json_object_get(Root.get(), "pending");
			m_pStatus = pPending->type == json_boolean && pPending->u.boolean ? Localizable("Tag submitted for review.") : Localizable("Your support has been recorded.");
			m_NextRefresh = 0;
		}
		else
		{
			const json_value *pError = Root ? json_object_get(Root.get(), "error") : nullptr;
			const char *pErrorText = pError && pError->type == json_string ? pError->u.string.ptr : "";
			if(str_comp(pErrorText, "blocked_tag") == 0 || str_comp(pErrorText, "rejected_tag") == 0)
				m_pStatus = Localizable("This tag is not allowed.");
			else if(str_comp(pErrorText, "finish_required") == 0)
				m_pStatus = Localizable("Finish this map before submitting tags.");
			else
				m_pStatus = Localizable("Cannot submit this tag. Please retry later.");
		}
		m_pWrite.reset();
	}
}

void QmMapTags::CClient::Update(IHttp *pHttp, double Now)
{
	Poll();
	if(!pHttp || m_Map.empty() || m_Player.empty() || m_pRead || m_pWrite || Now < m_NextRefresh)
		return;
	char aCommunity[128], aMap[2048], aPlayer[512], aUrl[3072];
	EscapeUrl(aCommunity, m_Community.c_str());
	EscapeUrl(aMap, m_Map.c_str());
	EscapeUrl(aPlayer, m_Player.c_str());
	str_format(aUrl, sizeof(aUrl), "%s?community=%s&map=%s&player=%s", ENDPOINT, aCommunity, aMap, aPlayer);
	m_pRead = m_CreateRead(aUrl);
	Prepare(*m_pRead);
	pHttp->Run(m_pRead);
	m_NextRefresh = Now + 30.0;
}

void QmMapTags::CClient::Submit(IHttp *pHttp, const char *pTag)
{
	if(!pHttp || m_pWrite || !m_Ready || !m_Snapshot.m_Eligible || !ValidTag(pTag))
		return;
	CJsonStringWriter Writer;
	Writer.BeginObject();
	for(const auto &Field : {std::pair{"community", m_Community.c_str()}, {"map", m_Map.c_str()}, {"player", m_Player.c_str()}, {"text", pTag}})
	{
		Writer.WriteAttribute(Field.first);
		Writer.WriteStrValue(Field.second);
	}
	Writer.EndObject();
	m_pWrite = m_CreateWrite(ENDPOINT, Writer.GetOutputString().c_str());
	Prepare(*m_pWrite);
	// 先前的读请求可能返回赞同前快照，写入开始后将其丢弃。
	if(m_pRead)
		m_pRead->Abort();
	m_pRead.reset();
	pHttp->Run(m_pWrite);
}
