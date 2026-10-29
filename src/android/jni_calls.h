#ifndef ANDROID_JNI_CALLS_H
#define ANDROID_JNI_CALLS_H

#include <base/system.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace android_jni
{
	// Env只借用当前线程的JNI环境，清理调用不得抛C++异常。
	template<typename TEnv>
	class CLocalFrame
	{
		TEnv &m_Env;
		bool m_Open = false;

	public:
		explicit CLocalFrame(TEnv &Env) noexcept :
			m_Env(Env) {}
		CLocalFrame(const CLocalFrame &) = delete;
		CLocalFrame &operator=(const CLocalFrame &) = delete;
		~CLocalFrame()
		{
			if(m_Env.ExceptionCheck())
			{
				m_Env.ExceptionDescribe();
				m_Env.ExceptionClear();
			}
			if(m_Open)
				m_Env.PopLocalFrame(nullptr);
		}
		bool Open()
		{
			if(m_Env.ExceptionCheck() || m_Env.PushLocalFrame(8) != 0)
				return false;
			m_Open = true;
			return !m_Env.ExceptionCheck();
		}
	};

	// Activity也在帧中获取，正常、Java异常及C++准备异常均释放全部局部引用。
	template<typename TEnv, typename TAcquire, typename TAction>
	bool Call(TEnv *pEnv, TAcquire &&Acquire, TAction &&Action) noexcept
	{
		if(!pEnv)
			return false;
		try
		{
			CLocalFrame<TEnv> Frame(*pEnv);
			if(!Frame.Open())
				return false;
			auto Activity = Acquire();
			if(!Activity || pEnv->ExceptionCheck())
				return false;
			return Action(Activity) && !pEnv->ExceptionCheck();
		}
		catch(...)
		{
			return false;
		}
	}

	inline std::vector<std::uint16_t> Utf16(const char *pText)
	{
		std::vector<std::uint16_t> vCharacters;
		while(*pText)
		{
			const int Codepoint = str_utf8_decode(&pText);
			if(Codepoint < 0)
				vCharacters.push_back(0xfffd);
			else if(Codepoint <= 0xffff)
				vCharacters.push_back(static_cast<std::uint16_t>(Codepoint));
			else
			{
				vCharacters.push_back(static_cast<std::uint16_t>(0xd800 + ((Codepoint - 0x10000) >> 10)));
				vCharacters.push_back(static_cast<std::uint16_t>(0xdc00 + ((Codepoint - 0x10000) & 0x3ff)));
			}
		}
		return vCharacters;
	}

	template<typename TEnv>
	auto MakeString(TEnv &Env, const char *pText)
	{
		const auto vCharacters = Utf16(pText);
		if(vCharacters.size() > static_cast<size_t>(std::numeric_limits<std::int32_t>::max()))
			throw std::length_error("JNI string length");
		const std::uint16_t Empty = 0;
		return Env.NewString(vCharacters.empty() ? &Empty : vCharacters.data(), static_cast<std::int32_t>(vCharacters.size()));
	}

	inline bool ValidArray(const char *const *ppValues, size_t Count) noexcept
	{
		return Count <= static_cast<size_t>(std::numeric_limits<std::int32_t>::max()) && (!Count || ppValues);
	}

	template<typename TEnv, typename TObject>
	bool StartServer(TEnv &Env, TObject Activity, const char *const *ppArguments, size_t Count)
	{
		if(!ValidArray(ppArguments, Count))
			return false;
		auto ActivityClass = Env.GetObjectClass(Activity);
		if(!ActivityClass || Env.ExceptionCheck())
			return false;
		auto StringClass = Env.FindClass("java/lang/String");
		if(!StringClass || Env.ExceptionCheck())
			return false;
		auto Arguments = Env.NewObjectArray(static_cast<std::int32_t>(Count), StringClass, nullptr);
		if(!Arguments || Env.ExceptionCheck())
			return false;
		for(size_t i = 0; i < Count; ++i)
		{
			if(!ppArguments[i])
				return false;
			auto Argument = MakeString(Env, ppArguments[i]);
			if(!Argument || Env.ExceptionCheck())
				return false;
			Env.SetObjectArrayElement(Arguments, static_cast<std::int32_t>(i), Argument);
			Env.DeleteLocalRef(Argument);
			if(Env.ExceptionCheck())
				return false;
		}
		auto Method = Env.GetMethodID(ActivityClass, "startServer", "([Ljava/lang/String;)V");
		if(!Method || Env.ExceptionCheck())
			return false;
		Env.CallVoidMethod(Activity, Method, Arguments);
		return !Env.ExceptionCheck();
	}

	template<typename TEnv, typename TObject>
	bool ExecuteCommand(TEnv &Env, TObject Activity, const char *pCommand)
	{
		if(!pCommand)
			return false;
		auto ActivityClass = Env.GetObjectClass(Activity);
		if(!ActivityClass || Env.ExceptionCheck())
			return false;
		auto Command = MakeString(Env, pCommand);
		if(!Command || Env.ExceptionCheck())
			return false;
		auto Method = Env.GetMethodID(ActivityClass, "executeCommand", "(Ljava/lang/String;)V");
		if(!Method || Env.ExceptionCheck())
			return false;
		Env.CallVoidMethod(Activity, Method, Command);
		return !Env.ExceptionCheck();
	}

	template<typename TEnv, typename TObject>
	bool IsRunning(TEnv &Env, TObject Activity)
	{
		auto ActivityClass = Env.GetObjectClass(Activity);
		if(!ActivityClass || Env.ExceptionCheck())
			return false;
		auto Method = Env.GetMethodID(ActivityClass, "isServerRunning", "()Z");
		if(!Method || Env.ExceptionCheck())
			return false;
		const bool Running = Env.CallBooleanMethod(Activity, Method);
		return !Env.ExceptionCheck() && Running;
	}

	template<typename TEnv, typename TObject>
	bool ShowMedia(TEnv &Env, TObject Activity, const char *pDirectory, bool Videos, const char *const *ppExcluded, size_t Count)
	{
		if(!pDirectory || !ValidArray(ppExcluded, Count))
			return false;
		auto ActivityClass = Env.GetObjectClass(Activity);
		if(!ActivityClass || Env.ExceptionCheck())
			return false;
		auto Method = Env.GetMethodID(ActivityClass, "showMediaFiles", "(Ljava/lang/String;Z[Ljava/lang/String;)V");
		if(!Method || Env.ExceptionCheck())
			return false;
		auto StringClass = Env.FindClass("java/lang/String");
		if(!StringClass || Env.ExceptionCheck())
			return false;
		auto Directory = MakeString(Env, pDirectory);
		if(!Directory || Env.ExceptionCheck())
			return false;
		auto Excluded = Env.NewObjectArray(static_cast<std::int32_t>(Count), StringClass, nullptr);
		if(!Excluded || Env.ExceptionCheck())
			return false;
		for(size_t i = 0; i < Count; ++i)
		{
			if(!ppExcluded[i])
				return false;
			auto Path = MakeString(Env, ppExcluded[i]);
			if(!Path || Env.ExceptionCheck())
				return false;
			Env.SetObjectArrayElement(Excluded, static_cast<std::int32_t>(i), Path);
			Env.DeleteLocalRef(Path);
			if(Env.ExceptionCheck())
				return false;
		}
		Env.CallVoidMethod(Activity, Method, Directory, Videos, Excluded);
		return !Env.ExceptionCheck();
	}
}

#endif
