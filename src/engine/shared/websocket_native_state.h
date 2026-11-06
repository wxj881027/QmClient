#ifndef ENGINE_SHARED_WEBSOCKET_NATIVE_STATE_H
#define ENGINE_SHARED_WEBSOCKET_NATIVE_STATE_H

#include <base/borrowed_allocator.h>

#include <engine/shared/network.h>
#include <engine/shared/ringbuffer.h>

#include <limits>
#include <map>
#include <memory>
#include <utility>

struct lws;

struct SWebsocketNativeChunk
{
	size_t m_Size;
	size_t m_Read;
	NETADDR m_Addr;
	unsigned char m_aData[0];
};

// C库只持有wsi；环缓冲及其std::function必须由正常构造的C++对象持有。
class CWebsocketNativeState
{
public:
	using TRecvBuffer = CStaticRingBuffer<SWebsocketNativeChunk, (MAX_CLIENTS * 2) * NET_CONN_BUFFERSIZE, CRingBufferBase::FLAG_RECYCLE>;
	using TSendBuffer = CStaticRingBuffer<SWebsocketNativeChunk, NET_CONN_BUFFERSIZE, CRingBufferBase::FLAG_RECYCLE>;
	struct SSession
	{
		lws *m_pWsi;
		NETADDR m_Addr;
		TSendBuffer m_SendBuffer;
		SSession(lws *pWsi, const NETADDR &Addr) :
			m_pWsi(pWsi), m_Addr(Addr) {}
		SSession(const SSession &) = delete;
		SSession &operator=(const SSession &) = delete;
	};

private:
	std::map<lws *, std::unique_ptr<SSession>, std::less<lws *>, CBorrowedAllocator<std::pair<lws *const, std::unique_ptr<SSession>>>> m_Sessions;
	std::map<NETADDR, SSession *, std::less<NETADDR>, CBorrowedAllocator<std::pair<const NETADDR, SSession *>>> m_Ports;
	TRecvBuffer m_RecvBuffer;

	static bool ChunkSize(size_t Size, size_t Before, size_t After, int &Result)
	{
		constexpr size_t Limit = std::numeric_limits<int>::max() - sizeof(SWebsocketNativeChunk);
		if(Before > Limit || After > Limit - Before || Size > Limit - Before - After)
			return false;
		Result = static_cast<int>(sizeof(SWebsocketNativeChunk) + Before + Size + After);
		return true;
	}

public:
	explicit CWebsocketNativeState(IAllocationResource *pResource = nullptr) :
		m_Sessions(pResource), m_Ports(pResource) {}
	CWebsocketNativeState(const CWebsocketNativeState &) = delete;
	CWebsocketNativeState &operator=(const CWebsocketNativeState &) = delete;

	SSession *Find(lws *pWsi) const
	{
		const auto It = m_Sessions.find(pWsi);
		return It == m_Sessions.end() ? nullptr : It->second.get();
	}
	SSession *Find(const NETADDR &Addr) const
	{
		const auto It = m_Ports.find(Addr);
		return It == m_Ports.end() ? nullptr : It->second;
	}
	const auto &Ports() const { return m_Ports; }
	size_t SessionCount() const { return m_Sessions.size(); }
	TRecvBuffer &RecvBuffer() { return m_RecvBuffer; }

	SSession *TryRegister(lws *pWsi, const NETADDR &Addr) noexcept
	{
		if(!pWsi || Addr.type == NETTYPE_INVALID)
			return nullptr;
		if(auto *pExisting = Find(pWsi))
			return pExisting;
		try
		{
			auto pCandidate = std::make_unique<SSession>(pWsi, Addr);
			auto *pSession = pCandidate.get();
			const auto It = m_Sessions.emplace(pWsi, std::move(pCandidate)).first;
			try
			{
				m_Ports.insert_or_assign(Addr, pSession);
			}
			catch(...)
			{
				m_Sessions.erase(It);
				throw;
			}
			return pSession;
		}
		catch(...)
		{
			return nullptr;
		}
	}

	void Remove(lws *pWsi) noexcept
	{
		const auto It = m_Sessions.find(pWsi);
		if(It == m_Sessions.end())
			return;
		const auto Port = m_Ports.find(It->second->m_Addr);
		// 同地址的后续连接已取代索引时，旧连接关闭不能擦掉新连接。
		if(Port != m_Ports.end() && Port->second == It->second.get())
			m_Ports.erase(Port);
		m_Sessions.erase(It);
	}
	void Reset() noexcept
	{
		m_Ports.clear();
		m_Sessions.clear();
		m_RecvBuffer.Init();
	}

	// 回调失败先撤回本连接，不让C++异常跨过C库的调用栈。
	template<typename F>
	int Dispatch(lws *pWsi, F &&Callback) noexcept
	{
		try
		{
			return std::forward<F>(Callback)();
		}
		catch(...)
		{
			Remove(pWsi);
			return -1;
		}
	}

	bool Receive(const NETADDR &Addr, const void *pData, size_t Size)
	{
		int AllocationSize;
		if((Size != 0 && !pData) || !ChunkSize(Size, 0, 0, AllocationSize))
			return false;
		auto *pChunk = m_RecvBuffer.Allocate(AllocationSize);
		if(!pChunk)
			return false;
		pChunk->m_Size = Size;
		pChunk->m_Read = 0;
		pChunk->m_Addr = Addr;
		if(Size != 0)
			mem_copy(pChunk->m_aData, pData, Size);
		return true;
	}
	bool QueueSend(SSession &Session, const void *pData, size_t Size, size_t Before, size_t After)
	{
		int AllocationSize;
		if((Size != 0 && !pData) || !ChunkSize(Size, Before, After, AllocationSize))
			return false;
		auto *pChunk = Session.m_SendBuffer.Allocate(AllocationSize);
		if(!pChunk)
			return false;
		mem_zero(pChunk, AllocationSize);
		pChunk->m_Size = Size;
		pChunk->m_Addr = Session.m_Addr;
		if(Size != 0)
			mem_copy(pChunk->m_aData + Before, pData, Size);
		return true;
	}
};

#endif
