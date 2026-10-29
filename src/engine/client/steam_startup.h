#ifndef ENGINE_CLIENT_STEAM_STARTUP_H
#define ENGINE_CLIENT_STEAM_STARTUP_H

#include <memory>
#include <type_traits>
#include <utility>

namespace steam_startup
{
	// 可选启动失败只返回false，不让路径准备或启动器异常中断客户端启动。
	template<typename TFind, typename TLaunch>
	bool Open(TFind &&Find, TLaunch &&Launch) noexcept
	{
		try
		{
			char aPath[1024] = {};
			if(!Find(aPath, sizeof(aPath)))
				return false;
			return Launch(aPath);
		}
		catch(...)
		{
			return false;
		}
	}

	// fallback须在调用前准备；成功对象接管SDK寿命，创建失败则立即释放SDK。
	// Shutdown必须不抛异常；Factory仅在完整返回后接管SDK，失败不自行Shutdown。
	template<typename TInterface, typename TInit, typename TFactory, typename TShutdown>
	std::unique_ptr<TInterface> Create(std::unique_ptr<TInterface> &pFallback, TInit &&Init, TFactory &&Factory, TShutdown &&Shutdown) noexcept
	{
		static_assert(std::is_nothrow_invocable_v<TShutdown &>);
		bool Initialized = false;
		try
		{
			if(!Init())
				return std::move(pFallback);
			Initialized = true;
			auto pCandidate = Factory();
			if(pCandidate)
				return pCandidate;
		}
		catch(...)
		{
		}
		if(Initialized)
			Shutdown();
		return std::move(pFallback);
	}
}

#endif
