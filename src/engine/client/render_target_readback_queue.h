#ifndef ENGINE_CLIENT_RENDER_TARGET_READBACK_QUEUE_H
#define ENGINE_CLIENT_RENDER_TARGET_READBACK_QUEUE_H

#include <base/borrowed_allocator.h>
#include <base/sphore.h>

#include <engine/graphics.h>

#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

// 主线程管理请求；渲染线程只写分配时取得的 Image，并在写完后发出完成信号。
class CRenderTargetReadbackQueue
{
public:
	struct SAllocation
	{
		int m_Index = -1;
		uint32_t m_Generation = 0;
		CImageInfo *m_pImage = nullptr;
		CSemaphore *m_pCompletion = nullptr;
	};

private:
	enum class EState
	{
		FREE,
		PENDING,
		READY,
		FAILED,
		CANCELED,
	};

	struct SRequest
	{
		uint32_t m_Generation = 0;
		int m_NextFree = -1;
		EState m_State = EState::FREE;
		CImageInfo m_Image;
		std::unique_ptr<CSemaphore> m_pCompletion;
	};

	// 新增请求不能移动已交给渲染线程的 Image 地址。
	using CRequests = std::deque<SRequest, CBorrowedAllocator<SRequest>>;
	IAllocationResource *m_pResource;
	std::optional<CRequests> m_Requests;
	int m_FirstFree = -1;
	uint32_t m_Generation = 0;

	SRequest *Get(int Index, uint32_t Generation)
	{
		if(!m_Requests || Index < 0 || static_cast<size_t>(Index) >= m_Requests->size())
			return nullptr;
		SRequest &Request = (*m_Requests)[Index];
		if(Request.m_State == EState::FREE || Request.m_State == EState::CANCELED || Request.m_Generation != Generation)
			return nullptr;
		return &Request;
	}

	static void Update(SRequest &Request)
	{
		if(Request.m_State != EState::PENDING || Request.m_pCompletion->GetApproximateValue() <= 0)
			return;
		Request.m_pCompletion->Wait();
		Request.m_State = Request.m_Image.m_pData != nullptr ? EState::READY : EState::FAILED;
	}

	void Release(int Index)
	{
		SRequest &Request = (*m_Requests)[Index];
		Request.m_Image.Free();
		Request.m_State = EState::FREE;
		Request.m_NextFree = m_FirstFree;
		m_FirstFree = Index;
	}

	void CleanupCanceled()
	{
		if(!m_Requests)
			return;
		for(size_t Index = 0; Index < m_Requests->size(); ++Index)
		{
			SRequest &Request = (*m_Requests)[Index];
			if(Request.m_State != EState::CANCELED || Request.m_pCompletion->GetApproximateValue() <= 0)
				continue;
			Request.m_pCompletion->Wait();
			Release(static_cast<int>(Index));
		}
	}

public:
	// 内存域须存活到队列销毁，包括 Clear 后保留的容器存储。
	explicit CRenderTargetReadbackQueue(IAllocationResource *pResource = nullptr) noexcept :
		m_pResource(pResource) {}
	~CRenderTargetReadbackQueue() { Clear(); }

	SAllocation Allocate() noexcept
	{
		return Allocate([] { return CSemaphore::TryCreate(); });
	}

	// 工厂仅同步借用且不能重入队列；返回未发信号的新对象，失败不发布请求。
	template<typename TCreateCompletion>
	SAllocation Allocate(TCreateCompletion &&CreateCompletion) noexcept
	{
		CleanupCanceled();
		int Index = m_FirstFree;
		if(Index < 0)
		{
			if(m_Requests && (m_Requests->size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
						 m_Requests->size() == m_Requests->max_size()))
				return {};
			try
			{
				// 先准备信号，再扩容；任何分配失败都不留下未登记的空槽。
				std::unique_ptr<CSemaphore> pCompletion = CreateCompletion();
				if(pCompletion == nullptr)
					return {};
				// deque 构造本身也可能分配，放在同一失败回退内。
				if(!m_Requests)
					m_Requests.emplace(CBorrowedAllocator<SRequest>(m_pResource));
				Index = static_cast<int>(m_Requests->size());
				m_Requests->emplace_back();
				m_Requests->back().m_pCompletion = std::move(pCompletion);
			}
			catch(...)
			{
				return {};
			}
		}
		else
			m_FirstFree = (*m_Requests)[Index].m_NextFree;
		SRequest &Request = (*m_Requests)[Index];
		if(++m_Generation == 0)
			++m_Generation;
		Request.m_Generation = m_Generation;
		Request.m_NextFree = -1;
		Request.m_State = EState::PENDING;
		return {Index, Request.m_Generation, &Request.m_Image, Request.m_pCompletion.get()};
	}

	IGraphics::ERenderTargetReadbackState Poll(int Index, uint32_t Generation)
	{
		CleanupCanceled();
		SRequest *pRequest = Get(Index, Generation);
		if(pRequest == nullptr)
			return IGraphics::ERenderTargetReadbackState::INVALID;
		Update(*pRequest);
		if(pRequest->m_State == EState::READY)
			return IGraphics::ERenderTargetReadbackState::READY;
		if(pRequest->m_State == EState::FAILED)
			return IGraphics::ERenderTargetReadbackState::FAILED;
		return IGraphics::ERenderTargetReadbackState::PENDING;
	}

	bool Resolve(int Index, uint32_t Generation, CImageInfo &Image)
	{
		const auto State = Poll(Index, Generation);
		if(State == IGraphics::ERenderTargetReadbackState::FAILED)
			Release(Index);
		if(State != IGraphics::ERenderTargetReadbackState::READY)
			return false;
		SRequest &Request = (*m_Requests)[Index];
		Image = std::move(Request.m_Image);
		Release(Index);
		return true;
	}

	// 阻塞结算：等完成信号（渲染线程在读回命令内写完像素后点亮）再取像素。
	// 调用方须确保命令已被接纳，且延迟 PBO 已有后续读回或 Flush 负责结算。
	bool ResolveBlocking(int Index, uint32_t Generation, CImageInfo &Image)
	{
		SRequest *pRequest = Get(Index, Generation);
		if(pRequest == nullptr)
			return false;
		if(pRequest->m_State == EState::PENDING)
		{
			pRequest->m_pCompletion->Wait();
			pRequest->m_State = pRequest->m_Image.m_pData != nullptr ? EState::READY : EState::FAILED;
		}
		if(pRequest->m_State != EState::READY)
		{
			if(pRequest->m_State == EState::FAILED)
				Release(Index);
			return false;
		}
		Image = std::move(pRequest->m_Image);
		Release(Index);
		return true;
	}

	// 调用方先完成后端结算并确认空闲；同步后端允许直接检查像素。
	// 若已收到完成信号，必须消费它，不能把旧信号留给复用后的新请求。
	bool ResolveAfterIdle(int Index, uint32_t Generation, CImageInfo &Image)
	{
		SRequest *pRequest = Get(Index, Generation);
		if(pRequest == nullptr)
			return false;
		Update(*pRequest);
		if(pRequest->m_State == EState::PENDING)
			pRequest->m_State = pRequest->m_Image.m_pData != nullptr ? EState::READY : EState::FAILED;
		if(pRequest->m_State != EState::READY)
		{
			if(pRequest->m_State == EState::FAILED)
				Release(Index);
			return false;
		}
		Image = std::move(pRequest->m_Image);
		Release(Index);
		return true;
	}

	void Cancel(int Index, uint32_t Generation)
	{
		const auto State = Poll(Index, Generation);
		if(State == IGraphics::ERenderTargetReadbackState::READY || State == IGraphics::ERenderTargetReadbackState::FAILED)
			Release(Index);
		else if(State == IGraphics::ERenderTargetReadbackState::PENDING)
			(*m_Requests)[Index].m_State = EState::CANCELED;
		CleanupCanceled();
	}

	// 调用方必须先结束后端的全部借用，再销毁请求和信号；仅空闲但仍持有 PBO 不够。
	void Clear()
	{
		if(m_Requests)
		{
			for(SRequest &Request : *m_Requests)
				Request.m_Image.Free();
			m_Requests->clear();
		}
		m_FirstFree = -1;
		// 保留代数，设备重建后的新请求不能接管旧句柄。
	}
};

#endif
