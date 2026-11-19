#ifndef QM_HOOKS_NETEASE_QM_NETEASE_CDP_IO_H
#define QM_HOOKS_NETEASE_QM_NETEASE_CDP_IO_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace QmNeteaseCdp
{
	// 整个操作共享起点；字节、控制帧或重试都不能重新延长期限。
	class CIoBudget
	{
		uint64_t m_Started;
		int m_Duration;
		const std::atomic_bool *m_pStop;

	public:
		CIoBudget(uint64_t Started, int DurationMs, const std::atomic_bool *pStop = nullptr) :
			m_Started(Started), m_Duration(std::max(0, DurationMs)), m_pStop(pStop) {}
		bool Cancelled() const noexcept { return m_pStop && m_pStop->load(std::memory_order_acquire); }
		int Remaining(uint64_t Now) const noexcept
		{
			if(Cancelled() || Now < m_Started || Now - m_Started >= static_cast<uint64_t>(m_Duration))
				return 0;
			return m_Duration - static_cast<int>(Now - m_Started);
		}
		int Slice(uint64_t Now) const noexcept { return std::min(20, Remaining(Now)); }
	};

	enum class EIoReady
	{
		READY,
		TIMEOUT,
		FAILED
	};
	enum class EIoDirection
	{
		READ,
		WRITE
	};
	enum class EChunkStatus
	{
		DATA,
		RETRY,
		END,
		FAILED
	};
	struct SIoChunk
	{
		EChunkStatus m_Status = EChunkStatus::FAILED;
		size_t m_Size = 0;
	};
	enum class ETransferResult
	{
		COMPLETE,
		IDLE,
		FAILED
	};

	// 传输必须非阻塞，等待仅占用传入的短时间片；调用方串行管理socket寿命。
	class ISocketIo
	{
	public:
		virtual ~ISocketIo() = default;
		virtual uint64_t NowMs() const noexcept = 0;
		virtual EIoReady Wait(EIoDirection Direction, int SliceMs) noexcept = 0;
		virtual SIoChunk Read(void *pData, size_t Capacity) noexcept = 0;
		virtual SIoChunk Write(const void *pData, size_t Size) noexcept = 0;
	};

	inline bool WaitCdpSocket(ISocketIo &Io, const CIoBudget &Budget, EIoDirection Direction) noexcept
	{
		while(const int Slice = Budget.Slice(Io.NowMs()))
		{
			const EIoReady Ready = Io.Wait(Direction, Slice);
			if(Ready == EIoReady::FAILED)
				return false;
			if(Ready == EIoReady::READY)
				return Budget.Remaining(Io.NowMs()) > 0;
		}
		return false;
	}

	namespace detail
	{
		template<typename T, typename FTransfer>
		ETransferResult Transfer(ISocketIo &Io, T *pData, size_t Size, const CIoBudget &Budget, EIoDirection Direction, bool AllowIdle, FTransfer Action) noexcept
		{
			if(!pData && Size != 0)
				return ETransferResult::FAILED;
			size_t Done = 0;
			while(Done < Size)
			{
				if(Budget.Cancelled())
					return ETransferResult::FAILED;
				const int Slice = Budget.Slice(Io.NowMs());
				if(Slice == 0)
					return Done == 0 && AllowIdle ? ETransferResult::IDLE : ETransferResult::FAILED;
				const EIoReady Ready = Io.Wait(Direction, Slice);
				if(Ready == EIoReady::FAILED)
					return ETransferResult::FAILED;
				if(Ready == EIoReady::TIMEOUT)
					continue;
				if(Budget.Remaining(Io.NowMs()) == 0)
					continue;
				const SIoChunk Chunk = Action(pData + Done, Size - Done);
				if(Chunk.m_Status == EChunkStatus::RETRY)
					continue;
				if(Chunk.m_Status != EChunkStatus::DATA || Chunk.m_Size == 0 || Chunk.m_Size > Size - Done)
					return ETransferResult::FAILED;
				Done += Chunk.m_Size;
			}
			return !Budget.Cancelled() && (Size == 0 || Budget.Remaining(Io.NowMs()) > 0) ? ETransferResult::COMPLETE : ETransferResult::FAILED;
		}
	}

	inline ETransferResult ReadCdpExact(ISocketIo &Io, void *pData, size_t Size, const CIoBudget &Budget, bool AllowIdle = false) noexcept
	{
		return detail::Transfer(Io, static_cast<char *>(pData), Size, Budget, EIoDirection::READ, AllowIdle, [&Io](void *pOutput, size_t Capacity) { return Io.Read(pOutput, Capacity); });
	}
	inline bool WriteCdpAll(ISocketIo &Io, const void *pData, size_t Size, const CIoBudget &Budget) noexcept
	{
		return detail::Transfer(Io, static_cast<const char *>(pData), Size, Budget, EIoDirection::WRITE, false, [&Io](const void *pInput, size_t Count) { return Io.Write(pInput, Count); }) == ETransferResult::COMPLETE;
	}

	// 只有收到完整、非空且未超限的正文才交换发布；错误和取消保留原输出。
	template<typename FAllowed, typename FRead>
	bool ReadCdpHttpBody(std::string &Output, size_t MaximumBytes, FAllowed Allowed, FRead Read)
	{
		std::array<char, 8192> Buffer{};
		std::string Candidate;
		while(Allowed())
		{
			const SIoChunk Chunk = Read(Buffer.data(), Buffer.size());
			if(!Allowed())
				return false;
			if(Chunk.m_Status == EChunkStatus::END)
			{
				if(Candidate.empty() || Chunk.m_Size != 0)
					return false;
				Output.swap(Candidate);
				return true;
			}
			if(Chunk.m_Status != EChunkStatus::DATA || Chunk.m_Size == 0 || Chunk.m_Size > Buffer.size() || Chunk.m_Size > MaximumBytes - Candidate.size())
				return false;
			Candidate.append(Buffer.data(), Chunk.m_Size);
		}
		return false;
	}
}

#endif
