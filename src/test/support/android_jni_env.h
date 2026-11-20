#ifndef TEST_SUPPORT_ANDROID_JNI_ENV_H
#define TEST_SUPPORT_ANDROID_JNI_ENV_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <vector>

// 仅替换外部JNI环境：局部引用/异常/Java调用结果可观察，不实现生产调用策略。
class CAndroidJniEnv
{
public:
	enum class EFailure
	{
		NONE,
		PUSH,
		ACTIVITY,
		CLASS,
		STRING_CLASS,
		ARRAY,
		STRING,
		ELEMENT,
		METHOD,
		VOID_CALL,
		BOOLEAN_CALL,
		STRING_LENGTH,
		CHARS,
		ARRAY_LENGTH,
		GET_ELEMENT
	};
	struct CReference
	{
		bool m_Live = false;
		int m_Depth = 0;
		std::u16string m_Text;
	};
	struct CMethod
	{
	};
	using TObject = CReference *;

	EFailure m_Failure = EFailure::NONE;
	bool m_FailWithException = true;
	bool m_Pending = false;
	bool m_Running = true;
	int m_Depth = 0, m_Pushes = 0, m_Pops = 0, m_Clears = 0, m_Describes = 0;
	int m_Live = 0, m_PeakLive = 0, m_BadCalls = 0, m_VoidCalls = 0, m_BooleanCalls = 0;
	int m_Arrays = 0, m_Strings = 0, m_Elements = 0, m_ThrowAtString = 0;
	int m_Borrows = 0, m_Releases = 0, m_ReadStrings = 0, m_ThrowAtReadString = 0;
	int m_ArrayLengthOverride = -2, m_StringLengthOverride = -2, m_NullElement = -1;
	bool m_CharsWithException = false;
	const char *m_pMethod = nullptr;
	const char *m_pSignature = nullptr;
	std::u16string m_LastString;
	std::vector<std::u16string> m_ArrayValues;

private:
	std::array<CReference, 32> m_aReferences;
	CMethod m_Method;
	bool Fault(EFailure Stage)
	{
		if(m_Pending)
			++m_BadCalls;
		if(m_Failure != Stage)
			return false;
		m_Pending = m_FailWithException;
		return true;
	}
	bool Valid(TObject Object)
	{
		if(!Object || !Object->m_Live)
		{
			++m_BadCalls;
			return false;
		}
		return true;
	}
	TObject NewReference()
	{
		const auto Current = std::count_if(m_aReferences.begin(), m_aReferences.end(), [&](const auto &Ref) { return Ref.m_Live && Ref.m_Depth == m_Depth; });
		if(m_Depth <= 0 || Current >= 8)
		{
			++m_BadCalls;
			m_Pending = true;
			return nullptr;
		}
		for(auto &Ref : m_aReferences)
			if(!Ref.m_Live)
			{
				Ref.m_Live = true;
				Ref.m_Depth = m_Depth;
				Ref.m_Text.clear();
				m_PeakLive = std::max(m_PeakLive, ++m_Live);
				return &Ref;
			}
		++m_BadCalls;
		return nullptr;
	}

public:
	bool ExceptionCheck() const { return m_Pending; }
	void ExceptionDescribe() noexcept { ++m_Describes; }
	void ExceptionClear() noexcept
	{
		++m_Clears;
		m_Pending = false;
	}
	int PushLocalFrame(int)
	{
		++m_Pushes;
		if(Fault(EFailure::PUSH))
			return -1;
		++m_Depth;
		return 0;
	}
	void PopLocalFrame(std::nullptr_t) noexcept
	{
		++m_Pops;
		if(m_Depth <= 0)
			++m_BadCalls;
		for(auto &Ref : m_aReferences)
			if(Ref.m_Live && Ref.m_Depth == m_Depth)
			{
				Ref.m_Live = false;
				--m_Live;
			}
		--m_Depth;
	}
	TObject Activity() { return Fault(EFailure::ACTIVITY) ? nullptr : NewReference(); }
	TObject GetObjectClass(TObject Activity)
	{
		if(Fault(EFailure::CLASS) || !Valid(Activity))
			return nullptr;
		return NewReference();
	}
	TObject FindClass(const char *) { return Fault(EFailure::STRING_CLASS) ? nullptr : NewReference(); }
	TObject NewObjectArray(std::int32_t Count, TObject Class, std::nullptr_t)
	{
		++m_Arrays;
		if(Fault(EFailure::ARRAY) || !Valid(Class))
			return nullptr;
		m_ArrayValues.assign(static_cast<size_t>(Count), std::u16string{});
		return NewReference();
	}
	TObject NewString(const std::uint16_t *pText, std::int32_t Count)
	{
		++m_Strings;
		if(m_Strings == m_ThrowAtString)
			throw std::bad_alloc();
		if(Fault(EFailure::STRING))
			return nullptr;
		if(!pText || Count < 0)
		{
			++m_BadCalls;
			return nullptr;
		}
		auto Ref = NewReference();
		if(Ref)
			Ref->m_Text.assign(pText, pText + Count);
		m_LastString.assign(pText, pText + Count);
		return Ref;
	}
	void SetObjectArrayElement(TObject Array, std::int32_t Index, TObject Value)
	{
		++m_Elements;
		if(Fault(EFailure::ELEMENT) || !Valid(Array) || !Valid(Value))
			return;
		if(Index < 0 || static_cast<size_t>(Index) >= m_ArrayValues.size())
		{
			++m_BadCalls;
			return;
		}
		m_ArrayValues[Index] = Value->m_Text;
	}
	void DeleteLocalRef(TObject Ref) noexcept
	{
		if(!Ref || !Ref->m_Live)
			++m_BadCalls;
		else
		{
			Ref->m_Live = false;
			--m_Live;
		}
	}
	std::int32_t GetStringLength(TObject String)
	{
		if(++m_ReadStrings == m_ThrowAtReadString)
			throw std::bad_alloc();
		if(Fault(EFailure::STRING_LENGTH) || !Valid(String))
			return 0;
		return m_StringLengthOverride != -2 ? m_StringLengthOverride : static_cast<std::int32_t>(String->m_Text.size());
	}
	const char16_t *GetStringChars(TObject String, std::nullptr_t)
	{
		if(Fault(EFailure::CHARS) || !Valid(String))
			return nullptr;
		++m_Borrows;
		m_Pending = m_CharsWithException;
		return String->m_Text.data();
	}
	void ReleaseStringChars(TObject String, const char16_t *pChars) noexcept
	{
		if(!String || !String->m_Live || pChars != String->m_Text.data() || m_Borrows <= 0)
			++m_BadCalls;
		--m_Borrows;
		++m_Releases;
	}
	std::int32_t GetArrayLength(TObject Array)
	{
		if(Fault(EFailure::ARRAY_LENGTH) || !Valid(Array))
			return 0;
		return m_ArrayLengthOverride != -2 ? m_ArrayLengthOverride : static_cast<std::int32_t>(m_ArrayValues.size());
	}
	TObject GetObjectArrayElement(TObject Array, std::int32_t Index)
	{
		if(Fault(EFailure::GET_ELEMENT) || !Valid(Array) || Index == m_NullElement)
			return nullptr;
		if(Index < 0 || static_cast<size_t>(Index) >= m_ArrayValues.size())
		{
			++m_BadCalls;
			return nullptr;
		}
		auto Ref = NewReference();
		if(Ref)
			Ref->m_Text = m_ArrayValues[Index];
		return Ref;
	}
	CMethod *GetMethodID(TObject Class, const char *pName, const char *pSignature)
	{
		m_pMethod = pName;
		m_pSignature = pSignature;
		return Fault(EFailure::METHOD) || !Valid(Class) ? nullptr : &m_Method;
	}
	template<typename... TArgs>
	void CallVoidMethod(TObject Activity, CMethod *Method, TArgs...)
	{
		++m_VoidCalls;
		if(!Valid(Activity) || !Method)
			++m_BadCalls;
		Fault(EFailure::VOID_CALL);
	}
	bool CallBooleanMethod(TObject Activity, CMethod *Method)
	{
		++m_BooleanCalls;
		if(!Valid(Activity) || !Method)
			++m_BadCalls;
		Fault(EFailure::BOOLEAN_CALL);
		return m_Running;
	}
};

#endif
