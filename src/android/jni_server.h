#ifndef ANDROID_JNI_SERVER_H
#define ANDROID_JNI_SERVER_H

#include "jni_calls.h"

#include <optional>
#include <string>
#include <utility>

namespace android_jni
{
	// JNI字符只在当前读取作用域借用；转换分配失败也必须先归还。
	template<typename TEnv, typename TString, typename TChar>
	class CChars
	{
		TEnv &m_Env;
		TString m_String;
		const TChar *m_pChars;

	public:
		CChars(TEnv &Env, TString String, const TChar *pChars) noexcept :
			m_Env(Env), m_String(String), m_pChars(pChars) {}
		CChars(const CChars &) = delete;
		CChars &operator=(const CChars &) = delete;
		~CChars()
		{
			if(m_pChars)
				m_Env.ReleaseStringChars(m_String, m_pChars);
		}
		const TChar *Get() const noexcept { return m_pChars; }
	};

	template<typename TChar>
	std::optional<std::string> Utf8(const TChar *pChars, size_t Count)
	{
		if(!pChars && Count)
			return std::nullopt;
		std::string Result;
		for(size_t i = 0; i < Count; ++i)
		{
			int Codepoint = pChars[i];
			if(!Codepoint)
				return std::nullopt; // 本机路径、argv和命令不能表示内嵌NUL，拒绝整项而不截断。
			if(Codepoint >= 0xd800 && Codepoint <= 0xdbff && i + 1 < Count && pChars[i + 1] >= 0xdc00 && pChars[i + 1] <= 0xdfff)
				Codepoint = 0x10000 + ((Codepoint - 0xd800) << 10) + (pChars[++i] - 0xdc00);
			else if(Codepoint >= 0xd800 && Codepoint <= 0xdfff)
				Codepoint = 0xfffd;
			char aEncoded[4];
			Result.append(aEncoded, str_utf8_encode(aEncoded, Codepoint));
		}
		return Result;
	}

	template<typename TEnv, typename TString>
	std::optional<std::string> ReadString(TEnv &Env, TString String)
	{
		if(!String || Env.ExceptionCheck())
			return std::nullopt;
		const auto Length = Env.GetStringLength(String);
		if(Length < 0 || Env.ExceptionCheck())
			return std::nullopt;
		if(!Length)
			return std::string{};
		const CChars Chars(Env, String, Env.GetStringChars(String, nullptr));
		if(!Chars.Get() || Env.ExceptionCheck())
			return std::nullopt;
		return Utf8(Chars.Get(), static_cast<size_t>(Length));
	}

	inline constexpr int DIRECTORY_ERROR = -1001;
	inline constexpr int PREPARATION_ERROR = -1002;

	template<typename TEnv, typename TString, typename TArray, typename TChangeDirectory, typename TRun>
	int RunServer(TEnv *pEnv, TString Directory, TArray Arguments, const char *pProgramName, TChangeDirectory &&ChangeDirectory, TRun &&Run) noexcept
	{
		if(!pEnv)
			return PREPARATION_ERROR;
		try
		{
			std::string WorkingDirectory;
			std::vector<std::string> vArguments;
			std::vector<const char *> vpArguments;
			{
				CLocalFrame<TEnv> Frame(*pEnv);
				if(!Frame.Open() || !Arguments || !pProgramName)
					return PREPARATION_ERROR;
				auto DirectoryText = ReadString(*pEnv, Directory);
				if(!DirectoryText)
					return PREPARATION_ERROR;
				const auto Count = pEnv->GetArrayLength(Arguments);
				if(pEnv->ExceptionCheck() || Count < 0 || Count >= std::numeric_limits<std::int32_t>::max())
					return PREPARATION_ERROR;
				vArguments.reserve(static_cast<size_t>(Count) + 1);
				vArguments.push_back(*DirectoryText + "/" + pProgramName);
				for(std::int32_t i = 0; i < Count; ++i)
				{
					// JNIEnv返回jobject，接收端读字符串前显式转换为jstring。
					auto Argument = static_cast<TString>(pEnv->GetObjectArrayElement(Arguments, i));
					if(!Argument || pEnv->ExceptionCheck())
						return PREPARATION_ERROR;
					auto Text = ReadString(*pEnv, Argument);
					pEnv->DeleteLocalRef(Argument);
					if(!Text || pEnv->ExceptionCheck())
						return PREPARATION_ERROR;
					vArguments.push_back(std::move(*Text));
				}
				vpArguments.reserve(vArguments.size());
				for(const auto &Argument : vArguments)
					vpArguments.push_back(Argument.c_str());
				WorkingDirectory = std::move(*DirectoryText);
			} // 字符借用与额外局部引用不跨越整个服务器运行期。
			if(!ChangeDirectory(WorkingDirectory.c_str()))
				return DIRECTORY_ERROR;
			return Run(static_cast<int>(vpArguments.size()), vpArguments.data());
		}
		catch(...)
		{
			return PREPARATION_ERROR;
		}
	}

	template<typename TEnv, typename TString, typename TSubmit>
	bool ReceiveCommand(TEnv *pEnv, TString Command, TSubmit &&Submit) noexcept
	{
		if(!pEnv)
			return false;
		try
		{
			std::optional<std::string> Text;
			{
				CLocalFrame<TEnv> Frame(*pEnv);
				if(!Frame.Open())
					return false;
				Text = ReadString(*pEnv, Command);
				if(!Text)
					return false;
			}
			return Submit(std::move(*Text));
		}
		catch(...)
		{
			return false;
		}
	}
}

#endif
