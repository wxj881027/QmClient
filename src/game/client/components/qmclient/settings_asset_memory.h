#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_MEMORY_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_ASSET_MEMORY_H

#include <engine/graphics.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>

inline constexpr uint64_t SETTINGS_ASSET_MEMORY_BUDGET_BYTES = uint64_t(512) * 1024 * 1024;

enum class ESettingsAssetImageAdmission
{
	ACCEPTED,
	DEFERRED,
	FAILED,
};

namespace SettingsAssetMemoryDetail
{
	struct SState
	{
		const uint64_t m_Limit;
		std::atomic<uint64_t> m_Used{0};
		std::atomic<uint64_t> m_DeferredSequence{0};
		explicit SState(uint64_t Limit) noexcept :
			m_Limit(Limit) {}
	};

	struct SContext
	{
		std::shared_ptr<SState> m_pState;
		uint64_t m_Bytes;
		SContext(std::shared_ptr<SState> pState, uint64_t Bytes) noexcept :
			m_pState(std::move(pState)), m_Bytes(Bytes) {}
	};

	inline bool Add(uint64_t Left, uint64_t Right, uint64_t &Result) noexcept
	{
		if(Right > std::numeric_limits<uint64_t>::max() - Left)
			return false;
		Result = Left + Right;
		return true;
	}

	inline bool Multiply(uint64_t Left, uint64_t Right, uint64_t &Result) noexcept
	{
		if(Left != 0 && Right > std::numeric_limits<uint64_t>::max() / Left)
			return false;
		Result = Left * Right;
		return true;
	}
}

class CSettingsAssetMemoryBudget;

// 每份像素 owner 单独持有一个上下文；移到原生命令后也不借用页面或预算外壳。
class CSettingsAssetMemoryLease
{
	using SState = SettingsAssetMemoryDetail::SState;
	using SContext = SettingsAssetMemoryDetail::SContext;
	SContext *m_pContext = nullptr;

	static void ReleaseContext(void *pContext) noexcept
	{
		std::unique_ptr<SContext> Context(static_cast<SContext *>(pContext));
		if(Context)
			Context->m_pState->m_Used.fetch_sub(Context->m_Bytes, std::memory_order_relaxed);
	}

	static bool TryAcquire(std::shared_ptr<SState> pState, uint64_t Bytes, CSettingsAssetMemoryLease &Out) noexcept
	{
		if(Bytes == 0)
		{
			Out.Reset();
			return true;
		}
		uint64_t Used = pState->m_Used.load(std::memory_order_relaxed);
		for(;;)
		{
			if(Bytes > pState->m_Limit || Used > pState->m_Limit - Bytes)
			{
				pState->m_DeferredSequence.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			if(pState->m_Used.compare_exchange_weak(Used, Used + Bytes, std::memory_order_relaxed))
				break;
		}
		// 只有预留成功才准备释放上下文；失败时完整退回此次预留。
		auto *pContext = new(std::nothrow) SContext(pState, Bytes);
		if(pContext == nullptr)
		{
			pState->m_Used.fetch_sub(Bytes, std::memory_order_relaxed);
			pState->m_DeferredSequence.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		CSettingsAssetMemoryLease Candidate;
		Candidate.m_pContext = pContext;
		Out = std::move(Candidate);
		return true;
	}
	friend class CSettingsAssetMemoryBudget;

public:
	CSettingsAssetMemoryLease() = default;
	CSettingsAssetMemoryLease(const CSettingsAssetMemoryLease &) = delete;
	CSettingsAssetMemoryLease &operator=(const CSettingsAssetMemoryLease &) = delete;
	CSettingsAssetMemoryLease(CSettingsAssetMemoryLease &&Other) noexcept :
		m_pContext(std::exchange(Other.m_pContext, nullptr)) {}
	CSettingsAssetMemoryLease &operator=(CSettingsAssetMemoryLease &&Other) noexcept
	{
		if(this != &Other)
		{
			Reset();
			m_pContext = std::exchange(Other.m_pContext, nullptr);
		}
		return *this;
	}
	~CSettingsAssetMemoryLease() { Reset(); }

	uint64_t Bytes() const noexcept { return m_pContext != nullptr ? m_pContext->m_Bytes : 0; }
	bool HasReservation() const noexcept { return m_pContext != nullptr; }
	void Reset() noexcept { ReleaseContext(std::exchange(m_pContext, nullptr)); }
	bool Shrink(uint64_t Bytes) noexcept
	{
		if(Bytes > this->Bytes())
			return false;
		if(Bytes == 0)
		{
			Reset();
			return true;
		}
		const uint64_t Previous = m_pContext->m_Bytes;
		m_pContext->m_Bytes = Bytes;
		m_pContext->m_pState->m_Used.fetch_sub(Previous - Bytes, std::memory_order_relaxed);
		return true;
	}
	bool TryClone(CSettingsAssetMemoryLease &Out) const noexcept
	{
		if(m_pContext == nullptr)
		{
			Out.Reset();
			return true;
		}
		return TryAcquire(m_pContext->m_pState, m_pContext->m_Bytes, Out);
	}
	SGraphicsUploadRelease Detach() noexcept
	{
		if(m_pContext == nullptr)
			return {};
		return {std::exchange(m_pContext, nullptr), ReleaseContext};
	}
	static CSettingsAssetMemoryLease Adopt(SGraphicsUploadRelease &Release) noexcept
	{
		CSettingsAssetMemoryLease Result;
		if(Release.m_pContext != nullptr && Release.m_pfnRelease == ReleaseContext)
		{
			Result.m_pContext = static_cast<SContext *>(Release.m_pContext);
			Release = {};
		}
		return Result;
	}
};

class CSettingsAssetMemoryBudget
{
	using SState = SettingsAssetMemoryDetail::SState;
	const uint64_t m_Limit;
	std::shared_ptr<SState> m_pState;
	std::atomic<uint64_t> m_PrepareDeferredSequence{0};

public:
	// 默认对象本身不分配；首次共享状态分配失败可以在下一次准入时重试。
	explicit CSettingsAssetMemoryBudget(uint64_t Limit = SETTINGS_ASSET_MEMORY_BUDGET_BYTES) noexcept :
		m_Limit(Limit) {}
	CSettingsAssetMemoryBudget(const CSettingsAssetMemoryBudget &) = delete;
	CSettingsAssetMemoryBudget &operator=(const CSettingsAssetMemoryBudget &) = delete;

	uint64_t Limit() const noexcept { return m_Limit; }
	uint64_t Used() const noexcept
	{
		const auto State = std::atomic_load_explicit(&m_pState, std::memory_order_acquire);
		return State != nullptr ? State->m_Used.load(std::memory_order_relaxed) : 0;
	}
	uint64_t DeferredSequence() const noexcept
	{
		const auto State = std::atomic_load_explicit(&m_pState, std::memory_order_acquire);
		return m_PrepareDeferredSequence.load(std::memory_order_relaxed) +
		       (State != nullptr ? State->m_DeferredSequence.load(std::memory_order_relaxed) : 0);
	}
	bool TryAcquire(uint64_t Bytes, CSettingsAssetMemoryLease &Out) noexcept
	{
		if(Bytes == 0)
		{
			Out.Reset();
			return true;
		}
		if(Bytes > m_Limit)
		{
			m_PrepareDeferredSequence.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		auto State = std::atomic_load_explicit(&m_pState, std::memory_order_acquire);
		if(State == nullptr)
		{
			try
			{
				auto Candidate = std::make_shared<SState>(m_Limit);
				std::shared_ptr<SState> Expected;
				State = std::atomic_compare_exchange_strong_explicit(&m_pState, &Expected, Candidate, std::memory_order_acq_rel, std::memory_order_acquire) ? std::move(Candidate) : std::move(Expected);
			}
			catch(const std::bad_alloc &)
			{
				m_PrepareDeferredSequence.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
		}
		return CSettingsAssetMemoryLease::TryAcquire(std::move(State), Bytes, Out);
	}
};

inline CSettingsAssetMemoryBudget &SettingsAssetMemoryDefaultBudget() noexcept
{
	static CSettingsAssetMemoryBudget Budget;
	return Budget;
}

// 仅计算编码缓冲与像素上界，不包含 codec scratch、元数据或 GPU staging。
inline bool SettingsAssetMemoryComputeWorkingBytes(size_t EncodedBytes, size_t DecodedBytes, int MaxTextureSize, uint64_t &Out) noexcept
{
	using namespace SettingsAssetMemoryDetail;
	Out = 0;
	const uint64_t Encoded = EncodedBytes > std::numeric_limits<unsigned>::max() ? std::numeric_limits<unsigned>::max() : EncodedBytes;
	const uint64_t Decoded = DecodedBytes;
	uint64_t Resized = 0;
	if(MaxTextureSize > 0)
	{
		uint64_t Pixels;
		if(!Multiply(static_cast<uint64_t>(MaxTextureSize), static_cast<uint64_t>(MaxTextureSize), Pixels) ||
			!Multiply(Pixels, 4, Resized))
			return false;
		if(Resized > Decoded)
			Resized = Decoded;
	}
	uint64_t File, Decode, Resize, Ready;
	if(!Add(Encoded, 1, File) || !Add(File, Encoded, Decode) || !Add(Decode, Decoded, Decode) ||
		!Add(File, Decoded, Resize) || !Add(Resize, Resized, Resize) || !Multiply(Decoded, 2, Ready))
		return false;
	Out = Decode > Resize ? Decode : Resize;
	if(Ready > Out)
		Out = Ready;
	return true;
}

inline bool SettingsAssetMemoryReadyBytes(const CImageInfo &Image, uint64_t &Out) noexcept
{
	using namespace SettingsAssetMemoryDetail;
	Out = 0;
	if(Image.m_pData == nullptr)
		return true;
	if(Image.m_Width == 0 || Image.m_Height == 0 ||
		Image.m_Width > static_cast<size_t>(std::numeric_limits<int>::max()) || Image.m_Height > static_cast<size_t>(std::numeric_limits<int>::max()) ||
		Image.m_Format < CImageInfo::FORMAT_RGB || Image.m_Format > CImageInfo::FORMAT_RA)
		return false;
	size_t SourceBytes = 0;
	uint64_t Pixels, Rgba;
	if(!Image.DataSize(SourceBytes) || SourceBytes == 0 || !Multiply(Image.m_Width, Image.m_Height, Pixels) ||
		!Multiply(Pixels, 4, Rgba) || !Multiply(Rgba, 2, Out))
	{
		Out = 0;
		return false;
	}
	return true;
}

inline bool SettingsAssetMemoryTryAcquireImage(const CImageInfo &Image, CSettingsAssetMemoryLease &Out, CSettingsAssetMemoryBudget *pBudget = nullptr) noexcept
{
	uint64_t Bytes = 0;
	if(!SettingsAssetMemoryReadyBytes(Image, Bytes))
		return false;
	return (pBudget != nullptr ? *pBudget : SettingsAssetMemoryDefaultBudget()).TryAcquire(Bytes, Out);
}

#endif
