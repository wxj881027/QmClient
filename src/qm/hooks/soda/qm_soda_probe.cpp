#include "qm_soda_probe.h"

#include <engine/external/json-parser/json.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

namespace QmSodaProbe
{
	namespace
	{
		std::string Base64Encode(std::string_view Data)
		{
			static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string Result;
			Result.reserve((Data.size() + 2) / 3 * 4);
			for(size_t Index = 0; Index < Data.size(); Index += 3)
			{
				const uint32_t A = (uint8_t)Data[Index];
				const uint32_t B = Index + 1 < Data.size() ? (uint8_t)Data[Index + 1] : 0;
				const uint32_t C = Index + 2 < Data.size() ? (uint8_t)Data[Index + 2] : 0;
				const uint32_t Value = (A << 16) | (B << 8) | C;
				Result.push_back(Alphabet[(Value >> 18) & 63]);
				Result.push_back(Alphabet[(Value >> 12) & 63]);
				Result.push_back(Index + 1 < Data.size() ? Alphabet[(Value >> 6) & 63] : '=');
				Result.push_back(Index + 2 < Data.size() ? Alphabet[Value & 63] : '=');
			}
			return Result;
		}

		const json_value *Field(const json_value *pObject, const char *pName)
		{
			if(pObject == nullptr || pObject->type != json_object || pName == nullptr)
				return nullptr;
			for(unsigned int Index = 0; Index < pObject->u.object.length; ++Index)
			{
				const auto &Entry = pObject->u.object.values[Index];
				if(Entry.name != nullptr && std::string_view(Entry.name, Entry.name_length) == pName)
					return Entry.value;
			}
			return nullptr;
		}

		std::string StringValue(const json_value *pValue)
		{
			if(pValue == nullptr || pValue->type != json_string || pValue->u.string.ptr == nullptr)
				return {};
			return std::string(pValue->u.string.ptr, pValue->u.string.length);
		}

		double NumberValue(const json_value *pValue, double Default = 0)
		{
			if(pValue == nullptr)
				return Default;
			if(pValue->type == json_integer)
				return (double)pValue->u.integer;
			if(pValue->type == json_double)
				return pValue->u.dbl;
			return Default;
		}

		bool BooleanValue(const json_value *pValue, bool Default = false)
		{
			if(pValue == nullptr)
				return Default;
			if(pValue->type == json_boolean)
				return pValue->u.boolean != 0;
			return Default;
		}
	}

	std::string BuildInnerProbeScript()
	{
		// 必须保持纯 ASCII:该脚本由 bridge 用 base64 编码、页面 atob 解回,atob 只认 Latin-1。
		return R"JS((function(){return new Promise(function(resolve){
var cleanup=function(){};
try{
var proto=MessagePort.prototype;var orig=proto.postMessage;var port=null;
proto.postMessage=function(){port=this;return orig.apply(this,arguments);};
try{window.transportPort.sendTransport({__qmSodaCap:1});}catch(e){}
proto.postMessage=orig;
if(!port){resolve({err:'no-port'});return;}
cleanup=function(){try{port.removeEventListener('message',onMsg);}catch(e){}};
var reqId='qmsodaget-'+Math.random().toString(36).slice(2)+Date.now();
var done=false;
var onMsg=function(e){
var d=e.data;
if(!d||d.type!=='method.return'||d.requestId!==reqId)return;
done=true;cleanup();
var r=d.return;
if(!r||r.type!=='success'){resolve({err:'ret'});return;}
var p=r.result||{};var md=p.mediaDetail||{};var pl=md.playable||{};var ly=md.lyrics||{};var al=pl.album||{};
var cover='';var cu=pl.cover_url;
if(typeof cu==='string'){cover=cu;}
else if(cu&&cu.uri&&cu.urls&&cu.urls.length){cover=cu.urls[0]+cu.uri+'~'+(cu.template_prefix||'')+'-crop-center:800:800.jpg';}
var artists=[];var pa=pl.artists||[];
for(var i=0;i<pa.length;i++){if(pa[i]&&pa[i].name)artists.push(pa[i].name);}
resolve({
ok:true,
isPlaying:!!p.isPlaying,isLoading:!!p.isLoading,
progressSeconds:p.progressSeconds,durationSeconds:p.durationSeconds,
mediaId:(p.mediaId!=null?String(p.mediaId):(pl.id!=null?String(pl.id):'')),
name:pl.name||'',album:(al.name||''),coverUrl:cover,
lyricType:ly.type||'',lyricContent:ly.content||'',
translationLrc:((ly.translations&&typeof ly.translations==='object'&&ly.translations.cn)?ly.translations.cn:'')
});
};
port.addEventListener('message',onMsg);
setTimeout(function(){if(!done){cleanup();resolve({err:'timeout'});}},2500);
try{window.transportPort.sendTransport({type:'method.invoke',fromWorkerId:'rendererMain',toServiceId:'sharedState',methodName:'get',requestId:reqId,arguments:['player'],callbacks:{}});}
catch(e){cleanup();resolve({err:'send:'+String(e&&e.message||e)});return;}
}catch(e){cleanup();resolve({err:'ex:'+String(e&&e.message||e)});}
});})())JS";
	}

	std::string BuildBridgeExpression()
	{
		const std::string B64 = Base64Encode(BuildInnerProbeScript());
		std::string Bridge = "(async()=>{";
		// 汽水 3.7.0 的 inspector 全局上下文没有 CJS require(PlayerCap 旧版有);
		// process.mainModule.require 是稳定入口(已真机验证)。
		Bridge += "const {webContents}=process.mainModule.require('electron');";
		Bridge += "const all=webContents.getAllWebContents();";
		Bridge += "let target=null;";
		Bridge += "for(const wc of all){try{const u=wc.getURL()||'';if(u.indexOf('main.html')>=0){target=wc;break;}}catch(e){}}";
		Bridge += "if(!target){for(const wc of all){try{const u=wc.getURL()||'';if(u.indexOf('taskbar')<0&&u.indexOf('.html')>=0){target=wc;break;}}catch(e){}}}";
		Bridge += "if(!target)return JSON.stringify({err:'no-main-window'});";
		Bridge += "let bt=null;try{target.setBackgroundThrottling(false);bt=target.backgroundThrottling;}catch(e){}";
		Bridge += "try{const r=await target.executeJavaScript(atob(\"" + B64 + "\"),true);";
		Bridge += "if(r&&typeof r==='object')r.throttled=bt;return JSON.stringify(r);}";
		Bridge += "catch(e){return JSON.stringify({err:'exec:'+String(e&&e.message||e)});}";
		Bridge += "})()";
		return Bridge;
	}

	bool ParseExtractionJson(std::string_view Json, SPlaybackSnapshot *pOut)
	{
		if(pOut == nullptr)
			return false;
		*pOut = {};
		if(Json.empty() || Json == "null")
		{
			pOut->m_Error = "empty extraction";
			return false;
		}
		json_value *pRoot = json_parse(Json.data(), Json.size());
		if(pRoot == nullptr || pRoot->type != json_object)
		{
			if(pRoot != nullptr)
				json_value_free(pRoot);
			pOut->m_Error = "invalid json";
			return false;
		}
		pOut->m_Error = StringValue(Field(pRoot, "err"));
		pOut->m_Ok = BooleanValue(Field(pRoot, "ok"), false);
		if(pOut->m_Ok)
		{
			pOut->m_IsPlaying = BooleanValue(Field(pRoot, "isPlaying"));
			pOut->m_IsLoading = BooleanValue(Field(pRoot, "isLoading"));
			pOut->m_ProgressSeconds = NumberValue(Field(pRoot, "progressSeconds"));
			pOut->m_DurationSeconds = NumberValue(Field(pRoot, "durationSeconds"));
			pOut->m_MediaId = StringValue(Field(pRoot, "mediaId"));
			pOut->m_Name = StringValue(Field(pRoot, "name"));
			pOut->m_Album = StringValue(Field(pRoot, "album"));
			pOut->m_CoverUrl = StringValue(Field(pRoot, "coverUrl"));
			// artists 是数组字段,折叠为逗号分隔字符串。
			const json_value *pArtists = Field(pRoot, "artists");
			if(pArtists != nullptr && pArtists->type == json_array)
			{
				for(unsigned int Index = 0; Index < pArtists->u.array.length; ++Index)
				{
					const std::string Name = StringValue(pArtists->u.array.values[Index]);
					if(Name.empty())
						continue;
					if(!pOut->m_Artist.empty())
						pOut->m_Artist.append(", ");
					pOut->m_Artist.append(Name);
				}
			}
			pOut->m_LyricType = StringValue(Field(pRoot, "lyricType"));
			pOut->m_LyricContent = StringValue(Field(pRoot, "lyricContent"));
			pOut->m_TranslationLrc = StringValue(Field(pRoot, "translationLrc"));
			pOut->m_Throttled = BooleanValue(Field(pRoot, "throttled"));
			if(pOut->m_Name.empty() && pOut->m_LyricContent.empty() && pOut->m_MediaId.empty())
			{
				pOut->m_Ok = false;
				pOut->m_Error = "extraction has no usable fields";
			}
		}
		else if(pOut->m_Error.empty())
			pOut->m_Error = "extraction not ok";
		json_value_free(pRoot);
		return pOut->m_Ok;
	}
} // namespace QmSodaProbe
