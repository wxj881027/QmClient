#ifndef TEST_SUPPORT_ANDROID_JNI_INPUT_H
#define TEST_SUPPORT_ANDROID_JNI_INPUT_H

#include "android_jni_env.h"

// 模拟Java native方法的传入局部引用；生产helper只能回收自己推入的子frame。
class CAndroidJniInput
{
public:
	CAndroidJniEnv m_Env;
	CAndroidJniEnv::TObject m_Directory;
	CAndroidJniEnv::TObject m_Arguments;

	CAndroidJniInput()
	{
		m_Env.PushLocalFrame(8);
		const std::uint16_t aDirectory[] = {'/', 'd', 'a', 't', 'a'};
		m_Directory = m_Env.NewString(aDirectory, 5);
		auto Class = m_Env.FindClass("java/lang/String");
		m_Arguments = m_Env.NewObjectArray(0, Class, nullptr);
		m_Env.DeleteLocalRef(Class);
	}
	~CAndroidJniInput() { m_Env.PopLocalFrame(nullptr); }
	CAndroidJniInput(const CAndroidJniInput &) = delete;
	CAndroidJniInput &operator=(const CAndroidJniInput &) = delete;
};

#endif
