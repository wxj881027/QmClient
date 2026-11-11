#include "qm_lxmusic_config.h"

#include <base/system.h>

#include <engine/external/json-parser/json.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

#include <tlhelp32.h>
#endif

namespace QmLxMusic
{
	namespace
	{
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

		bool BoolValue(const json_value *pValue, bool Default = false)
		{
			if(pValue == nullptr)
				return Default;
			if(pValue->type == json_boolean)
				return pValue->u.boolean != 0;
			return Default;
		}

		// 端口在配置里是字符串("23330"),也接受整数;越界按无效处理。
		int PortValue(const json_value *pValue)
		{
			long Port = 0;
			if(pValue == nullptr)
				return 0;
			if(pValue->type == json_string && pValue->u.string.ptr != nullptr)
			{
				const std::string Text(pValue->u.string.ptr, pValue->u.string.length);
				if(Text.empty())
					return 0;
				Port = std::strtol(Text.c_str(), nullptr, 10);
			}
			else if(pValue->type == json_integer)
			{
				Port = (long)pValue->u.integer;
			}
			else
			{
				return 0;
			}
			return Port > 0 && Port <= 65535 ? (int)Port : 0;
		}

		json_value *ParseJson(std::string_view Json)
		{
			if(Json.empty())
				return nullptr;
			const std::string Copy(Json);
			return json_parse(Copy.c_str(), Copy.size());
		}

		bool IsJsonSpace(char C)
		{
			return C == ' ' || C == '\t' || C == '\n' || C == '\r';
		}

		// 定位 setting 对象的字节范围(不含外层花括号):改写只允许发生在这一段内,
		// 否则歌单等数组里同名的键会被误改。扫描时跳过字符串字面量与转义。
		bool FindSettingRange(const std::string &Text, size_t *pBegin, size_t *pEnd)
		{
			const std::string Pattern = "\"setting\"";
			const size_t KeyPos = Text.find(Pattern);
			if(KeyPos == std::string::npos)
				return false;
			size_t Pos = KeyPos + Pattern.size();
			while(Pos < Text.size() && IsJsonSpace(Text[Pos]))
				++Pos;
			if(Pos >= Text.size() || Text[Pos] != ':')
				return false;
			++Pos;
			while(Pos < Text.size() && IsJsonSpace(Text[Pos]))
				++Pos;
			if(Pos >= Text.size() || Text[Pos] != '{')
				return false;

			const size_t Begin = Pos + 1;
			int Depth = 1;
			bool InString = false;
			for(size_t Index = Begin; Index < Text.size(); ++Index)
			{
				const char C = Text[Index];
				if(InString)
				{
					if(C == '\\')
						++Index;
					else if(C == '"')
						InString = false;
					continue;
				}
				if(C == '"')
				{
					InString = true;
					continue;
				}
				if(C == '{')
					++Depth;
				else if(C == '}' && --Depth == 0)
				{
					*pBegin = Begin;
					*pEnd = Index;
					return true;
				}
			}
			return false;
		}

		// 在 [Begin, End) 内定位 key 之后的值起点。
		size_t ValueOffset(const std::string &Text, const char *pKey, size_t Begin, size_t End)
		{
			const std::string Pattern = std::string("\"") + pKey + "\"";
			const size_t KeyPos = Text.find(Pattern, Begin);
			if(KeyPos == std::string::npos || KeyPos + Pattern.size() > End)
				return std::string::npos;
			size_t Pos = KeyPos + Pattern.size();
			while(Pos < End && IsJsonSpace(Text[Pos]))
				++Pos;
			if(Pos >= End || Text[Pos] != ':')
				return std::string::npos;
			++Pos;
			while(Pos < End && IsJsonSpace(Text[Pos]))
				++Pos;
			return Pos < End ? Pos : std::string::npos;
		}

		// 返回与 QuotePos 配对的反引号位置(处理 \" 转义);找不到返回 npos。
		size_t StringValueEnd(const std::string &Text, size_t QuotePos, size_t End)
		{
			for(size_t Index = QuotePos + 1; Index < End; ++Index)
			{
				if(Text[Index] == '\\')
					++Index;
				else if(Text[Index] == '"')
					return Index;
			}
			return std::string::npos;
		}

		bool SetBoolInRange(std::string &Text, const char *pKey, bool Value, size_t Begin, size_t End)
		{
			const size_t Pos = ValueOffset(Text, pKey, Begin, End);
			if(Pos == std::string::npos)
				return false;
			const std::string Target = Value ? "true" : "false";
			const std::string Opposite = Value ? "false" : "true";
			if(Text.compare(Pos, Opposite.size(), Opposite) == 0)
			{
				Text.replace(Pos, Opposite.size(), Target);
				return true;
			}
			// 已经是目标值也算成功,便于「重复点击不报错」。
			return Text.compare(Pos, Target.size(), Target) == 0;
		}

		bool SetPortInRange(std::string &Text, const char *pKey, int Port, size_t Begin, size_t End)
		{
			const size_t Pos = ValueOffset(Text, pKey, Begin, End);
			if(Pos == std::string::npos)
				return false;
			// 统一写成字符串:上游自己就用 "23330" 这种形式。
			const std::string Value = "\"" + std::to_string(Port) + "\"";
			if(Text[Pos] == '"')
			{
				const size_t ValueEnd = StringValueEnd(Text, Pos, End);
				if(ValueEnd == std::string::npos)
					return false;
				Text.replace(Pos, ValueEnd - Pos + 1, Value);
				return true;
			}
			// 同一字段也可能是裸数字,替换到分隔符为止。
			size_t TokenEnd = Pos;
			while(TokenEnd < End && Text[TokenEnd] != ',' && Text[TokenEnd] != '}' && !IsJsonSpace(Text[TokenEnd]))
				++TokenEnd;
			if(TokenEnd == Pos)
				return false;
			Text.replace(Pos, TokenEnd - Pos, Value);
			return true;
		}

#if defined(CONF_FAMILY_WINDOWS)

		std::filesystem::path ConfigPath()
		{
			wchar_t aAppData[32768];
			const DWORD Length = GetEnvironmentVariableW(L"APPDATA", aAppData, (DWORD)std::size(aAppData));
			if(Length == 0 || Length >= std::size(aAppData))
				return {};
			return std::filesystem::path(aAppData) / L"lx-music-desktop" / L"LxDatas" / L"config_v2.json";
		}

		bool WideEqualsIgnoreCase(const wchar_t *pLeft, const wchar_t *pRight)
		{
			for(;;)
			{
				const wchar_t Left = *pLeft++;
				const wchar_t Right = *pRight++;
				const wchar_t LowerLeft = Left >= L'A' && Left <= L'Z' ? (wchar_t)(Left - L'A' + L'a') : Left;
				const wchar_t LowerRight = Right >= L'A' && Right <= L'Z' ? (wchar_t)(Right - L'A' + L'a') : Right;
				if(LowerLeft != LowerRight)
					return false;
				if(LowerLeft == L'\0')
					return true;
			}
		}

		bool IsClientRunning()
		{
			HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
			if(hSnapshot == INVALID_HANDLE_VALUE)
				return false;
			PROCESSENTRY32W Entry{};
			Entry.dwSize = sizeof(Entry);
			bool Running = false;
			if(Process32FirstW(hSnapshot, &Entry))
			{
				do
				{
					// Electron 主进程与渲染子进程同名,只要有一个就说明 LX 正在运行。
					if(WideEqualsIgnoreCase(Entry.szExeFile, L"lx-music-desktop.exe"))
					{
						Running = true;
						break;
					}
				} while(Process32NextW(hSnapshot, &Entry));
			}
			CloseHandle(hSnapshot);
			return Running;
		}

		// 先写临时文件再替换,避免半截写入把配置弄坏。
		bool WriteFileAtomically(const std::filesystem::path &Path, const std::string &Text)
		{
			const std::wstring Temporary = Path.wstring() + L".qmclient-tmp";
			HANDLE hFile = CreateFileW(Temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if(hFile == INVALID_HANDLE_VALUE)
				return false;
			DWORD Written = 0;
			const bool Ok = WriteFile(hFile, Text.data(), (DWORD)Text.size(), &Written, nullptr) != FALSE &&
					Written == Text.size() && FlushFileBuffers(hFile) != FALSE;
			CloseHandle(hFile);
			if(!Ok)
			{
				DeleteFileW(Temporary.c_str());
				return false;
			}
			if(!MoveFileExW(Temporary.c_str(), Path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				DeleteFileW(Temporary.c_str());
				return false;
			}
			return true;
		}
#endif
	} // namespace

	bool ConfigTextSetBool(std::string &Text, const char *pKey, bool Value)
	{
		if(pKey == nullptr)
			return false;
		size_t Begin = 0;
		size_t End = 0;
		if(!FindSettingRange(Text, &Begin, &End))
			return false;
		return SetBoolInRange(Text, pKey, Value, Begin, End);
	}

	bool ConfigTextSetPort(std::string &Text, const char *pKey, int Port)
	{
		if(pKey == nullptr || Port <= 0 || Port > 65535)
			return false;
		size_t Begin = 0;
		size_t End = 0;
		if(!FindSettingRange(Text, &Begin, &End))
			return false;
		return SetPortInRange(Text, pKey, Port, Begin, End);
	}

	bool ParseClientConfig(std::string_view Json, SClientConfig *pOut)
	{
		if(pOut == nullptr)
			return false;
		json_value *pRoot = ParseJson(Json);
		if(pRoot == nullptr)
			return false;

		const json_value *pSetting = Field(pRoot, "setting");
		if(pSetting == nullptr || pSetting->type != json_object)
		{
			json_value_free(pRoot);
			return false;
		}

		SClientConfig Config;
		Config.m_Found = true;
		Config.m_OpenApiEnabled = BoolValue(Field(pSetting, "openAPI.enable"));
		Config.m_OpenApiPort = PortValue(Field(pSetting, "openAPI.port"));
		Config.m_SyncServerEnabled = BoolValue(Field(pSetting, "sync.enable"));
		Config.m_SyncServerPort = PortValue(Field(pSetting, "sync.server.port"));
		json_value_free(pRoot);

		*pOut = Config;
		return true;
	}

	bool LoadClientConfig(SClientConfig *pOut)
	{
		if(pOut == nullptr)
			return false;
#if defined(CONF_FAMILY_WINDOWS)
		const std::filesystem::path Path = ConfigPath();
		if(Path.empty())
			return false;
		std::error_code Error;
		if(!std::filesystem::exists(Path, Error) || Error)
			return false;

		std::ifstream File(Path, std::ios::binary);
		if(!File.is_open())
			return false;
		const std::string Json((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
		// LX 自己的配置里同一时刻只有一个写入者,读到的半文件直接放弃下一轮再读。
		if(Json.empty() || Json.front() != '{')
			return false;
		return ParseClientConfig(Json, pOut);
#else
		return false;
#endif
	}

	EEnableResult EnableOpenApi(std::string *pMessage)
	{
		if(pMessage == nullptr)
			return EEnableResult::WriteFailed;
		pMessage->clear();
#if defined(CONF_FAMILY_WINDOWS)
		const std::filesystem::path Path = ConfigPath();
		std::error_code Error;
		if(Path.empty() || !std::filesystem::exists(Path, Error) || Error)
		{
			*pMessage = "未找到 LX Music 配置（未安装或从未启动过）";
			return EEnableResult::NotInstalled;
		}
		// LX 退出时会把内存里的设置写回文件,运行中改动必然被覆盖。
		if(IsClientRunning())
		{
			*pMessage = "请先退出 LX Music，再点一次（它退出时会覆盖配置）";
			return EEnableResult::ClientRunning;
		}

		std::ifstream File(Path, std::ios::binary);
		if(!File.is_open())
		{
			*pMessage = "无法读取 LX Music 配置";
			return EEnableResult::WriteFailed;
		}
		const std::string Json((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
		File.close();

		SClientConfig Before;
		if(Json.empty() || !ParseClientConfig(Json, &Before))
		{
			*pMessage = "LX Music 配置格式无法识别，未做任何修改";
			return EEnableResult::WriteFailed;
		}
		if(Before.m_OpenApiEnabled)
		{
			*pMessage = "开放 API 已经是开启状态";
			return EEnableResult::AlreadyEnabled;
		}

		std::string Updated = Json;
		if(!ConfigTextSetBool(Updated, "openAPI.enable", true))
		{
			*pMessage = "配置里没有 openAPI.enable 字段，未做任何修改";
			return EEnableResult::WriteFailed;
		}
		// 端口冲突时挪开放 API 而不是同步服务:同步地址可能已经被其他设备保存。
		constexpr int CONFLICT_FREE_PORT = 23340;
		const bool PortConflict = Before.HasPortConflict();
		const bool MovedPort = PortConflict && ConfigTextSetPort(Updated, "openAPI.port", CONFLICT_FREE_PORT);
		if(PortConflict && !MovedPort)
		{
			*pMessage = "开放 API 与数据同步服务端口冲突，但配置里没有 openAPI.port 字段；请在 LX 设置里把开放 API 端口改为 23340";
			return EEnableResult::WriteFailed;
		}

		// 落盘前重新解析:确认开关生效、端口冲突已解决,且没有把 JSON 改坏。
		SClientConfig After;
		if(!ParseClientConfig(Updated, &After) || !After.m_OpenApiEnabled || After.HasPortConflict() ||
			(MovedPort && After.m_OpenApiPort != CONFLICT_FREE_PORT))
		{
			*pMessage = "写入前校验失败，未做任何修改";
			return EEnableResult::WriteFailed;
		}

		// 备份只保留一份:连续点击时覆盖上一次的备份,避免堆积。
		// 备份失败就不写:不给用户「改坏了还能还原」的错觉。
		const std::filesystem::path Backup = std::filesystem::path(Path.wstring() + L".qmclient-bak");
		std::error_code BackupError;
		std::filesystem::copy_file(Path, Backup, std::filesystem::copy_options::overwrite_existing, BackupError);
		if(BackupError)
		{
			*pMessage = "无法创建配置备份，未做任何修改";
			return EEnableResult::WriteFailed;
		}

		if(!WriteFileAtomically(Path, Updated))
		{
			*pMessage = "写入失败，请检查文件权限";
			return EEnableResult::WriteFailed;
		}

		if(MovedPort)
			*pMessage = "已开启开放 API，端口从冲突值改为 23340：启动 LX Music 即可生效";
		else
			*pMessage = "已开启开放 API：启动 LX Music 即可生效";
		return EEnableResult::Ok;
#else
		*pMessage = "仅 Windows 支持自动开启";
		return EEnableResult::NotInstalled;
#endif
	}
}
