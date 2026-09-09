#if defined(CONF_BACKEND_VULKAN)

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/client/backend/backend_base.h>
#include <engine/client/backend/vulkan/backend_vulkan.h>
#include <engine/client/backend_sdl.h>
#include <engine/client/graphics_threaded.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>

#include <SDL_video.h>
#include <SDL_vulkan.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef VK_API_VERSION_MAJOR
#define VK_API_VERSION_MAJOR VK_VERSION_MAJOR
#define VK_API_VERSION_MINOR VK_VERSION_MINOR
#define VK_API_VERSION_PATCH VK_VERSION_PATCH
#endif

// 旧版 Vulkan 头文件可能没有 1.4 宏，补一个与官方编码一致的定义
#ifndef VK_API_VERSION_1_4
#ifdef VK_MAKE_API_VERSION
#define VK_API_VERSION_1_4 VK_MAKE_API_VERSION(0, 1, 4, 0)
#else
#define VK_API_VERSION_1_4 VK_MAKE_VERSION(1, 4, 0)
#endif
#endif

using namespace std::chrono_literals;

class CCommandProcessorFragment_Vulkan : public CCommandProcessorFragment_GLBase
{
	enum EMemoryBlockUsage
	{
		MEMORY_BLOCK_USAGE_TEXTURE = 0,
		MEMORY_BLOCK_USAGE_BUFFER,
		MEMORY_BLOCK_USAGE_STREAM,
		MEMORY_BLOCK_USAGE_STAGING,

		// whenever dummy is used, make sure to deallocate all memory
		MEMORY_BLOCK_USAGE_DUMMY,
	};

	[[nodiscard]] bool IsVerbose()
	{
		return g_Config.m_DbgGfx == DEBUG_GFX_MODE_VERBOSE || g_Config.m_DbgGfx == DEBUG_GFX_MODE_ALL;
	}

	[[nodiscard]] bool FrameProfilingEnabled()
	{
#if defined(CONF_PLATFORM_MACOS)
		return IsVerbose() || g_Config.m_QmMacosGraphicsDiagnostics != 0;
#else
		return IsVerbose();
#endif
	}

	void VerboseAllocatedMemory(VkDeviceSize Size, size_t FrameImageIndex, EMemoryBlockUsage MemUsage) const
	{
		const char *pUsage = "unknown";
		switch(MemUsage)
		{
		case MEMORY_BLOCK_USAGE_TEXTURE:
			pUsage = "texture";
			break;
		case MEMORY_BLOCK_USAGE_BUFFER:
			pUsage = "buffer";
			break;
		case MEMORY_BLOCK_USAGE_STREAM:
			pUsage = "stream";
			break;
		case MEMORY_BLOCK_USAGE_STAGING:
			pUsage = "staging buffer";
			break;
		default: break;
		}
		dbg_msg("vulkan", "allocated chunk of memory with size: %" PRIzu " for frame %" PRIzu " (%s)", (size_t)Size, (size_t)m_CurImageIndex, pUsage);
	}

	void VerboseDeallocatedMemory(VkDeviceSize Size, size_t FrameImageIndex, EMemoryBlockUsage MemUsage) const
	{
		const char *pUsage = "unknown";
		switch(MemUsage)
		{
		case MEMORY_BLOCK_USAGE_TEXTURE:
			pUsage = "texture";
			break;
		case MEMORY_BLOCK_USAGE_BUFFER:
			pUsage = "buffer";
			break;
		case MEMORY_BLOCK_USAGE_STREAM:
			pUsage = "stream";
			break;
		case MEMORY_BLOCK_USAGE_STAGING:
			pUsage = "staging buffer";
			break;
		default: break;
		}
		dbg_msg("vulkan", "deallocated chunk of memory with size: %" PRIzu " for frame %" PRIzu " (%s)", (size_t)Size, (size_t)m_CurImageIndex, pUsage);
	}

	/************************
	 * STRUCT DEFINITIONS
	 ************************/

	static constexpr size_t STAGING_BUFFER_CACHE_ID = 0;
	static constexpr size_t STAGING_BUFFER_IMAGE_CACHE_ID = 1;
	static constexpr size_t VERTEX_BUFFER_CACHE_ID = 2;
	static constexpr size_t IMAGE_BUFFER_CACHE_ID = 3;
	static constexpr uint32_t FRAME_TIMESTAMP_QUERY_COUNT = 2;

	struct SDeviceMemoryBlock
	{
		VkDeviceMemory m_Mem = VK_NULL_HANDLE;
		VkDeviceSize m_Size = 0;
		EMemoryBlockUsage m_UsageType;
	};

	struct SDeviceDescriptorPools;

	struct SDeviceDescriptorSet
	{
		VkDescriptorSet m_Descriptor = VK_NULL_HANDLE;
		SDeviceDescriptorPools *m_pPools = nullptr;
		size_t m_PoolIndex = std::numeric_limits<size_t>::max();
	};

	struct SDeviceDescriptorPool
	{
		VkDescriptorPool m_Pool;
		VkDeviceSize m_Size = 0;
		VkDeviceSize m_CurSize = 0;
	};

	struct SDeviceDescriptorPools
	{
		std::vector<SDeviceDescriptorPool> m_vPools;
		VkDeviceSize m_DefaultAllocSize = 0;
		bool m_IsUniformPool = false;
	};

	// some mix of queue and binary tree
	struct SMemoryHeap
	{
		struct SMemoryHeapElement;
		struct SMemoryHeapQueueElement
		{
			size_t m_AllocationSize;
			// only useful information for the heap
			size_t m_OffsetInHeap;
			// useful for the user of this element
			size_t m_OffsetToAlign;
			SMemoryHeapElement *m_pElementInHeap;
			[[nodiscard]] bool operator>(const SMemoryHeapQueueElement &Other) const { return m_AllocationSize > Other.m_AllocationSize; }
			// respects alignment requirements
			constexpr bool CanFitAllocation(size_t AllocSize, size_t AllocAlignment) const
			{
				size_t ExtraSizeAlign = m_OffsetInHeap % AllocAlignment;
				if(ExtraSizeAlign != 0)
					ExtraSizeAlign = AllocAlignment - ExtraSizeAlign;
				size_t RealAllocSize = AllocSize + ExtraSizeAlign;
				return m_AllocationSize >= RealAllocSize;
			}
		};

		typedef std::multiset<SMemoryHeapQueueElement, std::greater<>> TMemoryHeapQueue;

		struct SMemoryHeapElement
		{
			size_t m_AllocationSize;
			size_t m_Offset;
			SMemoryHeapElement *m_pParent;
			std::unique_ptr<SMemoryHeapElement> m_pLeft;
			std::unique_ptr<SMemoryHeapElement> m_pRight;

			bool m_InUse;
			TMemoryHeapQueue::iterator m_InQueue;
		};

		SMemoryHeapElement m_Root;
		TMemoryHeapQueue m_Elements;

		void Init(size_t Size, size_t Offset)
		{
			m_Root.m_AllocationSize = Size;
			m_Root.m_Offset = Offset;
			m_Root.m_pParent = nullptr;
			m_Root.m_InUse = false;

			SMemoryHeapQueueElement QueueEl;
			QueueEl.m_AllocationSize = Size;
			QueueEl.m_OffsetInHeap = Offset;
			QueueEl.m_OffsetToAlign = Offset;
			QueueEl.m_pElementInHeap = &m_Root;
			m_Root.m_InQueue = m_Elements.insert(QueueEl);
		}

		[[nodiscard]] bool Allocate(size_t AllocSize, size_t AllocAlignment, SMemoryHeapQueueElement &AllocatedMemory)
		{
			if(m_Elements.empty())
			{
				return false;
			}
			else
			{
				// check if there is enough space in this instance
				if(!m_Elements.begin()->CanFitAllocation(AllocSize, AllocAlignment))
				{
					return false;
				}
				else
				{
					// see SMemoryHeapQueueElement::operator>
					SMemoryHeapQueueElement FindAllocSize;
					FindAllocSize.m_AllocationSize = AllocSize;
					// find upper bound for a allocation size
					auto Upper = m_Elements.upper_bound(FindAllocSize);
					// then find the first entry that respects alignment, this is a linear search!
					auto FoundEl = m_Elements.rend();
					for(auto AllocIterator = std::make_reverse_iterator(Upper); AllocIterator != m_Elements.rend(); ++AllocIterator)
					{
						if(AllocIterator->CanFitAllocation(AllocSize, AllocAlignment))
						{
							FoundEl = AllocIterator;
							break;
						}
					}

					auto TopEl = *FoundEl;
					m_Elements.erase(TopEl.m_pElementInHeap->m_InQueue);

					TopEl.m_pElementInHeap->m_InUse = true;

					// calculate the real alloc size + alignment offset
					size_t ExtraSizeAlign = TopEl.m_OffsetInHeap % AllocAlignment;
					if(ExtraSizeAlign != 0)
						ExtraSizeAlign = AllocAlignment - ExtraSizeAlign;
					size_t RealAllocSize = AllocSize + ExtraSizeAlign;

					// the heap element gets children
					TopEl.m_pElementInHeap->m_pLeft = std::make_unique<SMemoryHeapElement>();
					TopEl.m_pElementInHeap->m_pLeft->m_AllocationSize = RealAllocSize;
					TopEl.m_pElementInHeap->m_pLeft->m_Offset = TopEl.m_OffsetInHeap;
					TopEl.m_pElementInHeap->m_pLeft->m_pParent = TopEl.m_pElementInHeap;
					TopEl.m_pElementInHeap->m_pLeft->m_InUse = true;

					if(RealAllocSize < TopEl.m_AllocationSize)
					{
						SMemoryHeapQueueElement RemainingEl;
						RemainingEl.m_OffsetInHeap = TopEl.m_OffsetInHeap + RealAllocSize;
						RemainingEl.m_AllocationSize = TopEl.m_AllocationSize - RealAllocSize;

						TopEl.m_pElementInHeap->m_pRight = std::make_unique<SMemoryHeapElement>();
						TopEl.m_pElementInHeap->m_pRight->m_AllocationSize = RemainingEl.m_AllocationSize;
						TopEl.m_pElementInHeap->m_pRight->m_Offset = RemainingEl.m_OffsetInHeap;
						TopEl.m_pElementInHeap->m_pRight->m_pParent = TopEl.m_pElementInHeap;
						TopEl.m_pElementInHeap->m_pRight->m_InUse = false;

						RemainingEl.m_pElementInHeap = TopEl.m_pElementInHeap->m_pRight.get();
						RemainingEl.m_pElementInHeap->m_InQueue = m_Elements.insert(RemainingEl);
					}

					AllocatedMemory.m_pElementInHeap = TopEl.m_pElementInHeap->m_pLeft.get();
					AllocatedMemory.m_AllocationSize = RealAllocSize;
					AllocatedMemory.m_OffsetInHeap = TopEl.m_OffsetInHeap;
					AllocatedMemory.m_OffsetToAlign = TopEl.m_OffsetInHeap + ExtraSizeAlign;
					return true;
				}
			}
		}

		void Free(const SMemoryHeapQueueElement &AllocatedMemory)
		{
			bool ContinueFree = true;
			SMemoryHeapQueueElement ThisEl = AllocatedMemory;
			while(ContinueFree)
			{
				// first check if the other block is in use, if not merge them again
				SMemoryHeapElement *pThisHeapObj = ThisEl.m_pElementInHeap;
				SMemoryHeapElement *pThisParent = pThisHeapObj->m_pParent;
				pThisHeapObj->m_InUse = false;
				SMemoryHeapElement *pOtherHeapObj = nullptr;
				if(pThisParent != nullptr && pThisHeapObj == pThisParent->m_pLeft.get())
					pOtherHeapObj = pThisHeapObj->m_pParent->m_pRight.get();
				else if(pThisParent != nullptr)
					pOtherHeapObj = pThisHeapObj->m_pParent->m_pLeft.get();

				if((pThisParent != nullptr && pOtherHeapObj == nullptr) || (pOtherHeapObj != nullptr && !pOtherHeapObj->m_InUse))
				{
					// merge them
					if(pOtherHeapObj != nullptr)
					{
						m_Elements.erase(pOtherHeapObj->m_InQueue);
						pOtherHeapObj->m_InUse = false;
					}

					SMemoryHeapQueueElement ParentEl;
					ParentEl.m_OffsetInHeap = pThisParent->m_Offset;
					ParentEl.m_AllocationSize = pThisParent->m_AllocationSize;
					ParentEl.m_pElementInHeap = pThisParent;

					pThisParent->m_pLeft = nullptr;
					pThisParent->m_pRight = nullptr;

					ThisEl = ParentEl;
				}
				else
				{
					// else just put this back into queue
					ThisEl.m_pElementInHeap->m_InQueue = m_Elements.insert(ThisEl);
					ContinueFree = false;
				}
			}
		}

		[[nodiscard]] bool IsUnused() const
		{
			return !m_Root.m_InUse;
		}
	};

	template<size_t Id>
	struct SMemoryBlock
	{
		SMemoryHeap::SMemoryHeapQueueElement m_HeapData;

		VkDeviceSize m_UsedSize;

		// optional
		VkBuffer m_Buffer;

		SDeviceMemoryBlock m_BufferMem;
		void *m_pMappedBuffer;

		bool m_IsCached;
		SMemoryHeap *m_pHeap;
	};

	template<size_t Id>
	struct SMemoryImageBlock : public SMemoryBlock<Id>
	{
		uint32_t m_ImageMemoryBits;
	};

	template<size_t Id>
	struct SMemoryBlockCache
	{
		struct SMemoryCacheType
		{
			struct SMemoryCacheHeap
			{
				SMemoryHeap m_Heap;
				VkBuffer m_Buffer;

				SDeviceMemoryBlock m_BufferMem;
				void *m_pMappedBuffer;
			};
			std::vector<SMemoryCacheHeap *> m_vpMemoryHeaps;
		};
		SMemoryCacheType m_MemoryCaches;
		std::vector<std::vector<SMemoryBlock<Id>>> m_vvFrameDelayedCachedBufferCleanup;

		bool m_CanShrink = false;

		void Init(size_t SwapChainImageCount)
		{
			m_vvFrameDelayedCachedBufferCleanup.resize(SwapChainImageCount);
		}

		void DestroyFrameData(size_t ImageCount)
		{
			for(size_t i = 0; i < ImageCount; ++i)
				Cleanup(i);
			m_vvFrameDelayedCachedBufferCleanup.clear();
		}

		void Destroy(VkDevice &Device)
		{
			for(auto HeapIterator = m_MemoryCaches.m_vpMemoryHeaps.begin(); HeapIterator != m_MemoryCaches.m_vpMemoryHeaps.end();)
			{
				auto *pHeap = *HeapIterator;
				if(pHeap->m_pMappedBuffer != nullptr)
					vkUnmapMemory(Device, pHeap->m_BufferMem.m_Mem);
				if(pHeap->m_Buffer != VK_NULL_HANDLE)
					vkDestroyBuffer(Device, pHeap->m_Buffer, nullptr);
				vkFreeMemory(Device, pHeap->m_BufferMem.m_Mem, nullptr);

				delete pHeap;
				HeapIterator = m_MemoryCaches.m_vpMemoryHeaps.erase(HeapIterator);
			}

			m_MemoryCaches.m_vpMemoryHeaps.clear();
			m_vvFrameDelayedCachedBufferCleanup.clear();
		}

		void Cleanup(size_t ImgIndex)
		{
			for(auto &MemBlock : m_vvFrameDelayedCachedBufferCleanup[ImgIndex])
			{
				MemBlock.m_UsedSize = 0;
				MemBlock.m_pHeap->Free(MemBlock.m_HeapData);

				m_CanShrink = true;
			}
			m_vvFrameDelayedCachedBufferCleanup[ImgIndex].clear();
		}

		void FreeMemBlock(SMemoryBlock<Id> &Block, size_t ImgIndex)
		{
			m_vvFrameDelayedCachedBufferCleanup[ImgIndex].push_back(Block);
		}

		// returns the total free'd memory
		size_t Shrink(VkDevice &Device)
		{
			size_t FreedMemory = 0;
			if(m_CanShrink)
			{
				m_CanShrink = false;
				if(m_MemoryCaches.m_vpMemoryHeaps.size() > 1)
				{
					for(auto HeapIterator = m_MemoryCaches.m_vpMemoryHeaps.begin(); HeapIterator != m_MemoryCaches.m_vpMemoryHeaps.end();)
					{
						auto *pHeap = *HeapIterator;
						if(pHeap->m_Heap.IsUnused())
						{
							if(pHeap->m_pMappedBuffer != nullptr)
								vkUnmapMemory(Device, pHeap->m_BufferMem.m_Mem);
							if(pHeap->m_Buffer != VK_NULL_HANDLE)
								vkDestroyBuffer(Device, pHeap->m_Buffer, nullptr);
							vkFreeMemory(Device, pHeap->m_BufferMem.m_Mem, nullptr);
							FreedMemory += pHeap->m_BufferMem.m_Size;

							delete pHeap;
							HeapIterator = m_MemoryCaches.m_vpMemoryHeaps.erase(HeapIterator);
							if(m_MemoryCaches.m_vpMemoryHeaps.size() == 1)
								break;
						}
						else
							++HeapIterator;
					}
				}
			}

			return FreedMemory;
		}
	};

	struct CTexture
	{
		VkImage m_Img = VK_NULL_HANDLE;
		SMemoryImageBlock<IMAGE_BUFFER_CACHE_ID> m_ImgMem;
		VkImageView m_ImgView = VK_NULL_HANDLE;
		VkSampler m_aSamplers[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

		VkImage m_Img3D = VK_NULL_HANDLE;
		SMemoryImageBlock<IMAGE_BUFFER_CACHE_ID> m_Img3DMem;
		VkImageView m_Img3DView = VK_NULL_HANDLE;
		VkSampler m_Sampler3D = VK_NULL_HANDLE;

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_RescaleCount = 0;

		uint32_t m_MipMapCount = 1;

		std::array<SDeviceDescriptorSet, 2> m_aVKStandardTexturedDescrSets;
		SDeviceDescriptorSet m_VKStandard3DTexturedDescrSet;
		SDeviceDescriptorSet m_VKTextDescrSet;
	};

	struct SRenderTarget
	{
		VkImage m_Image = VK_NULL_HANDLE;
		SMemoryImageBlock<IMAGE_BUFFER_CACHE_ID> m_ImageMem;
		VkImageView m_ImageView = VK_NULL_HANDLE;
		VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
		VkImageLayout m_Layout = VK_IMAGE_LAYOUT_UNDEFINED;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		std::array<SDeviceDescriptorSet, 2> m_aVKStandardTexturedDescrSets;
	};

	struct SBufferObject
	{
		SMemoryBlock<VERTEX_BUFFER_CACHE_ID> m_Mem;
	};

	struct SBufferObjectFrame
	{
		SBufferObject m_BufferObject;

		// since stream buffers can be used the cur buffer should always be used for rendering
		bool m_IsStreamedBuffer = false;
		VkBuffer m_CurBuffer = VK_NULL_HANDLE;
		size_t m_CurBufferOffset = 0;
	};

	struct SBufferContainer
	{
		int m_BufferObjectIndex = -1;
	};

	struct SFrameBuffers
	{
		VkBuffer m_Buffer;
		SDeviceMemoryBlock m_BufferMem;
		size_t m_OffsetInBuffer = 0;
		size_t m_Size;
		size_t m_UsedSize;
		uint8_t *m_pMappedBufferData;

		SFrameBuffers(VkBuffer Buffer, SDeviceMemoryBlock BufferMem, size_t OffsetInBuffer, size_t Size, size_t UsedSize, uint8_t *pMappedBufferData) :
			m_Buffer(Buffer), m_BufferMem(BufferMem), m_OffsetInBuffer(OffsetInBuffer), m_Size(Size), m_UsedSize(UsedSize), m_pMappedBufferData(pMappedBufferData)
		{
		}
	};

	struct SFrameUniformBuffers : public SFrameBuffers
	{
		std::array<SDeviceDescriptorSet, 2> m_aUniformSets;

		SFrameUniformBuffers(VkBuffer Buffer, SDeviceMemoryBlock BufferMem, size_t OffsetInBuffer, size_t Size, size_t UsedSize, uint8_t *pMappedBufferData) :
			SFrameBuffers(Buffer, BufferMem, OffsetInBuffer, Size, UsedSize, pMappedBufferData) {}
	};

	template<typename TName>
	struct SStreamMemory
	{
		typedef std::vector<std::vector<TName>> TBufferObjectsOfFrame;
		typedef std::vector<std::vector<VkMappedMemoryRange>> TMemoryMapRangesOfFrame;
		typedef std::vector<size_t> TStreamUseCount;
		TBufferObjectsOfFrame m_vvBufferObjectsOfFrame;
		TMemoryMapRangesOfFrame m_vvBufferObjectsOfFrameRangeData;
		TStreamUseCount m_vCurrentUsedCount;

		std::vector<TName> &GetBuffers(size_t FrameImageIndex)
		{
			return m_vvBufferObjectsOfFrame[FrameImageIndex];
		}

		std::vector<VkMappedMemoryRange> &GetRanges(size_t FrameImageIndex)
		{
			return m_vvBufferObjectsOfFrameRangeData[FrameImageIndex];
		}

		size_t GetUsedCount(size_t FrameImageIndex)
		{
			return m_vCurrentUsedCount[FrameImageIndex];
		}

		void IncreaseUsedCount(size_t FrameImageIndex)
		{
			++m_vCurrentUsedCount[FrameImageIndex];
		}

		[[nodiscard]] bool IsUsed(size_t FrameImageIndex)
		{
			return GetUsedCount(FrameImageIndex) > 0;
		}

		void ResetFrame(size_t FrameImageIndex)
		{
			m_vCurrentUsedCount[FrameImageIndex] = 0;
		}

		void Init(size_t FrameImageCount)
		{
			m_vvBufferObjectsOfFrame.resize(FrameImageCount);
			m_vvBufferObjectsOfFrameRangeData.resize(FrameImageCount);
			m_vCurrentUsedCount.resize(FrameImageCount);
		}

		typedef std::function<void(size_t, TName &)> TDestroyBufferFunc;

		void Destroy(TDestroyBufferFunc &&DestroyBuffer)
		{
			size_t ImageIndex = 0;
			for(auto &vBuffersOfFrame : m_vvBufferObjectsOfFrame)
			{
				for(auto &BufferOfFrame : vBuffersOfFrame)
				{
					VkDeviceMemory BufferMem = BufferOfFrame.m_BufferMem.m_Mem;
					DestroyBuffer(ImageIndex, BufferOfFrame);

					// delete similar buffers
					for(auto &BufferOfFrameDel : vBuffersOfFrame)
					{
						if(BufferOfFrameDel.m_BufferMem.m_Mem == BufferMem)
						{
							BufferOfFrameDel.m_Buffer = VK_NULL_HANDLE;
							BufferOfFrameDel.m_BufferMem.m_Mem = VK_NULL_HANDLE;
						}
					}
				}
				++ImageIndex;
			}
			m_vvBufferObjectsOfFrame.clear();
			m_vvBufferObjectsOfFrameRangeData.clear();
			m_vCurrentUsedCount.clear();
		}
	};

	struct SShaderModule
	{
		VkShaderModule m_VertShaderModule = VK_NULL_HANDLE;
		VkShaderModule m_FragShaderModule = VK_NULL_HANDLE;

		VkDevice m_VKDevice = VK_NULL_HANDLE;

		~SShaderModule()
		{
			if(m_VKDevice != VK_NULL_HANDLE)
			{
				if(m_VertShaderModule != VK_NULL_HANDLE)
					vkDestroyShaderModule(m_VKDevice, m_VertShaderModule, nullptr);

				if(m_FragShaderModule != VK_NULL_HANDLE)
					vkDestroyShaderModule(m_VKDevice, m_FragShaderModule, nullptr);
			}
		}
	};

	enum EVulkanBackendAddressModes
	{
		VULKAN_BACKEND_ADDRESS_MODE_REPEAT = 0,
		VULKAN_BACKEND_ADDRESS_MODE_CLAMP_EDGES,

		VULKAN_BACKEND_ADDRESS_MODE_COUNT,
	};

	enum EVulkanBackendBlendModes
	{
		VULKAN_BACKEND_BLEND_MODE_ALPHA = 0,
		VULKAN_BACKEND_BLEND_MODE_NONE,
		VULKAN_BACKEND_BLEND_MODE_ADDITATIVE,

		VULKAN_BACKEND_BLEND_MODE_COUNT,
	};

	enum EVulkanBackendClipModes
	{
		VULKAN_BACKEND_CLIP_MODE_NONE = 0,
		VULKAN_BACKEND_CLIP_MODE_DYNAMIC_SCISSOR_AND_VIEWPORT,

		VULKAN_BACKEND_CLIP_MODE_COUNT,
	};

	enum EVulkanBackendTextureModes
	{
		VULKAN_BACKEND_TEXTURE_MODE_NOT_TEXTURED = 0,
		VULKAN_BACKEND_TEXTURE_MODE_TEXTURED,

		VULKAN_BACKEND_TEXTURE_MODE_COUNT,
	};

	struct SPipelineContainer
	{
		// 3 blend modes - 2 viewport & scissor modes - 2 texture modes
		std::array<std::array<std::array<VkPipelineLayout, VULKAN_BACKEND_TEXTURE_MODE_COUNT>, VULKAN_BACKEND_CLIP_MODE_COUNT>, VULKAN_BACKEND_BLEND_MODE_COUNT> m_aaaPipelineLayouts;
		std::array<std::array<std::array<VkPipeline, VULKAN_BACKEND_TEXTURE_MODE_COUNT>, VULKAN_BACKEND_CLIP_MODE_COUNT>, VULKAN_BACKEND_BLEND_MODE_COUNT> m_aaaPipelines;

		SPipelineContainer()
		{
			for(auto &aaPipeLayouts : m_aaaPipelineLayouts)
			{
				for(auto &aPipeLayouts : aaPipeLayouts)
				{
					for(auto &PipeLayout : aPipeLayouts)
					{
						PipeLayout = VK_NULL_HANDLE;
					}
				}
			}
			for(auto &aaPipe : m_aaaPipelines)
			{
				for(auto &aPipe : aaPipe)
				{
					for(auto &Pipe : aPipe)
					{
						Pipe = VK_NULL_HANDLE;
					}
				}
			}
		}

		void Destroy(VkDevice &Device)
		{
			for(auto &aaPipeLayouts : m_aaaPipelineLayouts)
			{
				for(auto &aPipeLayouts : aaPipeLayouts)
				{
					for(auto &PipeLayout : aPipeLayouts)
					{
						if(PipeLayout != VK_NULL_HANDLE)
							vkDestroyPipelineLayout(Device, PipeLayout, nullptr);
						PipeLayout = VK_NULL_HANDLE;
					}
				}
			}
			for(auto &aaPipe : m_aaaPipelines)
			{
				for(auto &aPipe : aaPipe)
				{
					for(auto &Pipe : aPipe)
					{
						if(Pipe != VK_NULL_HANDLE)
							vkDestroyPipeline(Device, Pipe, nullptr);
						Pipe = VK_NULL_HANDLE;
					}
				}
			}
		}
	};

	/*******************************
	 * UNIFORM PUSH CONSTANT LAYOUTS
	 ********************************/

	struct SUniformGPos
	{
		float m_aPos[4 * 2];
	};

	struct SUniformGaussianBlur
	{
		vec2 m_TexelOffset;
		int32_t m_Radius;
		int32_t m_Padding;
		std::array<float, IGraphics::GAUSSIAN_BLUR_MAX_RADIUS + 1> m_aWeights;
		float m_EndPadding;
	};
	static_assert(sizeof(SUniformGaussianBlur) == 64);

	struct SUniformGTextPos
	{
		float m_aPos[4 * 2];
		float m_TextureSize;
	};

	typedef vec3 SUniformTextGFragmentOffset;

	struct SUniformTextGFragmentConstants
	{
		ColorRGBA m_TextColor;
		ColorRGBA m_TextOutlineColor;
	};

	struct SUniformTextFragment
	{
		SUniformTextGFragmentConstants m_Constants;
	};

	struct SUniformTileGPos
	{
		float m_aPos[4 * 2];
	};

	struct SUniformTileGPosBorder : public SUniformTileGPos
	{
		vec2 m_Offset;
		vec2 m_Scale;
	};

	typedef ColorRGBA SUniformTileGVertColor;

	struct SUniformTileGVertColorAlign
	{
		float m_aPad[(64 - 48) / 4];
	};

	struct SUniformPrimExGPosRotationless
	{
		float m_aPos[4 * 2];
	};

	struct SUniformPrimExGPos : public SUniformPrimExGPosRotationless
	{
		vec2 m_Center;
		float m_Rotation;
	};

	typedef ColorRGBA SUniformPrimExGVertColor;

	struct SUniformPrimExGVertColorAlign
	{
		float m_aPad[(48 - 44) / 4];
	};

	struct SUniformSpriteMultiGPos
	{
		float m_aPos[4 * 2];
		vec2 m_Center;
	};

	typedef ColorRGBA SUniformSpriteMultiGVertColor;

	struct SUniformSpriteMultiGVertColorAlign
	{
		float m_aPad[(48 - 40) / 4];
	};

	struct SUniformSpriteMultiPushGPosBase
	{
		float m_aPos[4 * 2];
		vec2 m_Center;
		vec2 m_Padding;
	};

	struct SUniformSpriteMultiPushGPos : public SUniformSpriteMultiPushGPosBase
	{
		vec4 m_aPSR[1];
	};

	typedef ColorRGBA SUniformSpriteMultiPushGVertColor;

	struct SUniformQuadGPosBase
	{
		float m_aPos[4 * 2];
		int32_t m_QuadOffset;
	};

	struct SUniformQuadPushGBufferObject
	{
		ColorRGBA m_VertColor;
		vec2 m_Offset;
		float m_Rotation;
		float m_Padding;
	};

	struct SUniformQuadGroupedGPos
	{
		float m_aPos[4 * 2];
		SUniformQuadPushGBufferObject m_BOPush;
	};

	struct SUniformQuadGPos
	{
		float m_aPos[4 * 2];
		int32_t m_QuadOffset;
	};

	enum ESupportedSamplerTypes
	{
		SUPPORTED_SAMPLER_TYPE_REPEAT = 0,
		SUPPORTED_SAMPLER_TYPE_CLAMP_TO_EDGE,
		SUPPORTED_SAMPLER_TYPE_2D_TEXTURE_ARRAY,

		SUPPORTED_SAMPLER_TYPE_COUNT,
	};

	struct SShaderFileCache
	{
		std::vector<uint8_t> m_vBinary;
	};

	struct SSwapImgViewportExtent
	{
		VkExtent2D m_SwapImageViewport;
		bool m_HasForcedViewport = false;
		VkExtent2D m_ForcedViewport;

		// the viewport of the resulting presented image on the screen
		// if there is a forced viewport the resulting image is smaller
		// than the full swap image size
		VkExtent2D GetPresentedImageViewport() const
		{
			uint32_t ViewportWidth = m_SwapImageViewport.width;
			uint32_t ViewportHeight = m_SwapImageViewport.height;
			if(m_HasForcedViewport)
			{
				ViewportWidth = m_ForcedViewport.width;
				ViewportHeight = m_ForcedViewport.height;
			}

			return {ViewportWidth, ViewportHeight};
		}
	};

	struct SSwapChainMultiSampleImage
	{
		VkImage m_Image = VK_NULL_HANDLE;
		SMemoryImageBlock<IMAGE_BUFFER_CACHE_ID> m_ImgMem;
		VkImageView m_ImgView = VK_NULL_HANDLE;
	};

	/************************
	 * MEMBER VARIABLES
	 ************************/

	std::unordered_map<std::string, SShaderFileCache> m_ShaderFiles;

	SMemoryBlockCache<STAGING_BUFFER_CACHE_ID> m_StagingBufferCache;
	SMemoryBlockCache<STAGING_BUFFER_IMAGE_CACHE_ID> m_StagingBufferCacheImage;
	SMemoryBlockCache<VERTEX_BUFFER_CACHE_ID> m_VertexBufferCache;
	std::map<uint32_t, SMemoryBlockCache<IMAGE_BUFFER_CACHE_ID>> m_ImageBufferCaches;

	std::vector<VkMappedMemoryRange> m_vNonFlushedStagingBufferRange;

	std::vector<CTexture> m_vTextures;
	std::vector<SRenderTarget> m_vRenderTargets;

	std::atomic<uint64_t> *m_pTextureMemoryUsage;
	std::atomic<uint64_t> *m_pBufferMemoryUsage;
	std::atomic<uint64_t> *m_pStreamMemoryUsage;
	std::atomic<uint64_t> *m_pStagingMemoryUsage;

	TTwGraphicsGpuList *m_pGpuList;

	int m_GlobalTextureLodBIAS;
	uint32_t m_MultiSamplingCount = 1;

	uint32_t m_NextMultiSamplingCount = std::numeric_limits<uint32_t>::max();

	bool m_RecreateSwapChain = false;
	bool m_SwapchainCreated = false;
	bool m_RenderingPaused = false;
	bool m_HasDynamicViewport = false;
	bool m_ForceSingleThreadedRender = false;
	SBackendCapabilities *m_pBackendCapabilities = nullptr;
	VkOffset2D m_DynamicViewportOffset;
	VkExtent2D m_DynamicViewportSize;

	bool m_AllowsLinearBlitting = false;
	bool m_OptimalSwapChainImageBlitting = false;
	bool m_OptimalSwapChainImageLinearBlitting = false;
	bool m_OptimalRGBAImageBlitting = false;
	bool m_LinearRGBAImageBlitting = false;

	VkBuffer m_IndexBuffer;
	SDeviceMemoryBlock m_IndexBufferMemory;

	VkBuffer m_RenderIndexBuffer;
	SDeviceMemoryBlock m_RenderIndexBufferMemory;
	size_t m_CurRenderIndexPrimitiveCount;

	VkDeviceSize m_NonCoherentMemAlignment;
	VkDeviceSize m_OptimalImageCopyMemAlignment;
	uint32_t m_MaxTextureSize;
	uint32_t m_MaxSamplerAnisotropy;
	VkSampleCountFlags m_MaxMultiSample;

	uint32_t m_MinUniformAlign;

	std::vector<uint8_t> m_vReadPixelHelper;
	std::vector<uint8_t> m_vScreenshotHelper;

	SDeviceMemoryBlock m_GetPresentedImgDataHelperMem;
	VkImage m_GetPresentedImgDataHelperImage = VK_NULL_HANDLE;
	uint8_t *m_pGetPresentedImgDataHelperMappedMemory = nullptr;
	VkDeviceSize m_GetPresentedImgDataHelperMappedLayoutOffset = 0;
	VkDeviceSize m_GetPresentedImgDataHelperMappedLayoutPitch = 0;
	uint32_t m_GetPresentedImgDataHelperWidth = 0;
	uint32_t m_GetPresentedImgDataHelperHeight = 0;
	VkFence m_GetPresentedImgDataHelperFence = VK_NULL_HANDLE;

	std::array<VkSampler, SUPPORTED_SAMPLER_TYPE_COUNT> m_aSamplers;

	class IStorage *m_pStorage;

	struct SDelayedBufferCleanupItem
	{
		VkBuffer m_Buffer;
		SDeviceMemoryBlock m_Mem;
		void *m_pMappedData = nullptr;
	};

	std::vector<std::vector<SDelayedBufferCleanupItem>> m_vvFrameDelayedBufferCleanup;
	std::vector<std::vector<CTexture>> m_vvFrameDelayedTextureCleanup;
	std::vector<std::vector<std::pair<CTexture, CTexture>>> m_vvFrameDelayedTextTexturesCleanup;

	struct SFrameProfileStats
	{
		uint64_t m_CommandCount = 0;
		uint64_t m_EstimatedRenderCallCount = 0;
		uint64_t m_RenderCommands = 0;
		uint64_t m_CommandPrepares = 0;
		uint64_t m_MainCommandRecords = 0;
		uint64_t m_ThreadCommandRecords = 0;
		uint64_t m_DrawCalls = 0;
		uint64_t m_PipelineBinds = 0;
		uint64_t m_PipelineBindSkips = 0;
		uint64_t m_VertexBufferBinds = 0;
		uint64_t m_VertexBufferBindSkips = 0;
		uint64_t m_IndexBufferBinds = 0;
		uint64_t m_IndexBufferBindSkips = 0;
		uint64_t m_DynamicStateSets = 0;
		uint64_t m_DynamicStateSetSkips = 0;
		uint64_t m_DescriptorBinds = 0;
		uint64_t m_DescriptorBindSkips = 0;
		uint64_t m_DescriptorPoolAllocations = 0;
		uint64_t m_DescriptorAllocations = 0;
		uint64_t m_DescriptorUpdates = 0;
		uint64_t m_StreamUploads = 0;
		uint64_t m_StreamUploadBytes = 0;
		uint64_t m_StreamBufferAllocations = 0;
		uint64_t m_StreamBufferAllocationBytes = 0;
		uint64_t m_StagingUploads = 0;
		uint64_t m_StagingUploadBytes = 0;
		uint64_t m_StagingBufferAllocations = 0;
		uint64_t m_StagingBufferAllocationBytes = 0;
		uint64_t m_FlushCalls = 0;
		uint64_t m_FlushRanges = 0;
		uint64_t m_InvalidateCalls = 0;
		uint64_t m_InvalidateRanges = 0;
		uint64_t m_ReadbackRequests = 0;
		uint64_t m_ReadbackBytes = 0;
		uint64_t m_BarrierCalls = 0;
		uint64_t m_Barriers = 0;
		uint64_t m_QueueSubmits = 0;
		uint64_t m_QueueSubmitCommandBuffers = 0;
		uint64_t m_FenceWaits = 0;
		uint64_t m_QueueWaits = 0;
		uint64_t m_DeviceWaits = 0;
		std::chrono::nanoseconds m_CPUFrameTime = 0ns;
		std::chrono::nanoseconds m_GPUFrameTime = 0ns;
		std::chrono::nanoseconds m_CPUCommandPrepareTime = 0ns;
		std::chrono::nanoseconds m_CPUMainCommandRecordTime = 0ns;
		std::chrono::nanoseconds m_CPUThreadCommandRecordTime = 0ns;
		std::chrono::nanoseconds m_CPUFenceWaitTime = 0ns;
		std::chrono::nanoseconds m_CPUQueueSubmitTime = 0ns;
		std::chrono::nanoseconds m_CPUQueueWaitTime = 0ns;
		std::chrono::nanoseconds m_CPUDeviceWaitTime = 0ns;
		std::chrono::nanoseconds m_CPUInvalidateTime = 0ns;
		bool m_HasGPUFrameTime = false;

		void Add(const SFrameProfileStats &Other)
		{
			m_CommandCount += Other.m_CommandCount;
			m_EstimatedRenderCallCount += Other.m_EstimatedRenderCallCount;
			m_RenderCommands += Other.m_RenderCommands;
			m_CommandPrepares += Other.m_CommandPrepares;
			m_MainCommandRecords += Other.m_MainCommandRecords;
			m_ThreadCommandRecords += Other.m_ThreadCommandRecords;
			m_DrawCalls += Other.m_DrawCalls;
			m_PipelineBinds += Other.m_PipelineBinds;
			m_PipelineBindSkips += Other.m_PipelineBindSkips;
			m_VertexBufferBinds += Other.m_VertexBufferBinds;
			m_VertexBufferBindSkips += Other.m_VertexBufferBindSkips;
			m_IndexBufferBinds += Other.m_IndexBufferBinds;
			m_IndexBufferBindSkips += Other.m_IndexBufferBindSkips;
			m_DynamicStateSets += Other.m_DynamicStateSets;
			m_DynamicStateSetSkips += Other.m_DynamicStateSetSkips;
			m_DescriptorBinds += Other.m_DescriptorBinds;
			m_DescriptorBindSkips += Other.m_DescriptorBindSkips;
			m_DescriptorPoolAllocations += Other.m_DescriptorPoolAllocations;
			m_DescriptorAllocations += Other.m_DescriptorAllocations;
			m_DescriptorUpdates += Other.m_DescriptorUpdates;
			m_StreamUploads += Other.m_StreamUploads;
			m_StreamUploadBytes += Other.m_StreamUploadBytes;
			m_StreamBufferAllocations += Other.m_StreamBufferAllocations;
			m_StreamBufferAllocationBytes += Other.m_StreamBufferAllocationBytes;
			m_StagingUploads += Other.m_StagingUploads;
			m_StagingUploadBytes += Other.m_StagingUploadBytes;
			m_StagingBufferAllocations += Other.m_StagingBufferAllocations;
			m_StagingBufferAllocationBytes += Other.m_StagingBufferAllocationBytes;
			m_FlushCalls += Other.m_FlushCalls;
			m_FlushRanges += Other.m_FlushRanges;
			m_InvalidateCalls += Other.m_InvalidateCalls;
			m_InvalidateRanges += Other.m_InvalidateRanges;
			m_ReadbackRequests += Other.m_ReadbackRequests;
			m_ReadbackBytes += Other.m_ReadbackBytes;
			m_BarrierCalls += Other.m_BarrierCalls;
			m_Barriers += Other.m_Barriers;
			m_QueueSubmits += Other.m_QueueSubmits;
			m_QueueSubmitCommandBuffers += Other.m_QueueSubmitCommandBuffers;
			m_FenceWaits += Other.m_FenceWaits;
			m_QueueWaits += Other.m_QueueWaits;
			m_DeviceWaits += Other.m_DeviceWaits;
			m_CPUFrameTime += Other.m_CPUFrameTime;
			m_CPUCommandPrepareTime += Other.m_CPUCommandPrepareTime;
			m_CPUMainCommandRecordTime += Other.m_CPUMainCommandRecordTime;
			m_CPUThreadCommandRecordTime += Other.m_CPUThreadCommandRecordTime;
			m_CPUQueueWaitTime += Other.m_CPUQueueWaitTime;
			m_CPUDeviceWaitTime += Other.m_CPUDeviceWaitTime;
			m_CPUInvalidateTime += Other.m_CPUInvalidateTime;
			if(Other.m_HasGPUFrameTime)
			{
				m_GPUFrameTime = Other.m_GPUFrameTime;
				m_HasGPUFrameTime = true;
			}
			m_CPUFenceWaitTime += Other.m_CPUFenceWaitTime;
			m_CPUQueueSubmitTime += Other.m_CPUQueueSubmitTime;
		}
	};

	struct SDrawCommandState
	{
		VkBuffer m_VertexBuffer = VK_NULL_HANDLE;
		VkDeviceSize m_VertexBufferOffset = std::numeric_limits<VkDeviceSize>::max();
		VkBuffer m_IndexBuffer = VK_NULL_HANDLE;
		VkDeviceSize m_IndexBufferOffset = std::numeric_limits<VkDeviceSize>::max();
		VkIndexType m_IndexBufferType = VK_INDEX_TYPE_MAX_ENUM;
		std::array<VkDescriptorSet, 2> m_aDescriptorSets = {VK_NULL_HANDLE, VK_NULL_HANDLE};
		std::array<VkPipelineLayout, 2> m_aDescriptorPipelineLayouts = {VK_NULL_HANDLE, VK_NULL_HANDLE};
		bool m_HasDynamicViewportScissor = false;
		VkViewport m_Viewport = {};
		VkRect2D m_Scissor = {};
	};

	SFrameProfileStats m_FrameProfileStats;
	std::vector<SFrameProfileStats> m_vThreadFrameProfileStats;
	std::vector<SDrawCommandState> m_vDrawCommandStates;
	std::vector<VkMappedMemoryRange> m_vFrameProfileMergedFlushRanges;
	std::chrono::nanoseconds m_FrameProfileStartTime = 0ns;
	uint64_t m_LastFrameProfileLogFrame = 0;
	VkQueryPool m_FrameTimestampQueryPool = VK_NULL_HANDLE;
	std::vector<bool> m_vFrameTimestampQueriesPending;
	float m_FrameTimestampPeriod = 0.0f;
	uint32_t m_FrameTimestampValidBits = 0;
	bool m_FrameTimestampQueriesSupported = false;
	bool m_FrameTimestampQueryRecorded = false;
	bool m_FrameProfilingActive = false;
	uint32_t m_RequestedApiVersion = VK_API_VERSION_1_1;
	uint32_t m_EffectiveApiVersion = VK_API_VERSION_1_1;
	VkResult m_LastVulkanInstanceCreateResult = VK_SUCCESS;
	bool m_RequiredVulkanVersionUnavailable = false;

	size_t m_ThreadCount = 1;
	static constexpr size_t MAIN_THREAD_INDEX = 0;
	size_t m_CurCommandInPipe = 0;
	size_t m_CurRenderCallCountInPipe = 0;
	size_t m_CommandsInPipe = 0;
	size_t m_RenderCallsInPipe = 0;
	size_t m_LastCommandsInPipeThreadIndex = 0;

	struct SRenderThread
	{
		bool m_IsRendering = false;
		std::thread m_Thread;
		std::mutex m_Mutex;
		std::condition_variable m_Cond;
		bool m_Finished = false;
		bool m_Started = false;
	};
	std::vector<std::unique_ptr<SRenderThread>> m_vpRenderThreads;

private:
	std::vector<VkImageView> m_vSwapChainImageViewList;
	std::vector<SSwapChainMultiSampleImage> m_vSwapChainMultiSamplingImages;
	std::vector<VkFramebuffer> m_vFramebufferList;
	std::vector<VkCommandBuffer> m_vMainDrawCommandBuffers;

	std::vector<std::vector<VkCommandBuffer>> m_vvThreadDrawCommandBuffers;
	std::vector<VkCommandBuffer> m_vHelperThreadDrawCommandBuffers;
	std::vector<std::vector<bool>> m_vvUsedThreadDrawCommandBuffer;

	std::vector<VkCommandBuffer> m_vMemoryCommandBuffers;
	std::vector<bool> m_vUsedMemoryCommandBuffer;
	std::vector<VkFence> m_vMemoryCommandBufferFences;

	std::vector<VkSemaphore> m_vQueueSubmitSemaphores;
	std::vector<VkSemaphore> m_vBusyAcquireImageSemaphores;
	VkSemaphore m_AcquireImageSemaphore = VK_NULL_HANDLE;

	std::vector<VkFence> m_vQueueSubmitFences;

	uint64_t m_CurFrame = 0;
	std::vector<uint64_t> m_vImageLastFrameCheck;

	uint32_t m_LastPresentedSwapChainImageIndex;

	std::vector<SBufferObjectFrame> m_vBufferObjects;

	std::vector<SBufferContainer> m_vBufferContainers;

	VkInstance m_VKInstance = VK_NULL_HANDLE;
	VkPhysicalDevice m_VKGPU;
	uint32_t m_VKGraphicsQueueIndex = std::numeric_limits<uint32_t>::max();
	VkDevice m_VKDevice;
	VkQueue m_VKGraphicsQueue, m_VKPresentQueue;
	VkSurfaceKHR m_VKPresentSurface;
	SSwapImgViewportExtent m_VKSwapImgAndViewportExtent;

#ifdef VK_EXT_debug_utils
	VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
#endif

	VkDescriptorSetLayout m_StandardTexturedDescriptorSetLayout;
	VkDescriptorSetLayout m_Standard3DTexturedDescriptorSetLayout;

	VkDescriptorSetLayout m_TextDescriptorSetLayout;

	VkDescriptorSetLayout m_SpriteMultiUniformDescriptorSetLayout;
	VkDescriptorSetLayout m_QuadUniformDescriptorSetLayout;

	SPipelineContainer m_StandardPipeline;
	SPipelineContainer m_StandardLinePipeline;
	SPipelineContainer m_Standard3DPipeline;
	SPipelineContainer m_TextPipeline;
	SPipelineContainer m_TilePipeline;
	SPipelineContainer m_TileBorderPipeline;
	SPipelineContainer m_PrimExPipeline;
	SPipelineContainer m_PrimExRotationlessPipeline;
	SPipelineContainer m_SpriteMultiPipeline;
	SPipelineContainer m_SpriteMultiPushPipeline;
	SPipelineContainer m_QuadPipeline;
	SPipelineContainer m_QuadGroupedPipeline;
	SPipelineContainer m_MediaIslandSdfPipeline;
	SPipelineContainer m_RoundedRectSdfPipeline;
	SPipelineContainer m_TexturedMsdfPipeline;
	bool m_TexturedMsdfPipelineValid = false;
	bool m_TexturedMsdfPipelineRequired = false;
	SPipelineContainer m_GaussianBlurPipeline;
	bool m_GaussianBlurPipelineValid = false;

	void SyncTexturedMsdfCapability()
	{
		if(m_pBackendCapabilities != nullptr)
			m_pBackendCapabilities->m_TexturedMsdf.store(m_TexturedMsdfPipelineValid, std::memory_order_release);
	}

	std::vector<VkPipeline> m_vLastPipeline;

	std::vector<VkCommandPool> m_vCommandPools;

	VkRenderPass m_VKRenderPass;
	VkRenderPass m_VKRenderPassLoad = VK_NULL_HANDLE;
	VkRenderPass m_VKRenderTargetRenderPass = VK_NULL_HANDLE;

	VkSurfaceFormatKHR m_VKSurfFormat;

	SDeviceDescriptorPools m_StandardTextureDescrPool;
	SDeviceDescriptorPools m_TextTextureDescrPool;

	std::vector<SDeviceDescriptorPools> m_vUniformBufferDescrPools;

	VkSwapchainKHR m_VKSwapChain = VK_NULL_HANDLE;
	std::vector<VkImage> m_vSwapChainImages;
	uint32_t m_SwapChainImageCount = 0;
	PFN_vkCreateSwapchainKHR m_pfnCreateSwapchainKHR = nullptr;
	PFN_vkDestroySwapchainKHR m_pfnDestroySwapchainKHR = nullptr;
	PFN_vkGetSwapchainImagesKHR m_pfnGetSwapchainImagesKHR = nullptr;
	PFN_vkAcquireNextImageKHR m_pfnAcquireNextImageKHR = nullptr;
	PFN_vkQueuePresentKHR m_pfnQueuePresentKHR = nullptr;

	bool m_SwapRenderPassActive = false;
	bool m_RenderTargetActive = false;
	bool m_AcquireSemaphoreConsumed = false;
	int m_ActiveRenderTargetId = -1;
	// 渲染目标渲染通道进行中收到的销毁请求，延迟到 Cmd_RenderTarget_End 后统一处理（目标 id + 当时的 image 句柄，防止重建复用后误销毁）
	std::vector<std::pair<size_t, VkImage>> m_vPendingRenderTargetDestroy;
	bool m_SavedHasDynamicViewport = false;
	VkOffset2D m_SavedDynamicViewportOffset{};
	VkExtent2D m_SavedDynamicViewportSize{};

	std::vector<SStreamMemory<SFrameBuffers>> m_vStreamedVertexBuffers;
	std::vector<SStreamMemory<SFrameUniformBuffers>> m_vStreamedUniformBuffers;

	uint32_t m_CurImageIndex = 0;

	uint32_t m_CanvasWidth;
	uint32_t m_CanvasHeight;

	SDL_Window *m_pWindow;

	std::array<float, 4> m_aClearColor = {0, 0, 0, 0};

	struct SRenderCommandExecuteBuffer
	{
		CCommandBuffer::ECommandBufferCMD m_Command;
		const CCommandBuffer::SCommand *m_pRawCommand;
		uint32_t m_ThreadIndex;

		// must be calculated when the buffer gets filled
		size_t m_EstimatedRenderCallCount = 0;

		// useful data
		VkBuffer m_Buffer = VK_NULL_HANDLE;
		size_t m_BufferOff = 0;
		std::array<SDeviceDescriptorSet, 2> m_aDescriptors;

		VkBuffer m_IndexBuffer = VK_NULL_HANDLE;
		bool m_ClearColorInRenderThread = false;

		bool m_HasDynamicState = false;
		VkViewport m_Viewport;
		VkRect2D m_Scissor;
	};

	typedef std::vector<SRenderCommandExecuteBuffer> TCommandList;
	typedef std::vector<TCommandList> TThreadCommandList;

	TThreadCommandList m_vvThreadCommandLists;
	std::vector<bool> m_vThreadHelperHadCommands;

	typedef std::function<bool(const CCommandBuffer::SCommand *, SRenderCommandExecuteBuffer &)> TCommandBufferCommandCallback;
	typedef std::function<void(SRenderCommandExecuteBuffer &, const CCommandBuffer::SCommand *)> TCommandBufferFillExecuteBufferFunc;

	struct SCommandCallback
	{
		bool m_IsRenderCommand;
		TCommandBufferFillExecuteBufferFunc m_FillExecuteBuffer;
		TCommandBufferCommandCallback m_CommandCB;
		// command should be considered handled after it executed
		bool m_CMDIsHandled = true;
	};
	std::array<SCommandCallback, static_cast<int>(CCommandBuffer::CMD_COUNT) - static_cast<int>(CCommandBuffer::CMD_FIRST)> m_aCommandCallbacks;

protected:
	/************************
	 * ERROR MANAGEMENT
	 ************************/
	std::mutex m_ErrWarnMutex;
	std::string m_ErrorHelper;

	bool m_HasError = false;
	bool m_CanAssert = false;

	/**
	 * After an error occurred, the rendering stop as soon as possible
	 * Always stop the current code execution after a call to this function (e.g. return false)
	 */
	void SetError(EGfxErrorType ErrType, const char *pErr, const char *pErrStrExtra = nullptr)
	{
		std::unique_lock<std::mutex> Lock(m_ErrWarnMutex);
		SGfxErrorContainer::SError Err = {false, pErr};
		if(std::find(m_Error.m_vErrors.begin(), m_Error.m_vErrors.end(), Err) == m_Error.m_vErrors.end())
			m_Error.m_vErrors.emplace_back(Err);
		if(pErrStrExtra != nullptr)
		{
			SGfxErrorContainer::SError ErrExtra = {false, pErrStrExtra};
			if(std::find(m_Error.m_vErrors.begin(), m_Error.m_vErrors.end(), ErrExtra) == m_Error.m_vErrors.end())
				m_Error.m_vErrors.emplace_back(ErrExtra);
		}
		if(m_CanAssert)
		{
			if(pErrStrExtra != nullptr)
				dbg_msg("vulkan", "vulkan error: %s: %s", pErr, pErrStrExtra);
			else
				dbg_msg("vulkan", "vulkan error: %s", pErr);
			m_HasError = true;
			m_Error.m_ErrorType = ErrType;
		}
		else
		{
			Lock.unlock();
			// during initialization vulkan should not throw any errors but warnings instead
			// since most code in the swapchain is shared with runtime code, add this extra code path
			SetWarning(EGfxWarningType::GFX_WARNING_TYPE_INIT_FAILED, pErr);
		}
	}

	void SetWarningPreMsg(const char *pWarningPre)
	{
		std::unique_lock<std::mutex> Lock(m_ErrWarnMutex);
		if(std::find(m_Warning.m_vWarnings.begin(), m_Warning.m_vWarnings.end(), pWarningPre) == m_Warning.m_vWarnings.end())
			m_Warning.m_vWarnings.emplace(m_Warning.m_vWarnings.begin(), pWarningPre);
	}

	void SetWarning(EGfxWarningType WarningType, const char *pWarning)
	{
		std::unique_lock<std::mutex> Lock(m_ErrWarnMutex);
		dbg_msg("vulkan", "vulkan warning: %s", pWarning);
		if(std::find(m_Warning.m_vWarnings.begin(), m_Warning.m_vWarnings.end(), pWarning) == m_Warning.m_vWarnings.end())
			m_Warning.m_vWarnings.emplace_back(pWarning);
		m_Warning.m_WarningType = WarningType;
	}

	void ResetInitializationDiagnostics()
	{
		std::unique_lock<std::mutex> Lock(m_ErrWarnMutex);
		m_Error = {};
		m_Warning = {};
		m_ErrorHelper.clear();
		m_HasError = false;
	}

	const char *CheckVulkanCriticalError(VkResult CallResult)
	{
		const char *pCriticalError = nullptr;
		switch(CallResult)
		{
		case VK_ERROR_OUT_OF_HOST_MEMORY:
			pCriticalError = "host ran out of memory";
			dbg_msg("vulkan", "%s", pCriticalError);
			break;
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:
			pCriticalError = "device ran out of memory";
			dbg_msg("vulkan", "%s", pCriticalError);
			break;
		case VK_ERROR_DEVICE_LOST:
			pCriticalError = "device lost";
			dbg_msg("vulkan", "%s", pCriticalError);
			break;
		case VK_ERROR_OUT_OF_DATE_KHR:
		{
			if(IsVerbose())
			{
				dbg_msg("vulkan", "queueing swap chain recreation because the current is out of date");
			}
			m_RecreateSwapChain = true;
			break;
		}
		case VK_ERROR_SURFACE_LOST_KHR:
			dbg_msg("vulkan", "surface lost");
			break;
		/*case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
			dbg_msg("vulkan", "fullscreen exclusive mode lost");
			break;*/
		case VK_ERROR_INCOMPATIBLE_DRIVER:
			pCriticalError = "no compatible driver found. Vulkan 1.1 is required.";
			dbg_msg("vulkan", "%s", pCriticalError);
			break;
		case VK_ERROR_INITIALIZATION_FAILED:
			pCriticalError = "initialization failed for unknown reason.";
			dbg_msg("vulkan", "%s", pCriticalError);
			break;
		case VK_ERROR_LAYER_NOT_PRESENT:
			SetWarning(EGfxWarningType::GFX_WARNING_MISSING_EXTENSION, "One Vulkan layer was not present. (try to disable them)");
			break;
		case VK_ERROR_EXTENSION_NOT_PRESENT:
			SetWarning(EGfxWarningType::GFX_WARNING_MISSING_EXTENSION, "One Vulkan extension was not present. (try to disable them)");
			break;
		case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
			dbg_msg("vulkan", "native window in use");
			break;
		case VK_SUCCESS:
			break;
		case VK_SUBOPTIMAL_KHR:
			if(IsVerbose())
			{
				dbg_msg("vulkan", "queueing swap chain recreation because the current is sub optimal");
			}
			m_RecreateSwapChain = true;
			break;
		default:
			m_ErrorHelper = "unknown error: ";
			m_ErrorHelper.append(std::to_string(CallResult));
			pCriticalError = m_ErrorHelper.c_str();
			break;
		}

		return pCriticalError;
	}

	void ErroneousCleanup() override
	{
		CleanupVulkanSDL();
	}

	/************************
	 * COMMAND CALLBACKS
	 ************************/

	size_t CommandBufferCMDOff(CCommandBuffer::ECommandBufferCMD CommandBufferCMD)
	{
		return (size_t)CommandBufferCMD - CCommandBuffer::CMD_FIRST;
	}

	void RegisterCommands()
	{
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_TEXTURE_CREATE)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_Texture_Create(static_cast<const CCommandBuffer::SCommand_Texture_Create *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_TEXTURE_DESTROY)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_Texture_Destroy(static_cast<const CCommandBuffer::SCommand_Texture_Destroy *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_TEXTURE_UPDATE)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_Texture_Update(static_cast<const CCommandBuffer::SCommand_Texture_Update *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_TEXT_TEXTURES_CREATE)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_TextTextures_Create(static_cast<const CCommandBuffer::SCommand_TextTextures_Create *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_TEXT_TEXTURES_DESTROY)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_TextTextures_Destroy(static_cast<const CCommandBuffer::SCommand_TextTextures_Destroy *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_TEXT_TEXTURE_UPDATE)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_TextTexture_Update(static_cast<const CCommandBuffer::SCommand_TextTexture_Update *>(pBaseCommand)); }};

		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_CLEAR)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_Clear_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_Clear *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_Clear(ExecBuffer, static_cast<const CCommandBuffer::SCommand_Clear *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_Render_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_Render *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_Render(static_cast<const CCommandBuffer::SCommand_Render *>(pBaseCommand), ExecBuffer); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_MEDIA_ISLAND_SDF)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderMediaIslandSdf_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderMediaIslandSdf *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderMediaIslandSdf(static_cast<const CCommandBuffer::SCommand_RenderMediaIslandSdf *>(pBaseCommand), ExecBuffer); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_ROUNDED_RECT_SDF)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderRoundedRectSdf_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderRoundedRectSdf *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderRoundedRectSdf(static_cast<const CCommandBuffer::SCommand_RenderRoundedRectSdf *>(pBaseCommand), ExecBuffer); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TEXTURED_MSDF)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderTexturedMsdf_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderTexturedMsdf *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTexturedMsdf(static_cast<const CCommandBuffer::SCommand_RenderTexturedMsdf *>(pBaseCommand), ExecBuffer); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TEX3D)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderTex3D_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderTex3D *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTex3D(static_cast<const CCommandBuffer::SCommand_RenderTex3D *>(pBaseCommand), ExecBuffer); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_CREATE)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_Create(static_cast<const CCommandBuffer::SCommand_RenderTarget_Create *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_DESTROY)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_Destroy(static_cast<const CCommandBuffer::SCommand_RenderTarget_Destroy *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_BEGIN)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_Begin(static_cast<const CCommandBuffer::SCommand_RenderTarget_Begin *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_END)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_End(static_cast<const CCommandBuffer::SCommand_RenderTarget_End *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_DRAW)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_Draw(static_cast<const CCommandBuffer::SCommand_RenderTarget_Draw *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_CAPTURE_BACKBUFFER)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_CaptureBackbuffer(static_cast<const CCommandBuffer::SCommand_RenderTarget_CaptureBackbuffer *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_GAUSSIAN_BLUR_PASS)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_GaussianBlurPass(static_cast<const CCommandBuffer::SCommand_RenderTarget_GaussianBlurPass *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TARGET_READBACK)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTarget_Readback(static_cast<const CCommandBuffer::SCommand_RenderTarget_Readback *>(pBaseCommand)); }};

		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_CREATE_BUFFER_OBJECT)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_CreateBufferObject(static_cast<const CCommandBuffer::SCommand_CreateBufferObject *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RECREATE_BUFFER_OBJECT)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RecreateBufferObject(static_cast<const CCommandBuffer::SCommand_RecreateBufferObject *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_UPDATE_BUFFER_OBJECT)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_UpdateBufferObject(static_cast<const CCommandBuffer::SCommand_UpdateBufferObject *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_COPY_BUFFER_OBJECT)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_CopyBufferObject(static_cast<const CCommandBuffer::SCommand_CopyBufferObject *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_DELETE_BUFFER_OBJECT)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_DeleteBufferObject(static_cast<const CCommandBuffer::SCommand_DeleteBufferObject *>(pBaseCommand)); }};

		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_CREATE_BUFFER_CONTAINER)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_CreateBufferContainer(static_cast<const CCommandBuffer::SCommand_CreateBufferContainer *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_DELETE_BUFFER_CONTAINER)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_DeleteBufferContainer(static_cast<const CCommandBuffer::SCommand_DeleteBufferContainer *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_UPDATE_BUFFER_CONTAINER)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_UpdateBufferContainer(static_cast<const CCommandBuffer::SCommand_UpdateBufferContainer *>(pBaseCommand)); }};

		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_INDICES_REQUIRED_NUM_NOTIFY)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_IndicesRequiredNumNotify(static_cast<const CCommandBuffer::SCommand_IndicesRequiredNumNotify *>(pBaseCommand)); }};

		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TILE_LAYER)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderTileLayer_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderTileLayer *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderTileLayer(static_cast<const CCommandBuffer::SCommand_RenderTileLayer *>(pBaseCommand), ExecBuffer); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_BORDER_TILE)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderBorderTile_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderBorderTile *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderBorderTile(static_cast<const CCommandBuffer::SCommand_RenderBorderTile *>(pBaseCommand), ExecBuffer); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_QUAD_LAYER)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderQuadLayer_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderQuadLayer *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderQuadLayer(static_cast<const CCommandBuffer::SCommand_RenderQuadLayer *>(pBaseCommand), ExecBuffer, false); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_QUAD_LAYER_GROUPED)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderQuadLayer_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderQuadLayer *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderQuadLayer(static_cast<const CCommandBuffer::SCommand_RenderQuadLayer *>(pBaseCommand), ExecBuffer, true); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_TEXT)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderText_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderText *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderText(static_cast<const CCommandBuffer::SCommand_RenderText *>(pBaseCommand), ExecBuffer); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_QUAD_CONTAINER)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderQuadContainer_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderQuadContainer *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderQuadContainer(static_cast<const CCommandBuffer::SCommand_RenderQuadContainer *>(pBaseCommand), ExecBuffer); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_QUAD_CONTAINER_EX)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderQuadContainerEx_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderQuadContainerEx *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderQuadContainerEx(static_cast<const CCommandBuffer::SCommand_RenderQuadContainerEx *>(pBaseCommand), ExecBuffer); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_RENDER_QUAD_CONTAINER_SPRITE_MULTIPLE)] = {true, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_RenderQuadContainerAsSpriteMultiple_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_RenderQuadContainerAsSpriteMultiple *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_RenderQuadContainerAsSpriteMultiple(static_cast<const CCommandBuffer::SCommand_RenderQuadContainerAsSpriteMultiple *>(pBaseCommand), ExecBuffer); }};

		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_SWAP)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_Swap(static_cast<const CCommandBuffer::SCommand_Swap *>(pBaseCommand)); }};

		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_VSYNC)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_VSync(static_cast<const CCommandBuffer::SCommand_VSync *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_MULTISAMPLING)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_MultiSampling(static_cast<const CCommandBuffer::SCommand_MultiSampling *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_TRY_SWAP_AND_READ_PIXEL)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_ReadPixel(static_cast<const CCommandBuffer::SCommand_TrySwapAndReadPixel *>(pBaseCommand)); }};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_TRY_SWAP_AND_SCREENSHOT)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_Screenshot(static_cast<const CCommandBuffer::SCommand_TrySwapAndScreenshot *>(pBaseCommand)); }};

		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_UPDATE_VIEWPORT)] = {false, [this](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) { Cmd_Update_Viewport_FillExecuteBuffer(ExecBuffer, static_cast<const CCommandBuffer::SCommand_Update_Viewport *>(pBaseCommand)); }, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_Update_Viewport(static_cast<const CCommandBuffer::SCommand_Update_Viewport *>(pBaseCommand)); }};

		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_WINDOW_CREATE_NTF)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_WindowCreateNtf(static_cast<const CCommandBuffer::SCommand_WindowCreateNtf *>(pBaseCommand)); }, false};
		m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::CMD_WINDOW_DESTROY_NTF)] = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [this](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return Cmd_WindowDestroyNtf(static_cast<const CCommandBuffer::SCommand_WindowDestroyNtf *>(pBaseCommand)); }, false};

		for(auto &Callback : m_aCommandCallbacks)
		{
			if(!(bool)Callback.m_CommandCB)
				Callback = {false, [](SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand *pBaseCommand) {}, [](const CCommandBuffer::SCommand *pBaseCommand, SRenderCommandExecuteBuffer &ExecBuffer) { return true; }};
		}
	}

	/*****************************
	 * VIDEO AND SCREENSHOT HELPER
	 ******************************/

	[[nodiscard]] bool PreparePresentedImageDataImage(uint8_t *&pResImageData, uint32_t Width, uint32_t Height)
	{
		bool NeedsNewImg = Width != m_GetPresentedImgDataHelperWidth || Height != m_GetPresentedImgDataHelperHeight;
		if(m_GetPresentedImgDataHelperImage == VK_NULL_HANDLE || NeedsNewImg)
		{
			if(m_GetPresentedImgDataHelperImage != VK_NULL_HANDLE)
			{
				DeletePresentedImageDataImage();
			}
			m_GetPresentedImgDataHelperWidth = Width;
			m_GetPresentedImgDataHelperHeight = Height;

			VkImageCreateInfo ImageInfo{};
			ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ImageInfo.imageType = VK_IMAGE_TYPE_2D;
			ImageInfo.extent.width = Width;
			ImageInfo.extent.height = Height;
			ImageInfo.extent.depth = 1;
			ImageInfo.mipLevels = 1;
			ImageInfo.arrayLayers = 1;
			ImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
			ImageInfo.tiling = VK_IMAGE_TILING_LINEAR;
			ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			vkCreateImage(m_VKDevice, &ImageInfo, nullptr, &m_GetPresentedImgDataHelperImage);
			// Create memory to back up the image
			VkMemoryRequirements MemRequirements;
			vkGetImageMemoryRequirements(m_VKDevice, m_GetPresentedImgDataHelperImage, &MemRequirements);

			VkMemoryAllocateInfo MemAllocInfo{};
			MemAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			MemAllocInfo.allocationSize = MemRequirements.size;
			MemAllocInfo.memoryTypeIndex = FindMemoryType(m_VKGPU, MemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

			vkAllocateMemory(m_VKDevice, &MemAllocInfo, nullptr, &m_GetPresentedImgDataHelperMem.m_Mem);
			vkBindImageMemory(m_VKDevice, m_GetPresentedImgDataHelperImage, m_GetPresentedImgDataHelperMem.m_Mem, 0);

			if(!ImageBarrier(m_GetPresentedImgDataHelperImage, 0, 1, 0, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL))
				return false;

			VkImageSubresource SubResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
			VkSubresourceLayout SubResourceLayout;
			vkGetImageSubresourceLayout(m_VKDevice, m_GetPresentedImgDataHelperImage, &SubResource, &SubResourceLayout);

			if(vkMapMemory(m_VKDevice, m_GetPresentedImgDataHelperMem.m_Mem, 0, VK_WHOLE_SIZE, 0, (void **)&m_pGetPresentedImgDataHelperMappedMemory) != VK_SUCCESS)
				return false;
			m_GetPresentedImgDataHelperMappedLayoutOffset = SubResourceLayout.offset;
			m_GetPresentedImgDataHelperMappedLayoutPitch = SubResourceLayout.rowPitch;
			m_pGetPresentedImgDataHelperMappedMemory += m_GetPresentedImgDataHelperMappedLayoutOffset;

			VkFenceCreateInfo FenceInfo{};
			FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			vkCreateFence(m_VKDevice, &FenceInfo, nullptr, &m_GetPresentedImgDataHelperFence);
		}
		pResImageData = m_pGetPresentedImgDataHelperMappedMemory;
		return true;
	}

	void DeletePresentedImageDataImage()
	{
		if(m_GetPresentedImgDataHelperImage != VK_NULL_HANDLE)
		{
			vkDestroyFence(m_VKDevice, m_GetPresentedImgDataHelperFence, nullptr);

			m_GetPresentedImgDataHelperFence = VK_NULL_HANDLE;

			vkDestroyImage(m_VKDevice, m_GetPresentedImgDataHelperImage, nullptr);
			vkUnmapMemory(m_VKDevice, m_GetPresentedImgDataHelperMem.m_Mem);
			vkFreeMemory(m_VKDevice, m_GetPresentedImgDataHelperMem.m_Mem, nullptr);

			m_GetPresentedImgDataHelperImage = VK_NULL_HANDLE;
			m_GetPresentedImgDataHelperMem = {};
			m_pGetPresentedImgDataHelperMappedMemory = nullptr;

			m_GetPresentedImgDataHelperWidth = 0;
			m_GetPresentedImgDataHelperHeight = 0;
		}
	}

	[[nodiscard]] bool GetPresentedImageDataImpl(uint32_t &Width, uint32_t &Height, CImageInfo::EImageFormat &Format, std::vector<uint8_t> &vDstData, bool ResetAlpha, std::optional<ivec2> PixelOffset)
	{
		bool IsB8G8R8A8 = m_VKSurfFormat.format == VK_FORMAT_B8G8R8A8_UNORM;
		bool UsesRGBALikeFormat = m_VKSurfFormat.format == VK_FORMAT_R8G8B8A8_UNORM || IsB8G8R8A8;
		if(UsesRGBALikeFormat && m_LastPresentedSwapChainImageIndex != std::numeric_limits<decltype(m_LastPresentedSwapChainImageIndex)>::max())
		{
			auto Viewport = m_VKSwapImgAndViewportExtent.GetPresentedImageViewport();
			VkOffset3D SrcOffset;
			if(PixelOffset.has_value())
			{
				SrcOffset.x = PixelOffset.value().x;
				SrcOffset.y = PixelOffset.value().y;
				Width = 1;
				Height = 1;
			}
			else
			{
				SrcOffset.x = 0;
				SrcOffset.y = 0;
				Width = Viewport.width;
				Height = Viewport.height;
			}
			SrcOffset.z = 0;
			Format = CImageInfo::FORMAT_RGBA;

			const size_t ImageTotalSize = (size_t)Width * Height * CImageInfo::PixelSize(Format);

			uint8_t *pResImageData;
			if(!PreparePresentedImageDataImage(pResImageData, Width, Height))
				return false;

			VkCommandBuffer *pCommandBuffer;
			if(!GetMemoryCommandBuffer(pCommandBuffer))
				return false;
			VkCommandBuffer &CommandBuffer = *pCommandBuffer;

			auto &SwapImg = m_vSwapChainImages[m_LastPresentedSwapChainImageIndex];

			if(!ImageBarrier(m_GetPresentedImgDataHelperImage, 0, 1, 0, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
				return false;
			if(!ImageBarrier(SwapImg, 0, 1, 0, 1, m_VKSurfFormat.format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL))
				return false;

			// If source and destination support blit we'll blit as this also does automatic format conversion (e.g. from BGR to RGB)
			if(m_OptimalSwapChainImageBlitting && m_LinearRGBAImageBlitting)
			{
				VkOffset3D BlitSize;
				BlitSize.x = Width;
				BlitSize.y = Height;
				BlitSize.z = 1;

				VkImageBlit ImageBlitRegion{};
				ImageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				ImageBlitRegion.srcSubresource.layerCount = 1;
				ImageBlitRegion.srcOffsets[0] = SrcOffset;
				ImageBlitRegion.srcOffsets[1] = {SrcOffset.x + BlitSize.x, SrcOffset.y + BlitSize.y, SrcOffset.z + BlitSize.z};
				ImageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				ImageBlitRegion.dstSubresource.layerCount = 1;
				ImageBlitRegion.dstOffsets[1] = BlitSize;

				// Issue the blit command
				vkCmdBlitImage(CommandBuffer, SwapImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					m_GetPresentedImgDataHelperImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &ImageBlitRegion, VK_FILTER_NEAREST);

				// transformed to RGBA
				IsB8G8R8A8 = false;
			}
			else
			{
				// Otherwise use image copy (requires us to manually flip components)
				VkImageCopy ImageCopyRegion{};
				ImageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				ImageCopyRegion.srcSubresource.layerCount = 1;
				ImageCopyRegion.srcOffset = SrcOffset;
				ImageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				ImageCopyRegion.dstSubresource.layerCount = 1;
				ImageCopyRegion.extent.width = Width;
				ImageCopyRegion.extent.height = Height;
				ImageCopyRegion.extent.depth = 1;

				// Issue the copy command
				vkCmdCopyImage(CommandBuffer, SwapImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					m_GetPresentedImgDataHelperImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1, &ImageCopyRegion);
			}

			if(!ImageBarrier(m_GetPresentedImgDataHelperImage, 0, 1, 0, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL))
				return false;
			if(!ImageBarrier(SwapImg, 0, 1, 0, 1, m_VKSurfFormat.format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR))
				return false;

			VkResult EndCommandBufferResult = vkEndCommandBuffer(CommandBuffer);
			if(EndCommandBufferResult != VK_SUCCESS)
			{
				const char *pErr = CheckVulkanCriticalError(EndCommandBufferResult);
				if(pErr != nullptr)
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Ending presented image data command buffer failed: %s.", pErr);
				else
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Ending presented image data command buffer failed.");
				return false;
			}
			m_vUsedMemoryCommandBuffer[m_CurImageIndex] = false;

			VkSubmitInfo SubmitInfo{};
			SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			SubmitInfo.commandBufferCount = 1;
			SubmitInfo.pCommandBuffers = &CommandBuffer;

			VkResult ResetFenceResult = vkResetFences(m_VKDevice, 1, &m_GetPresentedImgDataHelperFence);
			if(ResetFenceResult != VK_SUCCESS)
			{
				const char *pErr = CheckVulkanCriticalError(ResetFenceResult);
				if(pErr != nullptr)
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Resetting presented image data fence failed: %s.", pErr);
				else
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Resetting presented image data fence failed.");
				return false;
			}

			VkResult QueueSubmitResult = QueueSubmit(m_VKGraphicsQueue, 1, &SubmitInfo, m_GetPresentedImgDataHelperFence);
			if(QueueSubmitResult != VK_SUCCESS)
			{
				const char *pErr = CheckVulkanCriticalError(QueueSubmitResult);
				if(pErr != nullptr)
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting presented image data command buffer failed: %s.", pErr);
				else
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting presented image data command buffer failed.");
				return false;
			}
			VkResult WaitForFencesResult = WaitForFences(1, &m_GetPresentedImgDataHelperFence, VK_TRUE, std::numeric_limits<uint64_t>::max());
			if(WaitForFencesResult != VK_SUCCESS)
			{
				const char *pErr = CheckVulkanCriticalError(WaitForFencesResult);
				if(pErr != nullptr)
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Waiting for presented image data fence failed: %s.", pErr);
				else
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Waiting for presented image data fence failed.");
				return false;
			}

			VkMappedMemoryRange MemRange{};
			MemRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
			MemRange.memory = m_GetPresentedImgDataHelperMem.m_Mem;
			MemRange.offset = m_GetPresentedImgDataHelperMappedLayoutOffset;
			MemRange.size = VK_WHOLE_SIZE;
			VkResult InvalidateResult = InvalidateMappedMemoryRanges(1, &MemRange);
			if(InvalidateResult != VK_SUCCESS)
			{
				const char *pErr = CheckVulkanCriticalError(InvalidateResult);
				if(pErr != nullptr)
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Invalidating presented image data failed: %s.", pErr);
				else
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Invalidating presented image data failed.");
				return false;
			}

			size_t RealFullImageSize = maximum(ImageTotalSize, (size_t)(Height * m_GetPresentedImgDataHelperMappedLayoutPitch));
			m_FrameProfileStats.m_ReadbackRequests++;
			m_FrameProfileStats.m_ReadbackBytes += RealFullImageSize;
			size_t ExtraRowSize = Width * 4;
			if(vDstData.size() < RealFullImageSize + ExtraRowSize)
				vDstData.resize(RealFullImageSize + ExtraRowSize);

			mem_copy(vDstData.data(), pResImageData, RealFullImageSize);

			// pack image data together without any offset that the driver might require
			if(Width * 4 < m_GetPresentedImgDataHelperMappedLayoutPitch)
			{
				for(uint32_t Y = 0; Y < Height; ++Y)
				{
					size_t OffsetImagePacked = (Y * Width * 4);
					size_t OffsetImageUnpacked = (Y * m_GetPresentedImgDataHelperMappedLayoutPitch);
					mem_copy(vDstData.data() + RealFullImageSize, vDstData.data() + OffsetImageUnpacked, Width * 4);
					mem_copy(vDstData.data() + OffsetImagePacked, vDstData.data() + RealFullImageSize, Width * 4);
				}
			}

			if(IsB8G8R8A8 || ResetAlpha)
			{
				// swizzle
				for(uint32_t Y = 0; Y < Height; ++Y)
				{
					for(uint32_t X = 0; X < Width; ++X)
					{
						size_t ImgOff = (Y * Width * 4) + (X * 4);
						if(IsB8G8R8A8)
						{
							std::swap(vDstData[ImgOff], vDstData[ImgOff + 2]);
						}
						vDstData[ImgOff + 3] = 255;
					}
				}
			}

			return true;
		}
		else
		{
			if(!UsesRGBALikeFormat)
			{
				dbg_msg("vulkan", "swap chain image was not in a RGBA like format.");
			}
			else
			{
				dbg_msg("vulkan", "swap chain image was not ready to be copied.");
			}
			return false;
		}
	}

	[[nodiscard]] bool GetPresentedImageData(uint32_t &Width, uint32_t &Height, CImageInfo::EImageFormat &Format, std::vector<uint8_t> &vDstData) override
	{
		return GetPresentedImageDataImpl(Width, Height, Format, vDstData, false, {});
	}

	/************************
	 * MEMORY MANAGEMENT
	 ************************/

	[[nodiscard]] bool AllocateVulkanMemory(const VkMemoryAllocateInfo *pAllocateInfo, VkDeviceMemory *pMemory)
	{
		VkResult Res = vkAllocateMemory(m_VKDevice, pAllocateInfo, nullptr, pMemory);
		if(Res != VK_SUCCESS)
		{
			dbg_msg("vulkan", "vulkan memory allocation failed, trying to recover.");
			if(Res == VK_ERROR_OUT_OF_HOST_MEMORY || Res == VK_ERROR_OUT_OF_DEVICE_MEMORY)
			{
				// aggressively try to get more memory
				VkResult WaitIdleResult = DeviceWaitIdle();
				if(WaitIdleResult != VK_SUCCESS)
				{
					const char *pCritErrorMsg = CheckVulkanCriticalError(WaitIdleResult);
					if(pCritErrorMsg != nullptr)
						SetError(EGfxErrorType::GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER, "Waiting for device idle during memory recovery failed.", pCritErrorMsg);
					else
						SetError(EGfxErrorType::GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER, "Waiting for device idle during memory recovery failed.");
					return false;
				}
				for(size_t i = 0; i < m_SwapChainImageCount + 1; ++i)
				{
					if(!NextFrame())
						return false;
				}
				Res = vkAllocateMemory(m_VKDevice, pAllocateInfo, nullptr, pMemory);
			}
			if(Res != VK_SUCCESS)
			{
				dbg_msg("vulkan", "vulkan memory allocation failed.");
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] bool GetBufferImpl(VkDeviceSize RequiredSize, EMemoryBlockUsage MemUsage, VkBuffer &Buffer, SDeviceMemoryBlock &BufferMemory, VkBufferUsageFlags BufferUsage, VkMemoryPropertyFlags BufferProperties)
	{
		return CreateBuffer(RequiredSize, MemUsage, BufferUsage, BufferProperties, Buffer, BufferMemory);
	}

	template<size_t Id,
		int64_t MemoryBlockSize, size_t BlockCount,
		bool RequiresMapping>
	[[nodiscard]] bool GetBufferBlockImpl(SMemoryBlock<Id> &RetBlock, SMemoryBlockCache<Id> &MemoryCache, VkBufferUsageFlags BufferUsage, VkMemoryPropertyFlags BufferProperties, const void *pBufferData, VkDeviceSize RequiredSize, VkDeviceSize TargetAlignment)
	{
		bool Res = true;

		auto &&CreateCacheBlock = [&]() -> bool {
			bool FoundAllocation = false;
			SMemoryHeap::SMemoryHeapQueueElement AllocatedMem;
			SDeviceMemoryBlock TmpBufferMemory;
			typename SMemoryBlockCache<Id>::SMemoryCacheType::SMemoryCacheHeap *pCacheHeap = nullptr;
			auto &Heaps = MemoryCache.m_MemoryCaches.m_vpMemoryHeaps;
			for(size_t i = 0; i < Heaps.size(); ++i)
			{
				auto *pHeap = Heaps[i];
				if(pHeap->m_Heap.Allocate(RequiredSize, TargetAlignment, AllocatedMem))
				{
					TmpBufferMemory = pHeap->m_BufferMem;
					FoundAllocation = true;
					pCacheHeap = pHeap;
					break;
				}
			}
			if(!FoundAllocation)
			{
				typename SMemoryBlockCache<Id>::SMemoryCacheType::SMemoryCacheHeap *pNewHeap = new SMemoryBlockCache<Id>::SMemoryCacheType::SMemoryCacheHeap();

				VkBuffer TmpBuffer;
				if(!GetBufferImpl(MemoryBlockSize * BlockCount, RequiresMapping ? MEMORY_BLOCK_USAGE_STAGING : MEMORY_BLOCK_USAGE_BUFFER, TmpBuffer, TmpBufferMemory, BufferUsage, BufferProperties))
				{
					delete pNewHeap;
					return false;
				}
				void *pMapData = nullptr;

				if(RequiresMapping)
				{
					if(vkMapMemory(m_VKDevice, TmpBufferMemory.m_Mem, 0, VK_WHOLE_SIZE, 0, &pMapData) != VK_SUCCESS)
					{
						SetError(RequiresMapping ? EGfxErrorType::GFX_ERROR_TYPE_OUT_OF_MEMORY_STAGING : EGfxErrorType::GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER, "Failed to map buffer block memory.");
						CleanBufferPair(m_CurImageIndex, TmpBuffer, TmpBufferMemory);
						delete pNewHeap;
						return false;
					}
					m_FrameProfileStats.m_StagingBufferAllocations++;
					m_FrameProfileStats.m_StagingBufferAllocationBytes += TmpBufferMemory.m_Size;
				}

				pNewHeap->m_Buffer = TmpBuffer;

				pNewHeap->m_BufferMem = TmpBufferMemory;
				pNewHeap->m_pMappedBuffer = pMapData;

				pCacheHeap = pNewHeap;
				Heaps.emplace_back(pNewHeap);
				Heaps.back()->m_Heap.Init(MemoryBlockSize * BlockCount, 0);
				if(!Heaps.back()->m_Heap.Allocate(RequiredSize, TargetAlignment, AllocatedMem))
				{
					SetError(RequiresMapping ? EGfxErrorType::GFX_ERROR_TYPE_OUT_OF_MEMORY_STAGING : EGfxErrorType::GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER, "Heap allocation failed directly after creating fresh heap.");
					if(pNewHeap->m_pMappedBuffer != nullptr)
						vkUnmapMemory(m_VKDevice, pNewHeap->m_BufferMem.m_Mem);
					CleanBufferPair(m_CurImageIndex, pNewHeap->m_Buffer, pNewHeap->m_BufferMem);
					Heaps.pop_back();
					delete pNewHeap;
					return false;
				}
			}

			RetBlock.m_Buffer = pCacheHeap->m_Buffer;
			RetBlock.m_BufferMem = TmpBufferMemory;
			if(RequiresMapping)
				RetBlock.m_pMappedBuffer = ((uint8_t *)pCacheHeap->m_pMappedBuffer) + AllocatedMem.m_OffsetToAlign;
			else
				RetBlock.m_pMappedBuffer = nullptr;
			RetBlock.m_IsCached = true;
			RetBlock.m_pHeap = &pCacheHeap->m_Heap;
			RetBlock.m_HeapData = AllocatedMem;
			RetBlock.m_UsedSize = RequiredSize;

			if(RequiresMapping)
				mem_copy(RetBlock.m_pMappedBuffer, pBufferData, RequiredSize);

			return true;
		};

		if(RequiredSize < (VkDeviceSize)MemoryBlockSize)
		{
			Res = CreateCacheBlock();
		}
		else
		{
			VkBuffer TmpBuffer;
			SDeviceMemoryBlock TmpBufferMemory;
			if(!GetBufferImpl(RequiredSize, RequiresMapping ? MEMORY_BLOCK_USAGE_STAGING : MEMORY_BLOCK_USAGE_BUFFER, TmpBuffer, TmpBufferMemory, BufferUsage, BufferProperties))
				return false;
			void *pMapData = nullptr;
			if(RequiresMapping)
			{
				if(vkMapMemory(m_VKDevice, TmpBufferMemory.m_Mem, 0, VK_WHOLE_SIZE, 0, &pMapData) != VK_SUCCESS)
				{
					CleanBufferPair(m_CurImageIndex, TmpBuffer, TmpBufferMemory);
					return false;
				}
				m_FrameProfileStats.m_StagingBufferAllocations++;
				m_FrameProfileStats.m_StagingBufferAllocationBytes += TmpBufferMemory.m_Size;
				mem_copy(pMapData, pBufferData, static_cast<size_t>(RequiredSize));
			}

			RetBlock.m_Buffer = TmpBuffer;
			RetBlock.m_BufferMem = TmpBufferMemory;
			RetBlock.m_pMappedBuffer = pMapData;
			RetBlock.m_pHeap = nullptr;
			RetBlock.m_IsCached = false;
			RetBlock.m_HeapData.m_OffsetToAlign = 0;
			RetBlock.m_HeapData.m_AllocationSize = RequiredSize;
			RetBlock.m_UsedSize = RequiredSize;
		}

		return Res;
	}

	[[nodiscard]] bool GetStagingBuffer(SMemoryBlock<STAGING_BUFFER_CACHE_ID> &ResBlock, const void *pBufferData, VkDeviceSize RequiredSize)
	{
		return GetBufferBlockImpl<STAGING_BUFFER_CACHE_ID, 8 * 1024 * 1024, 3, true>(ResBlock, m_StagingBufferCache, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, pBufferData, RequiredSize, maximum<VkDeviceSize>(m_NonCoherentMemAlignment, 16));
	}

	[[nodiscard]] bool GetStagingBufferImage(SMemoryBlock<STAGING_BUFFER_IMAGE_CACHE_ID> &ResBlock, const void *pBufferData, VkDeviceSize RequiredSize)
	{
		return GetBufferBlockImpl<STAGING_BUFFER_IMAGE_CACHE_ID, 8 * 1024 * 1024, 3, true>(ResBlock, m_StagingBufferCacheImage, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, pBufferData, RequiredSize, maximum<VkDeviceSize>(m_OptimalImageCopyMemAlignment, maximum<VkDeviceSize>(m_NonCoherentMemAlignment, 16)));
	}

	template<size_t Id>
	void PrepareStagingMemRange(SMemoryBlock<Id> &Block)
	{
		VkMappedMemoryRange UploadRange{};
		UploadRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		UploadRange.memory = Block.m_BufferMem.m_Mem;
		UploadRange.offset = Block.m_HeapData.m_OffsetToAlign;

		auto AlignmentMod = ((VkDeviceSize)Block.m_HeapData.m_AllocationSize % m_NonCoherentMemAlignment);
		auto AlignmentReq = (m_NonCoherentMemAlignment - AlignmentMod);
		if(AlignmentMod == 0)
			AlignmentReq = 0;
		UploadRange.size = Block.m_HeapData.m_AllocationSize + AlignmentReq;

		if(UploadRange.offset + UploadRange.size > Block.m_BufferMem.m_Size)
			UploadRange.size = VK_WHOLE_SIZE;

		m_vNonFlushedStagingBufferRange.push_back(UploadRange);
		m_FrameProfileStats.m_StagingUploads++;
		m_FrameProfileStats.m_StagingUploadBytes += Block.m_UsedSize;
	}

	void UploadAndFreeStagingMemBlock(SMemoryBlock<STAGING_BUFFER_CACHE_ID> &Block)
	{
		PrepareStagingMemRange(Block);
		if(!Block.m_IsCached)
		{
			m_vvFrameDelayedBufferCleanup[m_CurImageIndex].push_back({Block.m_Buffer, Block.m_BufferMem, Block.m_pMappedBuffer});
		}
		else
		{
			m_StagingBufferCache.FreeMemBlock(Block, m_CurImageIndex);
		}
	}

	void UploadAndFreeStagingImageMemBlock(SMemoryBlock<STAGING_BUFFER_IMAGE_CACHE_ID> &Block)
	{
		PrepareStagingMemRange(Block);
		if(!Block.m_IsCached)
		{
			m_vvFrameDelayedBufferCleanup[m_CurImageIndex].push_back({Block.m_Buffer, Block.m_BufferMem, Block.m_pMappedBuffer});
		}
		else
		{
			m_StagingBufferCacheImage.FreeMemBlock(Block, m_CurImageIndex);
		}
	}

	[[nodiscard]] bool GetVertexBuffer(SMemoryBlock<VERTEX_BUFFER_CACHE_ID> &ResBlock, VkDeviceSize RequiredSize)
	{
		return GetBufferBlockImpl<VERTEX_BUFFER_CACHE_ID, 8 * 1024 * 1024, 3, false>(ResBlock, m_VertexBufferCache, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, nullptr, RequiredSize, 16);
	}

	void FreeVertexMemBlock(SMemoryBlock<VERTEX_BUFFER_CACHE_ID> &Block)
	{
		if(!Block.m_IsCached)
		{
			m_vvFrameDelayedBufferCleanup[m_CurImageIndex].push_back({Block.m_Buffer, Block.m_BufferMem, nullptr});
		}
		else
		{
			m_VertexBufferCache.FreeMemBlock(Block, m_CurImageIndex);
		}
	}

	static size_t ImageMipLevelCount(size_t Width, size_t Height, size_t Depth)
	{
		return std::floor(std::log2(maximum(Width, maximum(Height, Depth)))) + 1;
	}

	static size_t ImageMipLevelCount(const VkExtent3D &ImgExtent)
	{
		return ImageMipLevelCount(ImgExtent.width, ImgExtent.height, ImgExtent.depth);
	}

	// good approximation of 1024x1024 image with mipmaps
	static constexpr int64_t IMAGE_SIZE_1024X1024_APPROXIMATION = (1024 * 1024 * 4) * 2;

	[[nodiscard]] bool GetImageMemoryImpl(VkDeviceSize RequiredSize, uint32_t RequiredMemoryTypeBits, SDeviceMemoryBlock &BufferMemory, VkMemoryPropertyFlags BufferProperties)
	{
		VkMemoryAllocateInfo MemAllocInfo{};
		MemAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		MemAllocInfo.allocationSize = RequiredSize;
		MemAllocInfo.memoryTypeIndex = FindMemoryType(m_VKGPU, RequiredMemoryTypeBits, BufferProperties);

		BufferMemory.m_Size = RequiredSize;
		m_pTextureMemoryUsage->store(m_pTextureMemoryUsage->load(std::memory_order_relaxed) + RequiredSize, std::memory_order_relaxed);

		if(IsVerbose())
		{
			VerboseAllocatedMemory(RequiredSize, m_CurImageIndex, MEMORY_BLOCK_USAGE_TEXTURE);
		}

		if(!AllocateVulkanMemory(&MemAllocInfo, &BufferMemory.m_Mem))
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_OUT_OF_MEMORY_IMAGE, "Allocation for image memory failed.");
			return false;
		}

		BufferMemory.m_UsageType = MEMORY_BLOCK_USAGE_TEXTURE;

		return true;
	}

	template<size_t Id,
		int64_t MemoryBlockSize, size_t BlockCount>
	[[nodiscard]] bool GetImageMemoryBlockImpl(SMemoryImageBlock<Id> &RetBlock, SMemoryBlockCache<Id> &MemoryCache, VkMemoryPropertyFlags BufferProperties, VkDeviceSize RequiredSize, VkDeviceSize RequiredAlignment, uint32_t RequiredMemoryTypeBits)
	{
		auto &&CreateCacheBlock = [&]() -> bool {
			bool FoundAllocation = false;
			SMemoryHeap::SMemoryHeapQueueElement AllocatedMem;
			SDeviceMemoryBlock TmpBufferMemory;
			typename SMemoryBlockCache<Id>::SMemoryCacheType::SMemoryCacheHeap *pCacheHeap = nullptr;
			for(size_t i = 0; i < MemoryCache.m_MemoryCaches.m_vpMemoryHeaps.size(); ++i)
			{
				auto *pHeap = MemoryCache.m_MemoryCaches.m_vpMemoryHeaps[i];
				if(pHeap->m_Heap.Allocate(RequiredSize, RequiredAlignment, AllocatedMem))
				{
					TmpBufferMemory = pHeap->m_BufferMem;
					FoundAllocation = true;
					pCacheHeap = pHeap;
					break;
				}
			}
			if(!FoundAllocation)
			{
				typename SMemoryBlockCache<Id>::SMemoryCacheType::SMemoryCacheHeap *pNewHeap = new SMemoryBlockCache<Id>::SMemoryCacheType::SMemoryCacheHeap();

				if(!GetImageMemoryImpl(MemoryBlockSize * BlockCount, RequiredMemoryTypeBits, TmpBufferMemory, BufferProperties))
				{
					delete pNewHeap;
					return false;
				}

				pNewHeap->m_Buffer = VK_NULL_HANDLE;

				pNewHeap->m_BufferMem = TmpBufferMemory;
				pNewHeap->m_pMappedBuffer = nullptr;

				auto &Heaps = MemoryCache.m_MemoryCaches.m_vpMemoryHeaps;
				pCacheHeap = pNewHeap;
				Heaps.emplace_back(pNewHeap);
				Heaps.back()->m_Heap.Init(MemoryBlockSize * BlockCount, 0);
				if(!Heaps.back()->m_Heap.Allocate(RequiredSize, RequiredAlignment, AllocatedMem))
				{
					dbg_assert_failed("Heap allocation failed directly after creating fresh heap for image");
				}
			}

			RetBlock.m_Buffer = VK_NULL_HANDLE;
			RetBlock.m_BufferMem = TmpBufferMemory;
			RetBlock.m_pMappedBuffer = nullptr;
			RetBlock.m_IsCached = true;
			RetBlock.m_pHeap = &pCacheHeap->m_Heap;
			RetBlock.m_HeapData = AllocatedMem;
			RetBlock.m_UsedSize = RequiredSize;

			return true;
		};

		if(RequiredSize < (VkDeviceSize)MemoryBlockSize)
		{
			if(!CreateCacheBlock())
				return false;
		}
		else
		{
			SDeviceMemoryBlock TmpBufferMemory;
			if(!GetImageMemoryImpl(RequiredSize, RequiredMemoryTypeBits, TmpBufferMemory, BufferProperties))
				return false;

			RetBlock.m_Buffer = VK_NULL_HANDLE;
			RetBlock.m_BufferMem = TmpBufferMemory;
			RetBlock.m_pMappedBuffer = nullptr;
			RetBlock.m_IsCached = false;
			RetBlock.m_pHeap = nullptr;
			RetBlock.m_HeapData.m_OffsetToAlign = 0;
			RetBlock.m_HeapData.m_AllocationSize = RequiredSize;
			RetBlock.m_UsedSize = RequiredSize;
		}

		RetBlock.m_ImageMemoryBits = RequiredMemoryTypeBits;

		return true;
	}

	[[nodiscard]] bool GetImageMemory(SMemoryImageBlock<IMAGE_BUFFER_CACHE_ID> &RetBlock, VkDeviceSize RequiredSize, VkDeviceSize RequiredAlignment, uint32_t RequiredMemoryTypeBits)
	{
		auto BufferCacheIterator = m_ImageBufferCaches.find(RequiredMemoryTypeBits);
		if(BufferCacheIterator == m_ImageBufferCaches.end())
		{
			BufferCacheIterator = m_ImageBufferCaches.insert({RequiredMemoryTypeBits, {}}).first;

			BufferCacheIterator->second.Init(m_SwapChainImageCount);
		}
		return GetImageMemoryBlockImpl<IMAGE_BUFFER_CACHE_ID, IMAGE_SIZE_1024X1024_APPROXIMATION, 2>(RetBlock, BufferCacheIterator->second, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, RequiredSize, RequiredAlignment, RequiredMemoryTypeBits);
	}

	void FreeImageMemBlock(SMemoryImageBlock<IMAGE_BUFFER_CACHE_ID> &Block)
	{
		if(!Block.m_IsCached)
		{
			m_vvFrameDelayedBufferCleanup[m_CurImageIndex].push_back({Block.m_Buffer, Block.m_BufferMem, nullptr});
		}
		else
		{
			m_ImageBufferCaches[Block.m_ImageMemoryBits].FreeMemBlock(Block, m_CurImageIndex);
		}
	}

	template<bool FlushForRendering, typename TName>
	void UploadStreamedBuffer(SStreamMemory<TName> &StreamedBuffer)
	{
		size_t RangeUpdateCount = 0;
		if(StreamedBuffer.IsUsed(m_CurImageIndex))
		{
			for(size_t i = 0; i < StreamedBuffer.GetUsedCount(m_CurImageIndex); ++i)
			{
				auto &BufferOfFrame = StreamedBuffer.GetBuffers(m_CurImageIndex)[i];
				auto &MemRange = StreamedBuffer.GetRanges(m_CurImageIndex)[RangeUpdateCount++];
				MemRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
				MemRange.memory = BufferOfFrame.m_BufferMem.m_Mem;
				MemRange.offset = BufferOfFrame.m_OffsetInBuffer;
				auto AlignmentMod = ((VkDeviceSize)BufferOfFrame.m_UsedSize % m_NonCoherentMemAlignment);
				auto AlignmentReq = (m_NonCoherentMemAlignment - AlignmentMod);
				if(AlignmentMod == 0)
					AlignmentReq = 0;
				MemRange.size = BufferOfFrame.m_UsedSize + AlignmentReq;

				if(MemRange.offset + MemRange.size > BufferOfFrame.m_BufferMem.m_Size)
					MemRange.size = VK_WHOLE_SIZE;

				BufferOfFrame.m_UsedSize = 0;
			}
			if(RangeUpdateCount > 0 && FlushForRendering)
			{
				FlushMergedMappedMemoryRanges(RangeUpdateCount, StreamedBuffer.GetRanges(m_CurImageIndex).data());
			}
		}
		StreamedBuffer.ResetFrame(m_CurImageIndex);
	}

	void CleanBufferPair(size_t ImageIndex, VkBuffer &Buffer, SDeviceMemoryBlock &BufferMem)
	{
		bool IsBuffer = Buffer != VK_NULL_HANDLE;
		if(IsBuffer)
		{
			vkDestroyBuffer(m_VKDevice, Buffer, nullptr);

			Buffer = VK_NULL_HANDLE;
		}
		if(BufferMem.m_Mem != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_VKDevice, BufferMem.m_Mem, nullptr);
			if(BufferMem.m_UsageType == MEMORY_BLOCK_USAGE_BUFFER)
				m_pBufferMemoryUsage->store(m_pBufferMemoryUsage->load(std::memory_order_relaxed) - BufferMem.m_Size, std::memory_order_relaxed);
			else if(BufferMem.m_UsageType == MEMORY_BLOCK_USAGE_TEXTURE)
				m_pTextureMemoryUsage->store(m_pTextureMemoryUsage->load(std::memory_order_relaxed) - BufferMem.m_Size, std::memory_order_relaxed);
			else if(BufferMem.m_UsageType == MEMORY_BLOCK_USAGE_STREAM)
				m_pStreamMemoryUsage->store(m_pStreamMemoryUsage->load(std::memory_order_relaxed) - BufferMem.m_Size, std::memory_order_relaxed);
			else if(BufferMem.m_UsageType == MEMORY_BLOCK_USAGE_STAGING)
				m_pStagingMemoryUsage->store(m_pStagingMemoryUsage->load(std::memory_order_relaxed) - BufferMem.m_Size, std::memory_order_relaxed);

			if(IsVerbose())
			{
				VerboseDeallocatedMemory(BufferMem.m_Size, ImageIndex, BufferMem.m_UsageType);
			}

			BufferMem.m_Mem = VK_NULL_HANDLE;
		}
	}

	void DestroyTexture(CTexture &Texture)
	{
		if(Texture.m_Img != VK_NULL_HANDLE)
		{
			FreeImageMemBlock(Texture.m_ImgMem);
			vkDestroyImage(m_VKDevice, Texture.m_Img, nullptr);

			vkDestroyImageView(m_VKDevice, Texture.m_ImgView, nullptr);
		}

		if(Texture.m_Img3D != VK_NULL_HANDLE)
		{
			FreeImageMemBlock(Texture.m_Img3DMem);
			vkDestroyImage(m_VKDevice, Texture.m_Img3D, nullptr);

			vkDestroyImageView(m_VKDevice, Texture.m_Img3DView, nullptr);
		}

		DestroyTexturedStandardDescriptorSets(Texture, 0);
		DestroyTexturedStandardDescriptorSets(Texture, 1);

		DestroyTextured3DStandardDescriptorSets(Texture);
	}

	void DestroyTextTexture(CTexture &Texture, CTexture &TextureOutline)
	{
		if(Texture.m_Img != VK_NULL_HANDLE)
		{
			FreeImageMemBlock(Texture.m_ImgMem);
			vkDestroyImage(m_VKDevice, Texture.m_Img, nullptr);

			vkDestroyImageView(m_VKDevice, Texture.m_ImgView, nullptr);
		}

		if(TextureOutline.m_Img != VK_NULL_HANDLE)
		{
			FreeImageMemBlock(TextureOutline.m_ImgMem);
			vkDestroyImage(m_VKDevice, TextureOutline.m_Img, nullptr);

			vkDestroyImageView(m_VKDevice, TextureOutline.m_ImgView, nullptr);
		}

		DestroyTextDescriptorSets(Texture, TextureOutline);
	}

	void ClearFrameData(size_t FrameImageIndex)
	{
		UploadStagingBuffers();

		// clear pending buffers, that require deletion
		for(auto &BufferPair : m_vvFrameDelayedBufferCleanup[FrameImageIndex])
		{
			if(BufferPair.m_pMappedData != nullptr)
			{
				vkUnmapMemory(m_VKDevice, BufferPair.m_Mem.m_Mem);
			}
			CleanBufferPair(FrameImageIndex, BufferPair.m_Buffer, BufferPair.m_Mem);
		}
		m_vvFrameDelayedBufferCleanup[FrameImageIndex].clear();

		// clear pending textures, that require deletion
		for(auto &Texture : m_vvFrameDelayedTextureCleanup[FrameImageIndex])
		{
			DestroyTexture(Texture);
		}
		m_vvFrameDelayedTextureCleanup[FrameImageIndex].clear();

		for(auto &TexturePair : m_vvFrameDelayedTextTexturesCleanup[FrameImageIndex])
		{
			DestroyTextTexture(TexturePair.first, TexturePair.second);
		}
		m_vvFrameDelayedTextTexturesCleanup[FrameImageIndex].clear();

		m_StagingBufferCache.Cleanup(FrameImageIndex);
		m_StagingBufferCacheImage.Cleanup(FrameImageIndex);
		m_VertexBufferCache.Cleanup(FrameImageIndex);
		for(auto &ImageBufferCache : m_ImageBufferCaches)
			ImageBufferCache.second.Cleanup(FrameImageIndex);
	}

	void ShrinkUnusedCaches()
	{
		size_t FreedMemory = 0;
		FreedMemory += m_StagingBufferCache.Shrink(m_VKDevice);
		FreedMemory += m_StagingBufferCacheImage.Shrink(m_VKDevice);
		if(FreedMemory > 0)
		{
			m_pStagingMemoryUsage->store(m_pStagingMemoryUsage->load(std::memory_order_relaxed) - FreedMemory, std::memory_order_relaxed);
			if(IsVerbose())
			{
				dbg_msg("vulkan", "deallocated chunks of memory with size: %" PRIzu " from all frames (staging buffer)", FreedMemory);
			}
		}
		FreedMemory = 0;
		FreedMemory += m_VertexBufferCache.Shrink(m_VKDevice);
		if(FreedMemory > 0)
		{
			m_pBufferMemoryUsage->store(m_pBufferMemoryUsage->load(std::memory_order_relaxed) - FreedMemory, std::memory_order_relaxed);
			if(IsVerbose())
			{
				dbg_msg("vulkan", "deallocated chunks of memory with size: %" PRIzu " from all frames (buffer)", FreedMemory);
			}
		}
		FreedMemory = 0;
		for(auto &ImageBufferCache : m_ImageBufferCaches)
			FreedMemory += ImageBufferCache.second.Shrink(m_VKDevice);
		if(FreedMemory > 0)
		{
			m_pTextureMemoryUsage->store(m_pTextureMemoryUsage->load(std::memory_order_relaxed) - FreedMemory, std::memory_order_relaxed);
			if(IsVerbose())
			{
				dbg_msg("vulkan", "deallocated chunks of memory with size: %" PRIzu " from all frames (texture)", FreedMemory);
			}
		}
	}

	[[nodiscard]] bool MemoryBarrier(VkBuffer Buffer, VkDeviceSize Offset, VkDeviceSize Size, VkAccessFlags BufferAccessType, bool BeforeCommand)
	{
		VkCommandBuffer *pMemCommandBuffer;
		if(!GetMemoryCommandBuffer(pMemCommandBuffer))
			return false;
		auto &MemCommandBuffer = *pMemCommandBuffer;

		VkBufferMemoryBarrier Barrier{};
		Barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.buffer = Buffer;
		Barrier.offset = Offset;
		Barrier.size = Size;

		VkPipelineStageFlags SourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkPipelineStageFlags DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

		if(BeforeCommand)
		{
			Barrier.srcAccessMask = BufferAccessType;
			Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			SourceStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
			DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else
		{
			Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			Barrier.dstAccessMask = BufferAccessType;

			SourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			DestinationStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
		}

		RecordBarrier(0, 1, 0);
		vkCmdPipelineBarrier(
			MemCommandBuffer,
			SourceStage, DestinationStage,
			0,
			0, nullptr,
			1, &Barrier,
			0, nullptr);

		return true;
	}

	/************************
	 * SWAPPING MECHANISM
	 ************************/

	void StartRenderThread(size_t ThreadIndex)
	{
		auto &List = m_vvThreadCommandLists[ThreadIndex];
		if(!List.empty())
		{
			m_vThreadHelperHadCommands[ThreadIndex] = true;
			auto *pThread = m_vpRenderThreads[ThreadIndex].get();
			std::unique_lock<std::mutex> Lock(pThread->m_Mutex);
			pThread->m_IsRendering = true;
			pThread->m_Cond.notify_one();
		}
	}

	void FinishRenderThreads()
	{
		if(m_ThreadCount > 1)
		{
			// execute threads

			for(size_t ThreadIndex = 0; ThreadIndex < m_ThreadCount - 1; ++ThreadIndex)
			{
				if(!m_vThreadHelperHadCommands[ThreadIndex])
				{
					StartRenderThread(ThreadIndex);
				}
			}

			for(size_t ThreadIndex = 0; ThreadIndex < m_ThreadCount - 1; ++ThreadIndex)
			{
				if(m_vThreadHelperHadCommands[ThreadIndex])
				{
					auto &pRenderThread = m_vpRenderThreads[ThreadIndex];
					m_vThreadHelperHadCommands[ThreadIndex] = false;
					std::unique_lock<std::mutex> Lock(pRenderThread->m_Mutex);
					pRenderThread->m_Cond.wait(Lock, [&pRenderThread] { return !pRenderThread->m_IsRendering; });
					ResetDrawCommandState(ThreadIndex + 1);
				}
			}
		}
	}

	[[nodiscard]] bool ExecuteMemoryCommandBuffer()
	{
		if(m_vUsedMemoryCommandBuffer[m_CurImageIndex])
		{
			auto &MemoryCommandBuffer = m_vMemoryCommandBuffers[m_CurImageIndex];
			VkResult EndCommandBufferResult = vkEndCommandBuffer(MemoryCommandBuffer);
			if(EndCommandBufferResult != VK_SUCCESS)
			{
				const char *pCritErrorMsg = CheckVulkanCriticalError(EndCommandBufferResult);
				if(pCritErrorMsg != nullptr)
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Ending memory command buffer failed.", pCritErrorMsg);
				else
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Ending memory command buffer failed.");
				return false;
			}

			VkSubmitInfo SubmitInfo{};
			SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

			SubmitInfo.commandBufferCount = 1;
			SubmitInfo.pCommandBuffers = &MemoryCommandBuffer;

			VkFence MemoryFence = m_vMemoryCommandBufferFences[m_CurImageIndex];
			VkResult ResetFencesResult = vkResetFences(m_VKDevice, 1, &MemoryFence);
			if(ResetFencesResult != VK_SUCCESS)
			{
				const char *pCritErrorMsg = CheckVulkanCriticalError(ResetFencesResult);
				if(pCritErrorMsg != nullptr)
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Resetting memory command buffer fence failed.", pCritErrorMsg);
				else
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Resetting memory command buffer fence failed.");
				return false;
			}
			VkResult QueueSubmitRes = QueueSubmit(m_VKGraphicsQueue, 1, &SubmitInfo, MemoryFence);
			if(QueueSubmitRes != VK_SUCCESS)
			{
				const char *pCritErrorMsg = CheckVulkanCriticalError(QueueSubmitRes);
				if(pCritErrorMsg != nullptr)
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting memory command buffer failed.", pCritErrorMsg);
				else
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting memory command buffer failed.");
				return false;
			}

			VkResult WaitRes = WaitForFences(1, &MemoryFence, VK_TRUE, std::numeric_limits<uint64_t>::max());
			if(WaitRes != VK_SUCCESS)
			{
				const char *pCritErrorMsg = CheckVulkanCriticalError(WaitRes);
				if(pCritErrorMsg != nullptr)
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Waiting for memory command buffer failed.", pCritErrorMsg);
				else
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Waiting for memory command buffer failed.");
				return false;
			}

			m_vUsedMemoryCommandBuffer[m_CurImageIndex] = false;
		}
		return true;
	}

	void ExecutePendingRenderThreadCommandBuffers(VkCommandBuffer CommandBuffer)
	{
		if(m_ThreadCount <= 1)
			return;
		size_t ThreadedCommandsUsedCount = 0;
		const size_t RenderThreadCount = m_ThreadCount - 1;
		for(size_t i = 0; i < RenderThreadCount; ++i)
		{
			if(m_vvUsedThreadDrawCommandBuffer[i + 1][m_CurImageIndex])
			{
				m_vHelperThreadDrawCommandBuffers[ThreadedCommandsUsedCount++] = m_vvThreadDrawCommandBuffers[i + 1][m_CurImageIndex];
				m_vvUsedThreadDrawCommandBuffer[i + 1][m_CurImageIndex] = false;
			}
		}
		if(ThreadedCommandsUsedCount > 0)
			vkCmdExecuteCommands(CommandBuffer, ThreadedCommandsUsedCount, m_vHelperThreadDrawCommandBuffers.data());
		if(m_vvUsedThreadDrawCommandBuffer[0][m_CurImageIndex])
		{
			auto &GraphicThreadCommandBuffer = m_vvThreadDrawCommandBuffers[0][m_CurImageIndex];
			vkEndCommandBuffer(GraphicThreadCommandBuffer);
			vkCmdExecuteCommands(CommandBuffer, 1, &GraphicThreadCommandBuffer);
			m_vvUsedThreadDrawCommandBuffer[0][m_CurImageIndex] = false;
		}
	}

	void BeginSwapRenderPass(VkRenderPass RenderPass)
	{
		auto &CommandBuffer = GetMainGraphicCommandBuffer();
		VkRenderPassBeginInfo RenderPassInfo{};
		RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		RenderPassInfo.renderPass = RenderPass;
		RenderPassInfo.framebuffer = m_vFramebufferList[m_CurImageIndex];
		RenderPassInfo.renderArea.offset = {0, 0};
		RenderPassInfo.renderArea.extent = m_VKSwapImgAndViewportExtent.m_SwapImageViewport;
		VkClearValue ClearColorVal = {{{m_aClearColor[0], m_aClearColor[1], m_aClearColor[2], m_aClearColor[3]}}};
		RenderPassInfo.clearValueCount = 1;
		RenderPassInfo.pClearValues = &ClearColorVal;
		const VkSubpassContents SubpassContents = (m_ThreadCount > 1 && !m_ForceSingleThreadedRender) ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE;
		vkCmdBeginRenderPass(CommandBuffer, &RenderPassInfo, SubpassContents);
		m_SwapRenderPassActive = true;
		for(auto &LastPipe : m_vLastPipeline)
			LastPipe = VK_NULL_HANDLE;
	}

	void EndSwapRenderPassForExternalWork()
	{
		FinishRenderThreads();
		auto &CommandBuffer = GetMainGraphicCommandBuffer();
		ExecutePendingRenderThreadCommandBuffers(CommandBuffer);
		if(m_SwapRenderPassActive)
		{
			vkCmdEndRenderPass(CommandBuffer);
			m_SwapRenderPassActive = false;
		}
		m_ForceSingleThreadedRender = true;
		for(auto &LastPipe : m_vLastPipeline)
			LastPipe = VK_NULL_HANDLE;
	}

	[[nodiscard]] bool SubmitCurrentCommandsAndRestartSwapPass()
	{
		EndSwapRenderPassForExternalWork();
		UploadNonFlushedBuffers<true>();
		auto &CommandBuffer = GetMainGraphicCommandBuffer();
		if(vkEndCommandBuffer(CommandBuffer) != VK_SUCCESS)
			return false;

		VkSubmitInfo SubmitInfo{};
		SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		SubmitInfo.commandBufferCount = 1;
		SubmitInfo.pCommandBuffers = &CommandBuffer;
		std::array<VkCommandBuffer, 2> aCommandBuffers = {};
		if(m_vUsedMemoryCommandBuffer[m_CurImageIndex])
		{
			auto &MemoryCommandBuffer = m_vMemoryCommandBuffers[m_CurImageIndex];
			vkEndCommandBuffer(MemoryCommandBuffer);
			aCommandBuffers[0] = MemoryCommandBuffer;
			aCommandBuffers[1] = CommandBuffer;
			SubmitInfo.commandBufferCount = 2;
			SubmitInfo.pCommandBuffers = aCommandBuffers.data();
			m_vUsedMemoryCommandBuffer[m_CurImageIndex] = false;
		}
		std::array<VkSemaphore, 1> aWaitSemaphores = {m_AcquireImageSemaphore};
		std::array<VkPipelineStageFlags, 1> aWaitStages = {(VkPipelineStageFlags)VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
		if(!m_AcquireSemaphoreConsumed)
		{
			SubmitInfo.waitSemaphoreCount = aWaitSemaphores.size();
			SubmitInfo.pWaitSemaphores = aWaitSemaphores.data();
			SubmitInfo.pWaitDstStageMask = aWaitStages.data();
			m_AcquireSemaphoreConsumed = true;
		}
		VkFence SubmitFence = m_vQueueSubmitFences[m_CurImageIndex];
		VkResult ResetSubmitFenceResult = vkResetFences(m_VKDevice, 1, &SubmitFence);
		if(ResetSubmitFenceResult != VK_SUCCESS)
		{
			const char *pCritErrorMsg = CheckVulkanCriticalError(ResetSubmitFenceResult);
			if(pCritErrorMsg != nullptr)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Resetting intermediate graphics queue submit fence failed.", pCritErrorMsg);
			else
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Resetting intermediate graphics queue submit fence failed.");
			return false;
		}
		VkResult QueueSubmitRes = QueueSubmit(m_VKGraphicsQueue, 1, &SubmitInfo, SubmitFence);
		if(QueueSubmitRes != VK_SUCCESS)
		{
			const char *pCritErrorMsg = CheckVulkanCriticalError(QueueSubmitRes);
			if(pCritErrorMsg != nullptr)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting intermediate graphics queue command buffer failed.", pCritErrorMsg);
			else
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting intermediate graphics queue command buffer failed.");
			return false;
		}
		VkResult WaitSubmitFenceResult = WaitForFences(1, &SubmitFence, VK_TRUE, std::numeric_limits<uint64_t>::max());
		if(WaitSubmitFenceResult != VK_SUCCESS)
		{
			const char *pCritErrorMsg = CheckVulkanCriticalError(WaitSubmitFenceResult);
			if(pCritErrorMsg != nullptr)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Waiting for intermediate graphics queue submit fence failed.", pCritErrorMsg);
			else
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Waiting for intermediate graphics queue submit fence failed.");
			return false;
		}

		vkResetCommandBuffer(CommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
		VkCommandBufferBeginInfo BeginInfo{};
		BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if(vkBeginCommandBuffer(CommandBuffer, &BeginInfo) != VK_SUCCESS)
			return false;
		BeginSwapRenderPass(m_VKRenderPassLoad);
		return true;
	}

	void ClearFrameMemoryUsage()
	{
		ClearFrameData(m_CurImageIndex);
		ShrinkUnusedCaches();
	}

	[[nodiscard]] bool WaitFrame()
	{
		FinishRenderThreads();
		m_LastCommandsInPipeThreadIndex = 0;

		UploadNonFlushedBuffers<true>();

		auto &CommandBuffer = GetMainGraphicCommandBuffer();

		// render threads
		if(m_ThreadCount > 1)
		{
			size_t ThreadedCommandsUsedCount = 0;
			size_t RenderThreadCount = m_ThreadCount - 1;
			for(size_t i = 0; i < RenderThreadCount; ++i)
			{
				if(m_vvUsedThreadDrawCommandBuffer[i + 1][m_CurImageIndex])
				{
					const auto &GraphicThreadCommandBuffer = m_vvThreadDrawCommandBuffers[i + 1][m_CurImageIndex];
					m_vHelperThreadDrawCommandBuffers[ThreadedCommandsUsedCount++] = GraphicThreadCommandBuffer;

					m_vvUsedThreadDrawCommandBuffer[i + 1][m_CurImageIndex] = false;
				}
			}
			if(ThreadedCommandsUsedCount > 0)
			{
				vkCmdExecuteCommands(CommandBuffer, ThreadedCommandsUsedCount, m_vHelperThreadDrawCommandBuffers.data());
			}

			// special case if swap chain was not completed in one runbuffer call

			if(m_vvUsedThreadDrawCommandBuffer[0][m_CurImageIndex])
			{
				auto &GraphicThreadCommandBuffer = m_vvThreadDrawCommandBuffers[0][m_CurImageIndex];
				VkResult EndThreadCommandBufferResult = vkEndCommandBuffer(GraphicThreadCommandBuffer);
				if(EndThreadCommandBufferResult != VK_SUCCESS)
				{
					const char *pCritErrorMsg = CheckVulkanCriticalError(EndThreadCommandBufferResult);
					if(pCritErrorMsg != nullptr)
						SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_RECORDING, "Thread command buffer cannot be ended anymore.", pCritErrorMsg);
					else
						SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_RECORDING, "Thread command buffer cannot be ended anymore.");
					return false;
				}

				vkCmdExecuteCommands(CommandBuffer, 1, &GraphicThreadCommandBuffer);

				m_vvUsedThreadDrawCommandBuffer[0][m_CurImageIndex] = false;
			}
		}

		vkCmdEndRenderPass(CommandBuffer);
		EndFrameTimestampQuery(CommandBuffer);

		if(vkEndCommandBuffer(CommandBuffer) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_RECORDING, "Command buffer cannot be ended anymore.");
			return false;
		}

		VkSubmitInfo SubmitInfo{};
		SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		SubmitInfo.commandBufferCount = 1;
		SubmitInfo.pCommandBuffers = &CommandBuffer;

		std::array<VkCommandBuffer, 2> aCommandBuffers = {};

		if(m_vUsedMemoryCommandBuffer[m_CurImageIndex])
		{
			auto &MemoryCommandBuffer = m_vMemoryCommandBuffers[m_CurImageIndex];
			VkResult EndMemoryCommandBufferResult = vkEndCommandBuffer(MemoryCommandBuffer);
			if(EndMemoryCommandBufferResult != VK_SUCCESS)
			{
				const char *pCritErrorMsg = CheckVulkanCriticalError(EndMemoryCommandBufferResult);
				if(pCritErrorMsg != nullptr)
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_RECORDING, "Memory command buffer cannot be ended anymore.", pCritErrorMsg);
				else
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_RECORDING, "Memory command buffer cannot be ended anymore.");
				return false;
			}

			aCommandBuffers[0] = MemoryCommandBuffer;
			aCommandBuffers[1] = CommandBuffer;
			SubmitInfo.commandBufferCount = 2;
			SubmitInfo.pCommandBuffers = aCommandBuffers.data();

			m_vUsedMemoryCommandBuffer[m_CurImageIndex] = false;
		}

		std::array<VkSemaphore, 1> aWaitSemaphores = {m_AcquireImageSemaphore};
		std::array<VkPipelineStageFlags, 1> aWaitStages = {(VkPipelineStageFlags)VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
		if(!m_AcquireSemaphoreConsumed)
		{
			SubmitInfo.waitSemaphoreCount = aWaitSemaphores.size();
			SubmitInfo.pWaitSemaphores = aWaitSemaphores.data();
			SubmitInfo.pWaitDstStageMask = aWaitStages.data();
			m_AcquireSemaphoreConsumed = true;
		}

		std::array<VkSemaphore, 1> aSignalSemaphores = {m_vQueueSubmitSemaphores[m_CurImageIndex]};
		SubmitInfo.signalSemaphoreCount = aSignalSemaphores.size();
		SubmitInfo.pSignalSemaphores = aSignalSemaphores.data();

		VkResult ResetSubmitFenceResult = vkResetFences(m_VKDevice, 1, &m_vQueueSubmitFences[m_CurImageIndex]);
		if(ResetSubmitFenceResult != VK_SUCCESS)
		{
			const char *pCritErrorMsg = CheckVulkanCriticalError(ResetSubmitFenceResult);
			if(pCritErrorMsg != nullptr)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Resetting graphics queue submit fence failed.", pCritErrorMsg);
			else
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Resetting graphics queue submit fence failed.");
			return false;
		}

		VkResult QueueSubmitRes = QueueSubmit(m_VKGraphicsQueue, 1, &SubmitInfo, m_vQueueSubmitFences[m_CurImageIndex]);
		if(QueueSubmitRes != VK_SUCCESS)
		{
			dbg_msg("vulkan", "frame submit failed: result=%d image=%u/%u command_buffers=%u wait_semaphores=%u signal_semaphores=%u render_commands=%" PRIu64 " render_calls=%" PRIu64 " command_count=%" PRIu64,
				(int)QueueSubmitRes, m_CurImageIndex, m_SwapChainImageCount, SubmitInfo.commandBufferCount, SubmitInfo.waitSemaphoreCount, SubmitInfo.signalSemaphoreCount,
				m_FrameProfileStats.m_RenderCommands, m_FrameProfileStats.m_EstimatedRenderCallCount, m_FrameProfileStats.m_CommandCount);
			const char *pCritErrorMsg = CheckVulkanCriticalError(QueueSubmitRes);
			if(pCritErrorMsg != nullptr)
			{
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting to graphics queue failed.", pCritErrorMsg);
				return false;
			}
		}
		MarkFrameTimestampQueryPending(m_CurImageIndex);

		std::swap(m_vBusyAcquireImageSemaphores[m_CurImageIndex], m_AcquireImageSemaphore);

		VkPresentInfoKHR PresentInfo{};
		PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

		PresentInfo.waitSemaphoreCount = aSignalSemaphores.size();
		PresentInfo.pWaitSemaphores = aSignalSemaphores.data();

		std::array<VkSwapchainKHR, 1> aSwapChains = {m_VKSwapChain};
		PresentInfo.swapchainCount = aSwapChains.size();
		PresentInfo.pSwapchains = aSwapChains.data();

		PresentInfo.pImageIndices = &m_CurImageIndex;

		m_LastPresentedSwapChainImageIndex = m_CurImageIndex;

		VkResult QueuePresentRes = m_pfnQueuePresentKHR(m_VKPresentQueue, &PresentInfo);
		if(QueuePresentRes != VK_SUCCESS && QueuePresentRes != VK_SUBOPTIMAL_KHR)
		{
			const char *pCritErrorMsg = CheckVulkanCriticalError(QueuePresentRes);
			if(pCritErrorMsg != nullptr)
			{
				SetError(EGfxErrorType::GFX_ERROR_TYPE_SWAP_FAILED, "Presenting graphics queue failed.", pCritErrorMsg);
				return false;
			}
		}

		if(m_FrameProfilingActive)
		{
			m_FrameProfileStats.m_CPUFrameTime = time_get_nanoseconds() - m_FrameProfileStartTime;
			LogFrameProfileStats();
		}

		return true;
	}

	[[nodiscard]] bool PrepareFrame()
	{
		m_ForceSingleThreadedRender = false;
		m_FrameProfilingActive = FrameProfilingEnabled();
		ResetFrameProfileData();

		if(m_RecreateSwapChain)
		{
			m_RecreateSwapChain = false;
			if(IsVerbose())
			{
				dbg_msg("vulkan", "recreating swap chain requested by user (prepare frame).");
			}
			if(RecreateSwapChain() != 0)
				return false;
		}

		auto AcqResult = m_pfnAcquireNextImageKHR(m_VKDevice, m_VKSwapChain, std::numeric_limits<uint64_t>::max(), m_AcquireImageSemaphore, VK_NULL_HANDLE, &m_CurImageIndex);
		if(AcqResult != VK_SUCCESS)
		{
			if(AcqResult == VK_ERROR_OUT_OF_DATE_KHR || m_RecreateSwapChain)
			{
				m_RecreateSwapChain = false;
				if(IsVerbose())
				{
					dbg_msg("vulkan", "recreating swap chain requested by acquire next image (prepare frame).");
				}
				if(RecreateSwapChain() != 0)
					return false;
				return PrepareFrame();
			}
			else
			{
				if(AcqResult != VK_SUBOPTIMAL_KHR)
					dbg_msg("vulkan", "acquire next image failed %d", (int)AcqResult);

				const char *pCritErrorMsg = CheckVulkanCriticalError(AcqResult);
				if(pCritErrorMsg != nullptr)
				{
					SetError(EGfxErrorType::GFX_ERROR_TYPE_SWAP_FAILED, "Acquiring next image failed.", pCritErrorMsg);
					return false;
				}
				else if(AcqResult == VK_ERROR_SURFACE_LOST_KHR)
				{
					m_RenderingPaused = true;
					return true;
				}
			}
		}
		m_AcquireSemaphoreConsumed = false;

		VkResult WaitForImageFenceResult = WaitForFences(1, &m_vQueueSubmitFences[m_CurImageIndex], VK_TRUE, std::numeric_limits<uint64_t>::max());
		if(WaitForImageFenceResult != VK_SUCCESS)
		{
			const char *pCritErrorMsg = CheckVulkanCriticalError(WaitForImageFenceResult);
			if(pCritErrorMsg != nullptr)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Waiting for swap chain image fence failed.", pCritErrorMsg);
			else
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Waiting for swap chain image fence failed.");
			return false;
		}
		ReadFrameTimestampQuery(m_CurImageIndex);

		// next frame
		m_CurFrame++;
		m_vImageLastFrameCheck[m_CurImageIndex] = m_CurFrame;

		// check if older frames weren't used in a long time
		for(size_t FrameImageIndex = 0; FrameImageIndex < m_vImageLastFrameCheck.size(); ++FrameImageIndex)
		{
			auto LastFrame = m_vImageLastFrameCheck[FrameImageIndex];
			if(m_CurFrame - LastFrame > (uint64_t)m_SwapChainImageCount)
			{
				VkResult WaitForUnusedFrameFenceResult = WaitForFences(1, &m_vQueueSubmitFences[FrameImageIndex], VK_TRUE, std::numeric_limits<uint64_t>::max());
				if(WaitForUnusedFrameFenceResult != VK_SUCCESS)
				{
					const char *pCritErrorMsg = CheckVulkanCriticalError(WaitForUnusedFrameFenceResult);
					if(pCritErrorMsg != nullptr)
						SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Waiting for unused frame fence failed.", pCritErrorMsg);
					else
						SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Waiting for unused frame fence failed.");
					return false;
				}
				ReadFrameTimestampQuery(FrameImageIndex);
				ClearFrameData(FrameImageIndex);
				m_vImageLastFrameCheck[FrameImageIndex] = m_CurFrame;
			}
		}

		// clear frame's memory data
		ClearFrameMemoryUsage();

		// clear frame
		vkResetCommandBuffer(GetMainGraphicCommandBuffer(), VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

		auto &CommandBuffer = GetMainGraphicCommandBuffer();
		VkCommandBufferBeginInfo BeginInfo{};
		BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		if(vkBeginCommandBuffer(CommandBuffer, &BeginInfo) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_RECORDING, "Command buffer cannot be filled anymore.");
			return false;
		}
		BeginFrameTimestampQuery(CommandBuffer);

		VkRenderPassBeginInfo RenderPassInfo{};
		RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		RenderPassInfo.renderPass = m_VKRenderPass;
		RenderPassInfo.framebuffer = m_vFramebufferList[m_CurImageIndex];
		RenderPassInfo.renderArea.offset = {0, 0};
		RenderPassInfo.renderArea.extent = m_VKSwapImgAndViewportExtent.m_SwapImageViewport;

		VkClearValue ClearColorVal = {{{m_aClearColor[0], m_aClearColor[1], m_aClearColor[2], m_aClearColor[3]}}};
		RenderPassInfo.clearValueCount = 1;
		RenderPassInfo.pClearValues = &ClearColorVal;

		vkCmdBeginRenderPass(CommandBuffer, &RenderPassInfo, m_ThreadCount > 1 ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE);
		m_SwapRenderPassActive = true;

		for(size_t RenderThreadIndex = 0; RenderThreadIndex < m_vDrawCommandStates.size(); ++RenderThreadIndex)
			ResetDrawCommandState(RenderThreadIndex);

		return true;
	}

	void UploadStagingBuffers()
	{
		if(!m_vNonFlushedStagingBufferRange.empty())
		{
			FlushMergedMappedMemoryRanges(m_vNonFlushedStagingBufferRange.size(), m_vNonFlushedStagingBufferRange.data());

			m_vNonFlushedStagingBufferRange.clear();
		}
	}

	template<bool FlushForRendering>
	void UploadNonFlushedBuffers()
	{
		// streamed vertices
		for(auto &StreamVertexBuffer : m_vStreamedVertexBuffers)
			UploadStreamedBuffer<FlushForRendering>(StreamVertexBuffer);
		// now the buffer objects
		for(auto &StreamUniformBuffer : m_vStreamedUniformBuffers)
			UploadStreamedBuffer<FlushForRendering>(StreamUniformBuffer);

		UploadStagingBuffers();
	}

	[[nodiscard]] bool PureMemoryFrame()
	{
		if(!ExecuteMemoryCommandBuffer())
			return false;

		// reset streamed data
		UploadNonFlushedBuffers<false>();

		ClearFrameMemoryUsage();

		return true;
	}

	[[nodiscard]] bool NextFrame()
	{
		if(!m_RenderingPaused)
		{
			if(!WaitFrame())
				return false;
			if(!PrepareFrame())
				return false;
		}
		// else only execute the memory command buffer
		else
		{
			if(!PureMemoryFrame())
				return false;
		}

		return true;
	}

	/************************
	 * TEXTURES
	 ************************/

	size_t VulkanFormatToPixelSize(VkFormat Format)
	{
		if(Format == VK_FORMAT_R8G8B8_UNORM)
			return 3;
		else if(Format == VK_FORMAT_R8G8B8A8_UNORM)
			return 4;
		else if(Format == VK_FORMAT_R8_UNORM)
			return 1;
		return 4;
	}

	[[nodiscard]] bool TextureDataSize(size_t Width, size_t Height, size_t Depth, size_t PixelSize, size_t &DataSize) const
	{
		if(Width == 0 || Height == 0 || Depth == 0 || PixelSize == 0)
		{
			DataSize = 0;
			return false;
		}
		if(Width > std::numeric_limits<size_t>::max() / Height)
		{
			DataSize = 0;
			return false;
		}
		const size_t PlaneSize = Width * Height;
		if(PlaneSize > std::numeric_limits<size_t>::max() / Depth)
		{
			DataSize = 0;
			return false;
		}
		const size_t PixelCount = PlaneSize * Depth;
		if(PixelCount > std::numeric_limits<size_t>::max() / PixelSize)
		{
			DataSize = 0;
			return false;
		}
		DataSize = PixelCount * PixelSize;
		return true;
	}

	[[nodiscard]] bool UpdateTexture(size_t TextureSlot, VkFormat Format, uint8_t *&pData, int64_t XOff, int64_t YOff, size_t Width, size_t Height)
	{
		size_t ImageSize = 0;
		if(!TextureDataSize(Width, Height, 1, VulkanFormatToPixelSize(Format), ImageSize))
			return false;
		SMemoryBlock<STAGING_BUFFER_IMAGE_CACHE_ID> StagingBuffer;
		bool StagingBufferInitialized = false;

		auto &Tex = m_vTextures[TextureSlot];

		if(Tex.m_RescaleCount > 0)
		{
			const size_t OldWidth = Width;
			const size_t OldHeight = Height;
			for(uint32_t i = 0; i < Tex.m_RescaleCount; ++i)
			{
				Width >>= 1;
				Height >>= 1;

				XOff /= 2;
				YOff /= 2;
			}

			uint8_t *pTmpData = ResizeImage(pData, OldWidth, OldHeight, Width, Height, VulkanFormatToPixelSize(Format));
			free(pData);
			pData = pTmpData;
			if(pData == nullptr)
				return false;
		}

		if(!TextureDataSize(Width, Height, 1, VulkanFormatToPixelSize(Format), ImageSize) ||
			!GetStagingBufferImage(StagingBuffer, pData, ImageSize))
			return false;
		StagingBufferInitialized = true;

		bool Success = ImageBarrier(Tex.m_Img, 0, Tex.m_MipMapCount, 0, 1, Format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) &&
			       CopyBufferToImage(StagingBuffer.m_Buffer, StagingBuffer.m_HeapData.m_OffsetToAlign, Tex.m_Img, XOff, YOff, Width, Height, 1);

		if(Success && Tex.m_MipMapCount > 1)
		{
			Success = BuildMipmaps(Tex.m_Img, Format, Width, Height, 1, Tex.m_MipMapCount);
		}
		else if(Success)
		{
			Success = ImageBarrier(Tex.m_Img, 0, 1, 0, 1, Format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		if(StagingBufferInitialized)
			UploadAndFreeStagingImageMemBlock(StagingBuffer);
		return Success;
	}

	[[nodiscard]] bool CreateTextureCMD(
		int Slot,
		int Width,
		int Height,
		VkFormat Format,
		VkFormat StoreFormat,
		int Flags,
		uint8_t *&pData)
	{
		size_t ImageIndex = (size_t)Slot;
		const size_t PixelSize = VulkanFormatToPixelSize(Format);

		while(ImageIndex >= m_vTextures.size())
		{
			m_vTextures.resize((m_vTextures.size() * 2) + 1);
		}

		// resample if needed
		uint32_t RescaleCount = 0;
		if((size_t)Width > m_MaxTextureSize || (size_t)Height > m_MaxTextureSize)
		{
			const size_t OldWidth = Width;
			const size_t OldHeight = Height;
			do
			{
				Width >>= 1;
				Height >>= 1;
				++RescaleCount;
			} while((size_t)Width > m_MaxTextureSize || (size_t)Height > m_MaxTextureSize);

			uint8_t *pTmpData = ResizeImage(pData, OldWidth, OldHeight, Width, Height, PixelSize);
			free(pData);
			pData = pTmpData;
			if(pData == nullptr)
				return false;
		}

		bool Requires2DTexture = (Flags & TextureFlag::NO_2D_TEXTURE) == 0;
		bool Requires2DTextureArray = (Flags & TextureFlag::TO_2D_ARRAY_TEXTURE) != 0;
		bool RequiresMipMaps = (Flags & TextureFlag::NO_MIPMAPS) == 0;
		size_t MipMapLevelCount = 1;
		if(RequiresMipMaps)
		{
			VkExtent3D ImgSize{(uint32_t)Width, (uint32_t)Height, 1};
			MipMapLevelCount = ImageMipLevelCount(ImgSize);
			if(!m_OptimalRGBAImageBlitting)
				MipMapLevelCount = 1;
		}

		CTexture &Texture = m_vTextures[ImageIndex];

		Texture.m_Width = Width;
		Texture.m_Height = Height;
		Texture.m_RescaleCount = RescaleCount;
		Texture.m_MipMapCount = MipMapLevelCount;

		if(Requires2DTexture)
		{
			if(!CreateTextureImage(ImageIndex, Texture.m_Img, Texture.m_ImgMem, pData, Format, Width, Height, 1, PixelSize, MipMapLevelCount))
				return false;
			VkFormat ImgFormat = Format;
			VkImageView ImgView = CreateTextureImageView(Texture.m_Img, ImgFormat, VK_IMAGE_VIEW_TYPE_2D, 1, MipMapLevelCount);
			Texture.m_ImgView = ImgView;
			VkSampler ImgSampler = GetTextureSampler(SUPPORTED_SAMPLER_TYPE_REPEAT);
			Texture.m_aSamplers[0] = ImgSampler;
			ImgSampler = GetTextureSampler(SUPPORTED_SAMPLER_TYPE_CLAMP_TO_EDGE);
			Texture.m_aSamplers[1] = ImgSampler;

			if(!CreateNewTexturedStandardDescriptorSets(ImageIndex, 0))
				return false;
			if(!CreateNewTexturedStandardDescriptorSets(ImageIndex, 1))
				return false;
		}

		if(Requires2DTextureArray)
		{
			int ConvertWidth = Width;
			int ConvertHeight = Height;

			if(ConvertWidth == 0 || (ConvertWidth % 16) != 0 || ConvertHeight == 0 || (ConvertHeight % 16) != 0)
			{
				dbg_msg("vulkan", "3D/2D array texture was resized");
				int NewWidth = maximum<int>(HighestBit(ConvertWidth), 16);
				int NewHeight = maximum<int>(HighestBit(ConvertHeight), 16);
				uint8_t *pNewTexData = ResizeImage(pData, ConvertWidth, ConvertHeight, NewWidth, NewHeight, PixelSize);

				ConvertWidth = NewWidth;
				ConvertHeight = NewHeight;

				free(pData);
				pData = pNewTexData;
				if(pData == nullptr)
					return false;
			}

			int Image3DWidth, Image3DHeight;
			uint8_t *pTexData3D = static_cast<uint8_t *>(malloc((size_t)PixelSize * ConvertWidth * ConvertHeight));
			if(pTexData3D == nullptr)
				return false;
			Texture2DTo3D(pData, ConvertWidth, ConvertHeight, PixelSize, 16, 16, pTexData3D, Image3DWidth, Image3DHeight);

			const size_t ImageDepth2DArray = (size_t)16 * 16;
			VkExtent3D ImgSize{(uint32_t)Image3DWidth, (uint32_t)Image3DHeight, 1};
			if(RequiresMipMaps)
			{
				MipMapLevelCount = ImageMipLevelCount(ImgSize);
				if(!m_OptimalRGBAImageBlitting)
					MipMapLevelCount = 1;
			}

			if(!CreateTextureImage(ImageIndex, Texture.m_Img3D, Texture.m_Img3DMem, pTexData3D, Format, Image3DWidth, Image3DHeight, ImageDepth2DArray, PixelSize, MipMapLevelCount))
			{
				free(pTexData3D);
				return false;
			}
			VkFormat ImgFormat = Format;
			VkImageView ImgView = CreateTextureImageView(Texture.m_Img3D, ImgFormat, VK_IMAGE_VIEW_TYPE_2D_ARRAY, ImageDepth2DArray, MipMapLevelCount);
			Texture.m_Img3DView = ImgView;
			VkSampler ImgSampler = GetTextureSampler(SUPPORTED_SAMPLER_TYPE_2D_TEXTURE_ARRAY);
			Texture.m_Sampler3D = ImgSampler;

			if(!CreateNew3DTexturedStandardDescriptorSets(ImageIndex))
			{
				free(pTexData3D);
				return false;
			}

			free(pTexData3D);
		}
		return true;
	}

	[[nodiscard]] bool BuildMipmaps(VkImage Image, VkFormat ImageFormat, size_t Width, size_t Height, size_t Depth, size_t MipMapLevelCount)
	{
		VkCommandBuffer *pMemCommandBuffer;
		if(!GetMemoryCommandBuffer(pMemCommandBuffer))
			return false;
		auto &MemCommandBuffer = *pMemCommandBuffer;

		VkImageMemoryBarrier Barrier{};
		Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		Barrier.image = Image;
		Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Barrier.subresourceRange.levelCount = 1;
		Barrier.subresourceRange.baseArrayLayer = 0;
		Barrier.subresourceRange.layerCount = Depth;

		int32_t TmpMipWidth = (int32_t)Width;
		int32_t TmpMipHeight = (int32_t)Height;

		for(size_t i = 1; i < MipMapLevelCount; ++i)
		{
			Barrier.subresourceRange.baseMipLevel = i - 1;
			Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			Barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			Barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			RecordBarrier(0, 0, 1);
			vkCmdPipelineBarrier(MemCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &Barrier);

			VkImageBlit Blit{};
			Blit.srcOffsets[0] = {0, 0, 0};
			Blit.srcOffsets[1] = {TmpMipWidth, TmpMipHeight, 1};
			Blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			Blit.srcSubresource.mipLevel = i - 1;
			Blit.srcSubresource.baseArrayLayer = 0;
			Blit.srcSubresource.layerCount = Depth;
			Blit.dstOffsets[0] = {0, 0, 0};
			Blit.dstOffsets[1] = {TmpMipWidth > 1 ? TmpMipWidth / 2 : 1, TmpMipHeight > 1 ? TmpMipHeight / 2 : 1, 1};
			Blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			Blit.dstSubresource.mipLevel = i;
			Blit.dstSubresource.baseArrayLayer = 0;
			Blit.dstSubresource.layerCount = Depth;

			vkCmdBlitImage(MemCommandBuffer,
				Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &Blit,
				m_AllowsLinearBlitting ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);

			Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			Barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			RecordBarrier(0, 0, 1);
			vkCmdPipelineBarrier(MemCommandBuffer,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
				0, nullptr,
				0, nullptr,
				1, &Barrier);

			if(TmpMipWidth > 1)
				TmpMipWidth /= 2;
			if(TmpMipHeight > 1)
				TmpMipHeight /= 2;
		}

		Barrier.subresourceRange.baseMipLevel = MipMapLevelCount - 1;
		Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		RecordBarrier(0, 0, 1);
		vkCmdPipelineBarrier(MemCommandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr,
			0, nullptr,
			1, &Barrier);

		return true;
	}

	[[nodiscard]] bool CreateTextureImage(size_t ImageIndex, VkImage &NewImage, SMemoryImageBlock<IMAGE_BUFFER_CACHE_ID> &NewImgMem, const uint8_t *pData, VkFormat Format, size_t Width, size_t Height, size_t Depth, size_t PixelSize, size_t MipMapLevelCount)
	{
		VkDeviceSize ImageSize = Width * Height * Depth * PixelSize;

		SMemoryBlock<STAGING_BUFFER_IMAGE_CACHE_ID> StagingBuffer;
		if(!GetStagingBufferImage(StagingBuffer, pData, ImageSize))
			return false;

		VkFormat ImgFormat = Format;

		if(!CreateImage(Width, Height, Depth, MipMapLevelCount, ImgFormat, VK_IMAGE_TILING_OPTIMAL, NewImage, NewImgMem))
			return false;

		if(!ImageBarrier(NewImage, 0, MipMapLevelCount, 0, Depth, ImgFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
			return false;
		if(!CopyBufferToImage(StagingBuffer.m_Buffer, StagingBuffer.m_HeapData.m_OffsetToAlign, NewImage, 0, 0, static_cast<uint32_t>(Width), static_cast<uint32_t>(Height), Depth))
			return false;

		UploadAndFreeStagingImageMemBlock(StagingBuffer);

		if(MipMapLevelCount > 1)
		{
			if(!BuildMipmaps(NewImage, ImgFormat, Width, Height, Depth, MipMapLevelCount))
				return false;
		}
		else
		{
			if(!ImageBarrier(NewImage, 0, 1, 0, Depth, ImgFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
				return false;
		}

		return true;
	}

	VkImageView CreateTextureImageView(VkImage TexImage, VkFormat ImgFormat, VkImageViewType ViewType, size_t Depth, size_t MipMapLevelCount)
	{
		return CreateImageView(TexImage, ImgFormat, ViewType, Depth, MipMapLevelCount);
	}

	[[nodiscard]] bool CreateTextureSamplersImpl(VkSampler &CreatedSampler, VkSamplerAddressMode AddrModeU, VkSamplerAddressMode AddrModeV, VkSamplerAddressMode AddrModeW)
	{
		VkSamplerCreateInfo SamplerInfo{};
		SamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		SamplerInfo.magFilter = VK_FILTER_LINEAR;
		SamplerInfo.minFilter = VK_FILTER_LINEAR;
		SamplerInfo.addressModeU = AddrModeU;
		SamplerInfo.addressModeV = AddrModeV;
		SamplerInfo.addressModeW = AddrModeW;
		SamplerInfo.anisotropyEnable = VK_FALSE;
		SamplerInfo.maxAnisotropy = m_MaxSamplerAnisotropy;
		SamplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		SamplerInfo.unnormalizedCoordinates = VK_FALSE;
		SamplerInfo.compareEnable = VK_FALSE;
		SamplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		SamplerInfo.mipLodBias = (m_GlobalTextureLodBIAS / 1000.0f);
		SamplerInfo.minLod = -1000;
		SamplerInfo.maxLod = 1000;

		if(vkCreateSampler(m_VKDevice, &SamplerInfo, nullptr, &CreatedSampler) != VK_SUCCESS)
		{
			dbg_msg("vulkan", "failed to create texture sampler!");
			return false;
		}
		return true;
	}

	[[nodiscard]] bool CreateTextureSamplers()
	{
		bool Ret = true;
		Ret &= CreateTextureSamplersImpl(m_aSamplers[SUPPORTED_SAMPLER_TYPE_REPEAT], VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT, VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT, VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT);
		Ret &= CreateTextureSamplersImpl(m_aSamplers[SUPPORTED_SAMPLER_TYPE_CLAMP_TO_EDGE], VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
		Ret &= CreateTextureSamplersImpl(m_aSamplers[SUPPORTED_SAMPLER_TYPE_2D_TEXTURE_ARRAY], VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT);
		return Ret;
	}

	void DestroyTextureSamplers()
	{
		vkDestroySampler(m_VKDevice, m_aSamplers[SUPPORTED_SAMPLER_TYPE_REPEAT], nullptr);
		vkDestroySampler(m_VKDevice, m_aSamplers[SUPPORTED_SAMPLER_TYPE_CLAMP_TO_EDGE], nullptr);
		vkDestroySampler(m_VKDevice, m_aSamplers[SUPPORTED_SAMPLER_TYPE_2D_TEXTURE_ARRAY], nullptr);
	}

	VkSampler GetTextureSampler(ESupportedSamplerTypes SamplerType)
	{
		return m_aSamplers[SamplerType];
	}

	VkImageView CreateImageView(VkImage Image, VkFormat Format, VkImageViewType ViewType, size_t Depth, size_t MipMapLevelCount)
	{
		VkImageViewCreateInfo ViewCreateInfo{};
		ViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		ViewCreateInfo.image = Image;
		ViewCreateInfo.viewType = ViewType;
		ViewCreateInfo.format = Format;
		ViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ViewCreateInfo.subresourceRange.baseMipLevel = 0;
		ViewCreateInfo.subresourceRange.levelCount = MipMapLevelCount;
		ViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		ViewCreateInfo.subresourceRange.layerCount = Depth;

		VkImageView ImageView;
		if(vkCreateImageView(m_VKDevice, &ViewCreateInfo, nullptr, &ImageView) != VK_SUCCESS)
		{
			return VK_NULL_HANDLE;
		}

		return ImageView;
	}

	[[nodiscard]] bool CreateImage(uint32_t Width, uint32_t Height, uint32_t Depth, size_t MipMapLevelCount, VkFormat Format, VkImageTiling Tiling, VkImage &Image, SMemoryImageBlock<IMAGE_BUFFER_CACHE_ID> &ImageMemory, VkImageUsageFlags ImageUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VkSampleCountFlagBits SampleCount = VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM)
	{
		VkImageCreateInfo ImageInfo{};
		ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ImageInfo.imageType = VK_IMAGE_TYPE_2D;
		ImageInfo.extent.width = Width;
		ImageInfo.extent.height = Height;
		ImageInfo.extent.depth = 1;
		ImageInfo.mipLevels = MipMapLevelCount;
		ImageInfo.arrayLayers = Depth;
		ImageInfo.format = Format;
		ImageInfo.tiling = Tiling;
		ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		ImageInfo.usage = ImageUsage;
		ImageInfo.samples = SampleCount == VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM ? ((ImageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0 ? VK_SAMPLE_COUNT_1_BIT : GetSampleCount()) : SampleCount;
		ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if(vkCreateImage(m_VKDevice, &ImageInfo, nullptr, &Image) != VK_SUCCESS)
		{
			dbg_msg("vulkan", "failed to create image!");
			Image = VK_NULL_HANDLE;
			return false;
		}

		VkMemoryRequirements MemRequirements;
		vkGetImageMemoryRequirements(m_VKDevice, Image, &MemRequirements);

		if(!GetImageMemory(ImageMemory, MemRequirements.size, MemRequirements.alignment, MemRequirements.memoryTypeBits))
		{
			vkDestroyImage(m_VKDevice, Image, nullptr);
			Image = VK_NULL_HANDLE;
			return false;
		}

		if(vkBindImageMemory(m_VKDevice, Image, ImageMemory.m_BufferMem.m_Mem, ImageMemory.m_HeapData.m_OffsetToAlign) != VK_SUCCESS)
		{
			FreeImageMemBlock(ImageMemory);
			vkDestroyImage(m_VKDevice, Image, nullptr);
			Image = VK_NULL_HANDLE;
			ImageMemory = {};
			return false;
		}

		return true;
	}

	[[nodiscard]] bool ImageBarrier(VkCommandBuffer CommandBuffer, const VkImage &Image, size_t MipMapBase, size_t MipMapCount, size_t LayerBase, size_t LayerCount, VkFormat Format, VkImageLayout OldLayout, VkImageLayout NewLayout)
	{
		VkImageMemoryBarrier Barrier{};
		Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		Barrier.oldLayout = OldLayout;
		Barrier.newLayout = NewLayout;
		Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.image = Image;
		Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Barrier.subresourceRange.baseMipLevel = MipMapBase;
		Barrier.subresourceRange.levelCount = MipMapCount;
		Barrier.subresourceRange.baseArrayLayer = LayerBase;
		Barrier.subresourceRange.layerCount = LayerCount;

		VkPipelineStageFlags SourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkPipelineStageFlags DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

		if(OldLayout == VK_IMAGE_LAYOUT_UNDEFINED && NewLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			Barrier.srcAccessMask = 0;
			Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			SourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			SourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			DestinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			Barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			SourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
		{
			Barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			Barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

			SourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			DestinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && NewLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			Barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			Barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			SourceStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_UNDEFINED && NewLayout == VK_IMAGE_LAYOUT_GENERAL)
		{
			Barrier.srcAccessMask = 0;
			Barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

			SourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_GENERAL && NewLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			Barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			SourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_GENERAL)
		{
			Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			Barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

			SourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_UNDEFINED && NewLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			Barrier.srcAccessMask = 0;
			Barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			SourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			DestinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			Barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			Barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			SourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			DestinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			SourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			DestinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			Barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			SourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			Barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			Barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			SourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			DestinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if(OldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && NewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			Barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			SourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			DestinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else
		{
			dbg_msg("vulkan", "unsupported layout transition!");
		}

		RecordBarrier(0, 0, 1);
		vkCmdPipelineBarrier(
			CommandBuffer,
			SourceStage, DestinationStage,
			0,
			0, nullptr,
			0, nullptr,
			1, &Barrier);

		return true;
	}

	[[nodiscard]] bool ImageBarrier(const VkImage &Image, size_t MipMapBase, size_t MipMapCount, size_t LayerBase, size_t LayerCount, VkFormat Format, VkImageLayout OldLayout, VkImageLayout NewLayout)
	{
		VkCommandBuffer *pMemCommandBuffer;
		if(!GetMemoryCommandBuffer(pMemCommandBuffer))
			return false;
		return ImageBarrier(*pMemCommandBuffer, Image, MipMapBase, MipMapCount, LayerBase, LayerCount, Format, OldLayout, NewLayout);
	}

	[[nodiscard]] bool CopyBufferToImage(VkBuffer Buffer, VkDeviceSize BufferOffset, VkImage Image, int32_t X, int32_t Y, uint32_t Width, uint32_t Height, size_t Depth)
	{
		VkCommandBuffer *pCommandBuffer;
		if(!GetMemoryCommandBuffer(pCommandBuffer))
			return false;
		auto &CommandBuffer = *pCommandBuffer;

		VkBufferImageCopy Region{};
		Region.bufferOffset = BufferOffset;
		Region.bufferRowLength = 0;
		Region.bufferImageHeight = 0;
		Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Region.imageSubresource.mipLevel = 0;
		Region.imageSubresource.baseArrayLayer = 0;
		Region.imageSubresource.layerCount = Depth;
		Region.imageOffset = {X, Y, 0};
		Region.imageExtent = {
			Width,
			Height,
			1};

		vkCmdCopyBufferToImage(CommandBuffer, Buffer, Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);

		return true;
	}

	/************************
	 * BUFFERS
	 ************************/

	[[nodiscard]] bool CreateBufferObject(size_t BufferIndex, const void *pUploadData, VkDeviceSize BufferDataSize, bool IsOneFrameBuffer)
	{
		std::vector<uint8_t> UploadDataTmp;
		if(pUploadData == nullptr)
		{
			UploadDataTmp.resize(BufferDataSize);
			pUploadData = UploadDataTmp.data();
		}

		while(BufferIndex >= m_vBufferObjects.size())
		{
			m_vBufferObjects.resize((m_vBufferObjects.size() * 2) + 1);
		}
		auto &BufferObject = m_vBufferObjects[BufferIndex];

		VkBuffer VertexBuffer;
		size_t BufferOffset = 0;
		if(!IsOneFrameBuffer)
		{
			SMemoryBlock<STAGING_BUFFER_CACHE_ID> StagingBuffer;
			if(!GetStagingBuffer(StagingBuffer, pUploadData, BufferDataSize))
				return false;

			SMemoryBlock<VERTEX_BUFFER_CACHE_ID> Mem;
			if(!GetVertexBuffer(Mem, BufferDataSize))
				return false;

			BufferObject.m_BufferObject.m_Mem = Mem;
			VertexBuffer = Mem.m_Buffer;
			BufferOffset = Mem.m_HeapData.m_OffsetToAlign;

			if(!MemoryBarrier(VertexBuffer, Mem.m_HeapData.m_OffsetToAlign, BufferDataSize, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, true))
				return false;
			if(!CopyBuffer(StagingBuffer.m_Buffer, VertexBuffer, StagingBuffer.m_HeapData.m_OffsetToAlign, Mem.m_HeapData.m_OffsetToAlign, BufferDataSize))
				return false;
			if(!MemoryBarrier(VertexBuffer, Mem.m_HeapData.m_OffsetToAlign, BufferDataSize, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, false))
				return false;
			UploadAndFreeStagingMemBlock(StagingBuffer);
		}
		else
		{
			SDeviceMemoryBlock VertexBufferMemory;
			if(!CreateStreamVertexBuffer(MAIN_THREAD_INDEX, VertexBuffer, VertexBufferMemory, BufferOffset, pUploadData, BufferDataSize))
				return false;
		}
		BufferObject.m_IsStreamedBuffer = IsOneFrameBuffer;
		BufferObject.m_CurBuffer = VertexBuffer;
		BufferObject.m_CurBufferOffset = BufferOffset;

		return true;
	}

	void DeleteBufferObject(size_t BufferIndex)
	{
		auto &BufferObject = m_vBufferObjects[BufferIndex];
		if(!BufferObject.m_IsStreamedBuffer)
		{
			FreeVertexMemBlock(BufferObject.m_BufferObject.m_Mem);
		}
		BufferObject = {};
	}

	[[nodiscard]] bool CopyBuffer(VkBuffer SrcBuffer, VkBuffer DstBuffer, VkDeviceSize SrcOffset, VkDeviceSize DstOffset, VkDeviceSize CopySize)
	{
		VkCommandBuffer *pCommandBuffer;
		if(!GetMemoryCommandBuffer(pCommandBuffer))
			return false;
		auto &CommandBuffer = *pCommandBuffer;
		VkBufferCopy CopyRegion{};
		CopyRegion.srcOffset = SrcOffset;
		CopyRegion.dstOffset = DstOffset;
		CopyRegion.size = CopySize;
		vkCmdCopyBuffer(CommandBuffer, SrcBuffer, DstBuffer, 1, &CopyRegion);

		return true;
	}

	/************************
	 * RENDER STATES
	 ************************/

	void GetStateMatrix(const CCommandBuffer::SState &State, std::array<float, (size_t)4 * 2> &Matrix)
	{
		Matrix = {
			// column 1
			2.f / (State.m_ScreenBR.x - State.m_ScreenTL.x),
			0,
			// column 2
			0,
			2.f / (State.m_ScreenBR.y - State.m_ScreenTL.y),
			// column 3
			0,
			0,
			// column 4
			-((State.m_ScreenTL.x + State.m_ScreenBR.x) / (State.m_ScreenBR.x - State.m_ScreenTL.x)),
			-((State.m_ScreenTL.y + State.m_ScreenBR.y) / (State.m_ScreenBR.y - State.m_ScreenTL.y)),
		};
	}

	[[nodiscard]] bool GetIsTextured(const CCommandBuffer::SState &State)
	{
		return State.m_Texture != -1;
	}

	size_t GetAddressModeIndex(const CCommandBuffer::SState &State)
	{
		switch(State.m_WrapMode)
		{
		case EWrapMode::REPEAT:
			return VULKAN_BACKEND_ADDRESS_MODE_REPEAT;
		case EWrapMode::CLAMP:
			return VULKAN_BACKEND_ADDRESS_MODE_CLAMP_EDGES;
		default:
			dbg_assert_failed("Invalid wrap mode: %d", (int)State.m_WrapMode);
		};
	}

	size_t GetBlendModeIndex(const CCommandBuffer::SState &State)
	{
		switch(State.m_BlendMode)
		{
		case EBlendMode::NONE:
			return VULKAN_BACKEND_BLEND_MODE_NONE;
		case EBlendMode::ALPHA:
			return VULKAN_BACKEND_BLEND_MODE_ALPHA;
		case EBlendMode::ADDITIVE:
			return VULKAN_BACKEND_BLEND_MODE_ADDITATIVE;
		default:
			dbg_assert_failed("Invalid blend mode: %d", (int)State.m_BlendMode);
		};
	}

	size_t GetDynamicModeIndexFromState(const CCommandBuffer::SState &State) const
	{
		return (State.m_ClipEnable || m_HasDynamicViewport || m_VKSwapImgAndViewportExtent.m_HasForcedViewport) ? VULKAN_BACKEND_CLIP_MODE_DYNAMIC_SCISSOR_AND_VIEWPORT : VULKAN_BACKEND_CLIP_MODE_NONE;
	}

	size_t GetDynamicModeIndexFromExecBuffer(const SRenderCommandExecuteBuffer &ExecBuffer)
	{
		return (ExecBuffer.m_HasDynamicState) ? VULKAN_BACKEND_CLIP_MODE_DYNAMIC_SCISSOR_AND_VIEWPORT : VULKAN_BACKEND_CLIP_MODE_NONE;
	}

	VkPipeline &GetPipeline(SPipelineContainer &Container, bool IsTextured, size_t BlendModeIndex, size_t DynamicIndex)
	{
		return Container.m_aaaPipelines[BlendModeIndex][DynamicIndex][(size_t)IsTextured];
	}

	VkPipelineLayout &GetPipeLayout(SPipelineContainer &Container, bool IsTextured, size_t BlendModeIndex, size_t DynamicIndex)
	{
		return Container.m_aaaPipelineLayouts[BlendModeIndex][DynamicIndex][(size_t)IsTextured];
	}

	VkPipelineLayout &GetStandardPipeLayout(bool IsLineGeometry, bool IsTextured, size_t BlendModeIndex, size_t DynamicIndex)
	{
		if(IsLineGeometry)
			return GetPipeLayout(m_StandardLinePipeline, IsTextured, BlendModeIndex, DynamicIndex);
		else
			return GetPipeLayout(m_StandardPipeline, IsTextured, BlendModeIndex, DynamicIndex);
	}

	VkPipeline &GetStandardPipe(bool IsLineGeometry, bool IsTextured, size_t BlendModeIndex, size_t DynamicIndex)
	{
		if(IsLineGeometry)
			return GetPipeline(m_StandardLinePipeline, IsTextured, BlendModeIndex, DynamicIndex);
		else
			return GetPipeline(m_StandardPipeline, IsTextured, BlendModeIndex, DynamicIndex);
	}

	VkPipelineLayout &GetTileLayerPipeLayout(bool IsBorder, bool IsTextured, size_t BlendModeIndex, size_t DynamicIndex)
	{
		if(!IsBorder)
			return GetPipeLayout(m_TilePipeline, IsTextured, BlendModeIndex, DynamicIndex);
		else
			return GetPipeLayout(m_TileBorderPipeline, IsTextured, BlendModeIndex, DynamicIndex);
	}

	VkPipeline &GetTileLayerPipe(bool IsBorder, bool IsTextured, size_t BlendModeIndex, size_t DynamicIndex)
	{
		if(!IsBorder)
			return GetPipeline(m_TilePipeline, IsTextured, BlendModeIndex, DynamicIndex);
		else
			return GetPipeline(m_TileBorderPipeline, IsTextured, BlendModeIndex, DynamicIndex);
	}

	void GetStateIndices(const SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SState &State, bool &IsTextured, size_t &BlendModeIndex, size_t &DynamicIndex, size_t &AddressModeIndex)
	{
		IsTextured = GetIsTextured(State);
		AddressModeIndex = GetAddressModeIndex(State);
		BlendModeIndex = GetBlendModeIndex(State);
		DynamicIndex = GetDynamicModeIndexFromExecBuffer(ExecBuffer);
	}

	void ExecBufferFillDynamicStates(const CCommandBuffer::SState &State, SRenderCommandExecuteBuffer &ExecBuffer)
	{
		// Workaround for a bug in molten-vk: https://github.com/KhronosGroup/MoltenVK/issues/2304
#ifdef CONF_PLATFORM_MACOS
		auto HasDynamicState = true;
#else
		size_t DynamicStateIndex = GetDynamicModeIndexFromState(State);
		auto HasDynamicState = DynamicStateIndex == VULKAN_BACKEND_CLIP_MODE_DYNAMIC_SCISSOR_AND_VIEWPORT;
#endif

		if(HasDynamicState)
		{
			VkViewport Viewport;
			if(m_HasDynamicViewport)
			{
				Viewport.x = (float)m_DynamicViewportOffset.x;
				Viewport.y = (float)m_DynamicViewportOffset.y;
				Viewport.width = (float)m_DynamicViewportSize.width;
				Viewport.height = (float)m_DynamicViewportSize.height;
				Viewport.minDepth = 0.0f;
				Viewport.maxDepth = 1.0f;
			}
			// else check if there is a forced viewport
			else if(m_VKSwapImgAndViewportExtent.m_HasForcedViewport)
			{
				Viewport.x = 0.0f;
				Viewport.y = 0.0f;
				Viewport.width = (float)m_VKSwapImgAndViewportExtent.m_ForcedViewport.width;
				Viewport.height = (float)m_VKSwapImgAndViewportExtent.m_ForcedViewport.height;
				Viewport.minDepth = 0.0f;
				Viewport.maxDepth = 1.0f;
			}
			else
			{
				Viewport.x = 0.0f;
				Viewport.y = 0.0f;
				Viewport.width = (float)m_VKSwapImgAndViewportExtent.m_SwapImageViewport.width;
				Viewport.height = (float)m_VKSwapImgAndViewportExtent.m_SwapImageViewport.height;
				Viewport.minDepth = 0.0f;
				Viewport.maxDepth = 1.0f;
			}

			VkRect2D Scissor;
			// convert from OGL to vulkan clip

			// the scissor always assumes the presented viewport, because the front-end keeps the calculation
			// for the forced viewport in sync
			auto ScissorViewport = m_VKSwapImgAndViewportExtent.GetPresentedImageViewport();
			if(State.m_ClipEnable)
			{
				int32_t ScissorY = (int32_t)ScissorViewport.height - ((int32_t)State.m_ClipY + (int32_t)State.m_ClipH);
				uint32_t ScissorH = (int32_t)State.m_ClipH;
				Scissor.offset = {(int32_t)State.m_ClipX, ScissorY};
				Scissor.extent = {(uint32_t)State.m_ClipW, ScissorH};
			}
			else
			{
				Scissor.offset = {0, 0};
				Scissor.extent = {ScissorViewport.width, ScissorViewport.height};
			}

			// if there is a dynamic viewport make sure the scissor data is scaled down to that
			if(m_HasDynamicViewport)
			{
				if(ScissorViewport.width > 0 && ScissorViewport.height > 0)
				{
					Scissor.offset.x = (int32_t)(((float)Scissor.offset.x / (float)ScissorViewport.width) * (float)m_DynamicViewportSize.width) + m_DynamicViewportOffset.x;
					Scissor.offset.y = (int32_t)(((float)Scissor.offset.y / (float)ScissorViewport.height) * (float)m_DynamicViewportSize.height) + m_DynamicViewportOffset.y;
					Scissor.extent.width = (uint32_t)(((float)Scissor.extent.width / (float)ScissorViewport.width) * (float)m_DynamicViewportSize.width);
					Scissor.extent.height = (uint32_t)(((float)Scissor.extent.height / (float)ScissorViewport.height) * (float)m_DynamicViewportSize.height);
				}
				else
				{
					Scissor.offset = m_DynamicViewportOffset;
					Scissor.extent = m_DynamicViewportSize;
				}
			}

			Viewport.x = std::clamp(Viewport.x, 0.0f, std::numeric_limits<decltype(Viewport.x)>::max());
			Viewport.y = std::clamp(Viewport.y, 0.0f, std::numeric_limits<decltype(Viewport.y)>::max());

			Scissor.offset.x = std::clamp(Scissor.offset.x, 0, std::numeric_limits<decltype(Scissor.offset.x)>::max());
			Scissor.offset.y = std::clamp(Scissor.offset.y, 0, std::numeric_limits<decltype(Scissor.offset.y)>::max());

			ExecBuffer.m_HasDynamicState = true;
			ExecBuffer.m_Viewport = Viewport;
			ExecBuffer.m_Scissor = Scissor;
		}
		else
		{
			ExecBuffer.m_HasDynamicState = false;
		}
	}

	static bool ViewportsEqual(const VkViewport &Left, const VkViewport &Right)
	{
		return Left.x == Right.x &&
		       Left.y == Right.y &&
		       Left.width == Right.width &&
		       Left.height == Right.height &&
		       Left.minDepth == Right.minDepth &&
		       Left.maxDepth == Right.maxDepth;
	}

	static bool ScissorsEqual(const VkRect2D &Left, const VkRect2D &Right)
	{
		return Left.offset.x == Right.offset.x &&
		       Left.offset.y == Right.offset.y &&
		       Left.extent.width == Right.extent.width &&
		       Left.extent.height == Right.extent.height;
	}

	static bool DynamicStatesEqual(const VkViewport &LeftViewport, const VkViewport &RightViewport, const VkRect2D &LeftScissor, const VkRect2D &RightScissor)
	{
		return ViewportsEqual(LeftViewport, RightViewport) && ScissorsEqual(LeftScissor, RightScissor);
	}

	void BindPipeline(size_t RenderThreadIndex, VkCommandBuffer &CommandBuffer, SRenderCommandExecuteBuffer &ExecBuffer, VkPipeline &BindingPipe, const CCommandBuffer::SState &State)
	{
		auto &Stats = DrawProfileStats(RenderThreadIndex);
		if(m_vLastPipeline[RenderThreadIndex] != BindingPipe)
		{
			vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, BindingPipe);
			m_vLastPipeline[RenderThreadIndex] = BindingPipe;
			Stats.m_PipelineBinds++;
		}
		else
		{
			Stats.m_PipelineBindSkips++;
		}

		size_t DynamicStateIndex = GetDynamicModeIndexFromExecBuffer(ExecBuffer);
		if(DynamicStateIndex == VULKAN_BACKEND_CLIP_MODE_DYNAMIC_SCISSOR_AND_VIEWPORT)
		{
			if(RenderThreadIndex < m_vDrawCommandStates.size())
			{
				auto &DrawState = m_vDrawCommandStates[RenderThreadIndex];
				if(DrawState.m_HasDynamicViewportScissor && DynamicStatesEqual(DrawState.m_Viewport, ExecBuffer.m_Viewport, DrawState.m_Scissor, ExecBuffer.m_Scissor))
				{
					Stats.m_DynamicStateSetSkips++;
					return;
				}
				DrawState.m_HasDynamicViewportScissor = true;
				DrawState.m_Viewport = ExecBuffer.m_Viewport;
				DrawState.m_Scissor = ExecBuffer.m_Scissor;
			}

			vkCmdSetViewport(CommandBuffer, 0, 1, &ExecBuffer.m_Viewport);
			vkCmdSetScissor(CommandBuffer, 0, 1, &ExecBuffer.m_Scissor);
			Stats.m_DynamicStateSets++;
		}
	}

	/**************************
	 * RENDERING IMPLEMENTATION
	 ***************************/

	void RenderTileLayer_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, size_t DrawCalls, const CCommandBuffer::SState &State, size_t BufferContainerIndex)
	{
		size_t BufferObjectIndex = (size_t)m_vBufferContainers[BufferContainerIndex].m_BufferObjectIndex;
		const auto &BufferObject = m_vBufferObjects[BufferObjectIndex];

		ExecBuffer.m_Buffer = BufferObject.m_CurBuffer;
		ExecBuffer.m_BufferOff = BufferObject.m_CurBufferOffset;

		bool IsTextured = GetIsTextured(State);
		if(IsTextured)
		{
			ExecBuffer.m_aDescriptors[0] = m_vTextures[State.m_Texture].m_VKStandard3DTexturedDescrSet;
		}

		ExecBuffer.m_IndexBuffer = m_RenderIndexBuffer;

		ExecBuffer.m_EstimatedRenderCallCount = DrawCalls;

		ExecBufferFillDynamicStates(State, ExecBuffer);
	}

	[[nodiscard]] bool RenderTileLayer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SState &State, bool IsBorder, const GL_SColorf &Color, const vec2 &Scale, const vec2 &Off, size_t IndicesDrawNum, char *const *pIndicesOffsets, const unsigned int *pDrawCount)
	{
		std::array<float, (size_t)4 * 2> m;
		GetStateMatrix(State, m);

		bool IsTextured;
		size_t BlendModeIndex;
		size_t DynamicIndex;
		size_t AddressModeIndex;
		GetStateIndices(ExecBuffer, State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
		auto &PipeLayout = GetTileLayerPipeLayout(IsBorder, IsTextured, BlendModeIndex, DynamicIndex);
		auto &PipeLine = GetTileLayerPipe(IsBorder, IsTextured, BlendModeIndex, DynamicIndex);

		VkCommandBuffer *pCommandBuffer;
		if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
			return false;
		auto &CommandBuffer = *pCommandBuffer;

		BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, State);

		BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_Buffer, (VkDeviceSize)ExecBuffer.m_BufferOff);

		if(IsTextured)
		{
			BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, ExecBuffer.m_aDescriptors[0]);
		}

		SUniformTileGPosBorder VertexPushConstants;
		size_t VertexPushConstantSize = sizeof(SUniformTileGPos);
		SUniformTileGVertColor FragPushConstants;
		size_t FragPushConstantSize = sizeof(SUniformTileGVertColor);

		mem_copy(VertexPushConstants.m_aPos, m.data(), m.size() * sizeof(float));
		FragPushConstants = Color;

		if(IsBorder)
		{
			VertexPushConstants.m_Scale = Scale;
			VertexPushConstants.m_Offset = Off;
			VertexPushConstantSize = sizeof(SUniformTileGPosBorder);
		}

		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, VertexPushConstantSize, &VertexPushConstants);
		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(SUniformTileGPosBorder) + sizeof(SUniformTileGVertColorAlign), FragPushConstantSize, &FragPushConstants);

		size_t DrawCount = IndicesDrawNum;
		BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		for(size_t i = 0; i < DrawCount;)
		{
			ptrdiff_t IndexOffsetBytes = (ptrdiff_t)pIndicesOffsets[i];
			unsigned int MergedDrawCount = pDrawCount[i];
			size_t MergeIndex = i + 1;
			while(MergeIndex < DrawCount)
			{
				const ptrdiff_t NextIndexOffsetBytes = (ptrdiff_t)pIndicesOffsets[MergeIndex];
				const ptrdiff_t CurrentEndOffsetBytes = IndexOffsetBytes + (ptrdiff_t)MergedDrawCount * sizeof(uint32_t);
				if(NextIndexOffsetBytes != CurrentEndOffsetBytes)
					break;
				if(std::numeric_limits<unsigned int>::max() - MergedDrawCount < pDrawCount[MergeIndex])
					break;
				MergedDrawCount += pDrawCount[MergeIndex];
				++MergeIndex;
			}

			VkDeviceSize IndexOffset = (VkDeviceSize)(IndexOffsetBytes / sizeof(uint32_t));
			DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, static_cast<uint32_t>(MergedDrawCount), 1, IndexOffset, 0, 0);
			i = MergeIndex;
		}

		return true;
	}

	template<typename TName, bool Is3DTextured>
	[[nodiscard]] bool RenderStandard(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SState &State, EPrimitiveType PrimType, const TName *pVertices, int PrimitiveCount)
	{
		std::array<float, (size_t)4 * 2> m;
		GetStateMatrix(State, m);

		bool IsLineGeometry = PrimType == EPrimitiveType::LINES;

		bool IsTextured;
		size_t BlendModeIndex;
		size_t DynamicIndex;
		size_t AddressModeIndex;
		GetStateIndices(ExecBuffer, State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
		auto &PipeLayout = Is3DTextured ? GetPipeLayout(m_Standard3DPipeline, IsTextured, BlendModeIndex, DynamicIndex) : GetStandardPipeLayout(IsLineGeometry, IsTextured, BlendModeIndex, DynamicIndex);
		auto &PipeLine = Is3DTextured ? GetPipeline(m_Standard3DPipeline, IsTextured, BlendModeIndex, DynamicIndex) : GetStandardPipe(IsLineGeometry, IsTextured, BlendModeIndex, DynamicIndex);

		VkCommandBuffer *pCommandBuffer;
		if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
			return false;
		auto &CommandBuffer = *pCommandBuffer;

		BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, State);

		size_t VertPerPrim = 2;
		bool IsIndexed = false;
		if(PrimType == EPrimitiveType::QUADS)
		{
			VertPerPrim = 4;
			IsIndexed = true;
		}
		else if(PrimType == EPrimitiveType::TRIANGLES)
		{
			VertPerPrim = 3;
		}

		VkBuffer VKBuffer;
		SDeviceMemoryBlock VKBufferMem;
		size_t BufferOff = 0;
		if(!CreateStreamVertexBuffer(ExecBuffer.m_ThreadIndex, VKBuffer, VKBufferMem, BufferOff, pVertices, VertPerPrim * sizeof(TName) * PrimitiveCount))
			return false;

		BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, VKBuffer, (VkDeviceSize)BufferOff);

		if(IsIndexed)
			BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

		if(IsTextured)
		{
			BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, ExecBuffer.m_aDescriptors[0]);
		}

		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos), m.data());

		if(IsIndexed)
			DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, static_cast<uint32_t>(PrimitiveCount * 6), 1, 0, 0, 0);
		else
			Draw(ExecBuffer.m_ThreadIndex, CommandBuffer, static_cast<uint32_t>(PrimitiveCount * VertPerPrim), 1, 0, 0);

		return true;
	}

public:
	CCommandProcessorFragment_Vulkan()
	{
		m_vTextures.reserve(CCommandBuffer::MAX_TEXTURES);
	}

	/************************
	 * VULKAN SETUP CODE
	 ************************/

	[[nodiscard]] bool GetVulkanExtensions(SDL_Window *pWindow, std::vector<std::string> &vVKExtensions)
	{
		unsigned int ExtCount = 0;
		if(!SDL_Vulkan_GetInstanceExtensions(pWindow, &ExtCount, nullptr))
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Could not get instance extensions from SDL.");
			return false;
		}

		std::vector<const char *> vExtensionList(ExtCount);
		if(!SDL_Vulkan_GetInstanceExtensions(pWindow, &ExtCount, vExtensionList.data()))
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Could not get instance extensions from SDL.");
			return false;
		}

		vVKExtensions.reserve(ExtCount + 1);
		for(uint32_t i = 0; i < ExtCount; i++)
		{
			vVKExtensions.emplace_back(vExtensionList[i]);
		}

#ifdef VK_KHR_portability_enumeration
		vVKExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

		return true;
	}

	std::set<std::string> OurVKLayers()
	{
		std::set<std::string> OurLayers;

		if(g_Config.m_DbgGfx == DEBUG_GFX_MODE_MINIMUM || g_Config.m_DbgGfx == DEBUG_GFX_MODE_ALL)
		{
			OurLayers.emplace("VK_LAYER_KHRONOS_validation");
			// deprecated, but VK_LAYER_KHRONOS_validation was released after vulkan 1.1
			OurLayers.emplace("VK_LAYER_LUNARG_standard_validation");
		}

		return OurLayers;
	}

	std::set<std::string> OurDeviceExtensions()
	{
		std::set<std::string> OurExt;
		OurExt.emplace(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		return OurExt;
	}

	[[nodiscard]] bool ResolveRequestedVulkanApiVersion()
	{
		uint32_t LoaderApiVersion = VK_API_VERSION_1_0;
		auto pfnGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
		if(pfnGetInstanceProcAddr == nullptr)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Could not resolve vkGetInstanceProcAddr from SDL.");
			return false;
		}
		auto pfnEnumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(pfnGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
		if(pfnEnumerateInstanceVersion != nullptr)
		{
			const VkResult Res = pfnEnumerateInstanceVersion(&LoaderApiVersion);
			if(Res != VK_SUCCESS)
			{
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Could not query the Vulkan loader version.", CheckVulkanCriticalError(Res));
				return false;
			}
		}

		const SVulkanVersion LoaderVersion = {
			(int)VK_API_VERSION_MAJOR(LoaderApiVersion),
			(int)VK_API_VERSION_MINOR(LoaderApiVersion),
			(int)VK_API_VERSION_PATCH(LoaderApiVersion)};
		if(!IsVulkanVersionAtLeast(LoaderVersion, gs_BackendVulkanMinimumVersion))
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "Vulkan %d.%d or newer is required, but the installed Vulkan loader only supports %d.%d.%d.", gs_BackendVulkanMinimumVersion.m_Major, gs_BackendVulkanMinimumVersion.m_Minor, LoaderVersion.m_Major, LoaderVersion.m_Minor, LoaderVersion.m_Patch);
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, aBuf);
			return false;
		}

		if(g_Config.m_QmVulkanApiVersion != 11 && g_Config.m_QmVulkanApiVersion != 14)
		{
			log_warn("gfx/vulkan", "Unsupported Vulkan API selection %d; falling back to Vulkan 1.1.", g_Config.m_QmVulkanApiVersion);
			g_Config.m_QmVulkanApiVersion = 11;
		}

		SVulkanVersion RequestedVersion = ResolveConfiguredVulkanApiVersion(g_Config.m_QmVulkanApiVersion);
		if(!IsVulkanVersionAtLeast(LoaderVersion, RequestedVersion))
		{
			log_warn("gfx/vulkan", "Vulkan API %d.%d was selected, but the installed loader only supports %d.%d.%d; falling back to Vulkan 1.1.", RequestedVersion.m_Major, RequestedVersion.m_Minor, LoaderVersion.m_Major, LoaderVersion.m_Minor, LoaderVersion.m_Patch);
			g_Config.m_QmVulkanApiVersion = 11;
			RequestedVersion = gs_BackendVulkanMinimumVersion;
		}
		m_RequestedApiVersion = VK_MAKE_API_VERSION(0, RequestedVersion.m_Major, RequestedVersion.m_Minor, RequestedVersion.m_Patch);
		log_info("gfx/vulkan", "requesting Vulkan API %d.%d.%d, loader supports %d.%d.%d", RequestedVersion.m_Major, RequestedVersion.m_Minor, RequestedVersion.m_Patch, LoaderVersion.m_Major, LoaderVersion.m_Minor, LoaderVersion.m_Patch);
		return true;
	}

	void DestroyVulkanInstance()
	{
		if(m_VKInstance == VK_NULL_HANDLE)
			return;
		if(g_Config.m_DbgGfx == DEBUG_GFX_MODE_MINIMUM || g_Config.m_DbgGfx == DEBUG_GFX_MODE_ALL)
			UnregisterDebugCallback();
		vkDestroyInstance(m_VKInstance, nullptr);
		m_VKInstance = VK_NULL_HANDLE;
	}

	std::vector<VkImageUsageFlags> OurImageUsages()
	{
		std::vector<VkImageUsageFlags> vImgUsages;

		vImgUsages.emplace_back(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
		vImgUsages.emplace_back(VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

		return vImgUsages;
	}

	[[nodiscard]] bool GetVulkanLayers(std::vector<std::string> &vVKLayers)
	{
		uint32_t LayerCount = 0;
		VkResult Res = vkEnumerateInstanceLayerProperties(&LayerCount, NULL);
		if(Res != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Could not get vulkan layers.");
			return false;
		}

		std::vector<VkLayerProperties> vVKInstanceLayers(LayerCount);
		Res = vkEnumerateInstanceLayerProperties(&LayerCount, vVKInstanceLayers.data());
		if(Res != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Could not get vulkan layers.");
			return false;
		}

		std::set<std::string> ReqLayerNames = OurVKLayers();
		vVKLayers.clear();
		for(const auto &LayerName : vVKInstanceLayers)
		{
			if(ReqLayerNames.contains(std::string(LayerName.layerName)))
			{
				vVKLayers.emplace_back(LayerName.layerName);
			}
		}

		return true;
	}

	bool IsGpuDenied(uint32_t Vendor, uint32_t DriverVersion, uint32_t ApiMajor, uint32_t ApiMinor, uint32_t ApiPatch)
	{
#ifdef CONF_FAMILY_WINDOWS
		// AMD
		if(0x1002 == Vendor)
		{
			auto Major = (DriverVersion >> 22);
			auto Minor = (DriverVersion >> 12) & 0x3ff;
			auto Patch = DriverVersion & 0xfff;

			return Major == 2 && Minor == 0 && Patch > 137 && Patch < 220 && ((ApiMajor <= 1 && ApiMinor < 3) || (ApiMajor <= 1 && ApiMinor == 3 && ApiPatch < 206));
		}
#endif
		return false;
	}

	[[nodiscard]] bool CreateVulkanInstance(const std::vector<std::string> &vVKLayers, const std::vector<std::string> &vVKExtensions, bool TryDebugExtensions)
	{
		std::vector<const char *> vLayersCStr;
		vLayersCStr.reserve(vVKLayers.size());
		for(const auto &Layer : vVKLayers)
			vLayersCStr.emplace_back(Layer.c_str());

		std::vector<const char *> vExtCStr;
		vExtCStr.reserve(vVKExtensions.size() + 1);
		for(const auto &Ext : vVKExtensions)
			vExtCStr.emplace_back(Ext.c_str());

#ifdef VK_EXT_debug_utils
		if(TryDebugExtensions && (g_Config.m_DbgGfx == DEBUG_GFX_MODE_MINIMUM || g_Config.m_DbgGfx == DEBUG_GFX_MODE_ALL))
		{
			// debug message support
			vExtCStr.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}
#endif

		VkApplicationInfo VKAppInfo = {};
		VKAppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		VKAppInfo.pNext = NULL;
		VKAppInfo.pApplicationName = "DDNet";
		VKAppInfo.applicationVersion = 1;
		VKAppInfo.pEngineName = "DDNet-Vulkan";
		VKAppInfo.engineVersion = 1;
		VKAppInfo.apiVersion = m_RequestedApiVersion;

		void *pExt = nullptr;
#if defined(VK_EXT_validation_features) && VK_EXT_VALIDATION_FEATURES_SPEC_VERSION >= 5
		VkValidationFeaturesEXT Features = {};
		std::array<VkValidationFeatureEnableEXT, 2> aEnables = {VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT, VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT};
		if(TryDebugExtensions && (g_Config.m_DbgGfx == DEBUG_GFX_MODE_AFFECTS_PERFORMANCE || g_Config.m_DbgGfx == DEBUG_GFX_MODE_ALL))
		{
			Features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
			Features.enabledValidationFeatureCount = aEnables.size();
			Features.pEnabledValidationFeatures = aEnables.data();

			pExt = &Features;
		}
#endif

		VkInstanceCreateInfo VKInstanceInfo = {};
		VKInstanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		VKInstanceInfo.pNext = pExt;
		VKInstanceInfo.flags = 0;
#ifdef VK_KHR_portability_enumeration
		VKInstanceInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
		VKInstanceInfo.pApplicationInfo = &VKAppInfo;
		VKInstanceInfo.enabledExtensionCount = static_cast<uint32_t>(vExtCStr.size());
		VKInstanceInfo.ppEnabledExtensionNames = vExtCStr.data();
		VKInstanceInfo.enabledLayerCount = static_cast<uint32_t>(vLayersCStr.size());
		VKInstanceInfo.ppEnabledLayerNames = vLayersCStr.data();

		bool TryAgain = false;

		VkInstance CreatedInstance = VK_NULL_HANDLE;
		VkResult Res = vkCreateInstance(&VKInstanceInfo, NULL, &CreatedInstance);
		m_LastVulkanInstanceCreateResult = Res;
		const char *pCritErrorMsg = CheckVulkanCriticalError(Res);
		if(pCritErrorMsg != nullptr)
		{
			if(m_RequestedApiVersion == VK_API_VERSION_1_4 && Res == VK_ERROR_INCOMPATIBLE_DRIVER)
			{
				log_warn("gfx/vulkan", "The Vulkan driver rejected the requested Vulkan 1.4 instance; Vulkan 1.1 will be tried instead.");
			}
			else
			{
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating instance failed.", pCritErrorMsg);
			}
			return false;
		}
		else if(Res == VK_ERROR_LAYER_NOT_PRESENT || Res == VK_ERROR_EXTENSION_NOT_PRESENT)
			TryAgain = true;

		if(TryAgain && TryDebugExtensions)
			return CreateVulkanInstance(vVKLayers, vVKExtensions, false);

		if(Res != VK_SUCCESS)
		{
			if(pCritErrorMsg == nullptr)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating instance failed.");
			return false;
		}

		m_VKInstance = CreatedInstance;
		return true;
	}

	STWGraphicGpu::ETWGraphicsGpuType VKGPUTypeToGraphicsGpuType(VkPhysicalDeviceType VKGPUType)
	{
		if(VKGPUType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			return STWGraphicGpu::ETWGraphicsGpuType::GRAPHICS_GPU_TYPE_DISCRETE;
		else if(VKGPUType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
			return STWGraphicGpu::ETWGraphicsGpuType::GRAPHICS_GPU_TYPE_INTEGRATED;
		else if(VKGPUType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)
			return STWGraphicGpu::ETWGraphicsGpuType::GRAPHICS_GPU_TYPE_VIRTUAL;
		else if(VKGPUType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_CPU)
			return STWGraphicGpu::ETWGraphicsGpuType::GRAPHICS_GPU_TYPE_CPU;

		return STWGraphicGpu::ETWGraphicsGpuType::GRAPHICS_GPU_TYPE_CPU;
	}

	// from: https://github.com/SaschaWillems/vulkan.gpuinfo.org/blob/5c3986798afc39d736b825bf8a5fbf92b8d9ed49/includes/functions.php#L364
	const char *GetDriverVersion(char (&aBuff)[256], uint32_t DriverVersion, uint32_t VendorId)
	{
		// NVIDIA
		if(VendorId == 4318)
		{
			str_format(aBuff, std::size(aBuff), "%d.%d.%d.%d",
				(DriverVersion >> 22) & 0x3ff,
				(DriverVersion >> 14) & 0x0ff,
				(DriverVersion >> 6) & 0x0ff,
				(DriverVersion) & 0x003f);
		}
#ifdef CONF_FAMILY_WINDOWS
		// windows only
		else if(VendorId == 0x8086)
		{
			str_format(aBuff, std::size(aBuff),
				"%d.%d",
				(DriverVersion >> 14),
				(DriverVersion) & 0x3fff);
		}
#endif
		else
		{
			// Use Vulkan version conventions if vendor mapping is not available
			str_format(aBuff, std::size(aBuff),
				"%d.%d.%d",
				(DriverVersion >> 22),
				(DriverVersion >> 12) & 0x3ff,
				DriverVersion & 0xfff);
		}

		return aBuff;
	}

	[[nodiscard]] bool SelectGpu(char *pRendererName, char *pVendorName, char *pVersionName)
	{
		m_RequiredVulkanVersionUnavailable = false;
		const SVulkanVersion RequiredVersion = {
			(int)VK_API_VERSION_MAJOR(m_RequestedApiVersion),
			(int)VK_API_VERSION_MINOR(m_RequestedApiVersion),
			(int)VK_API_VERSION_PATCH(m_RequestedApiVersion)};
		uint32_t DevicesCount = 0;
		auto Res = vkEnumeratePhysicalDevices(m_VKInstance, &DevicesCount, nullptr);
		if(Res != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, CheckVulkanCriticalError(Res));
			return false;
		}
		if(DevicesCount == 0)
		{
			m_RequiredVulkanVersionUnavailable = true;
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "No vulkan compatible devices found.");
			return false;
		}

		std::vector<VkPhysicalDevice> vDeviceList(DevicesCount);
		Res = vkEnumeratePhysicalDevices(m_VKInstance, &DevicesCount, vDeviceList.data());
		if(Res != VK_SUCCESS && Res != VK_INCOMPLETE)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, CheckVulkanCriticalError(Res));
			return false;
		}
		if(DevicesCount == 0)
		{
			m_RequiredVulkanVersionUnavailable = true;
			SetWarning(EGfxWarningType::GFX_WARNING_TYPE_INIT_FAILED_MISSING_INTEGRATED_GPU_DRIVER, "No vulkan compatible devices found.");
			return false;
		}
		// make sure to use the correct amount of devices available
		// the amount of physical devices can be smaller than the amount of devices reported
		// see vkEnumeratePhysicalDevices for details
		vDeviceList.resize(DevicesCount);

		size_t Index = 0;
		std::vector<VkPhysicalDeviceProperties> vDevicePropList(vDeviceList.size());
		m_pGpuList->m_vGpus.reserve(vDeviceList.size());

		const size_t InvalidDeviceIndex = std::numeric_limits<size_t>::max();
		size_t FoundDeviceIndex = InvalidDeviceIndex;
		size_t FirstCompatibleDeviceIndex = InvalidDeviceIndex;

		STWGraphicGpu::ETWGraphicsGpuType AutoGpuType = STWGraphicGpu::ETWGraphicsGpuType::GRAPHICS_GPU_TYPE_INVALID;

		bool IsAutoGpu = str_comp(g_Config.m_GfxGpuName, "auto") == 0;
		bool HasRequiredVersionDevice = false;

		bool UserSelectedGpuChosen = false;
		for(auto &CurDevice : vDeviceList)
		{
			vkGetPhysicalDeviceProperties(CurDevice, &(vDevicePropList[Index]));

			auto &DeviceProp = vDevicePropList[Index];

			STWGraphicGpu::ETWGraphicsGpuType GPUType = VKGPUTypeToGraphicsGpuType(DeviceProp.deviceType);

			int DevApiMajor = (int)VK_API_VERSION_MAJOR(DeviceProp.apiVersion);
			int DevApiMinor = (int)VK_API_VERSION_MINOR(DeviceProp.apiVersion);
			int DevApiPatch = (int)VK_API_VERSION_PATCH(DeviceProp.apiVersion);

			auto IsDenied = CCommandProcessorFragment_Vulkan::IsGpuDenied(DeviceProp.vendorID, DeviceProp.driverVersion, DevApiMajor, DevApiMinor, DevApiPatch);
			const SVulkanVersion DeviceVersion = {DevApiMajor, DevApiMinor, DevApiPatch};
			if(IsVulkanVersionAtLeast(DeviceVersion, RequiredVersion))
			{
				HasRequiredVersionDevice = true;
			}
			if(IsVulkanVersionAtLeast(DeviceVersion, RequiredVersion) && !IsDenied)
			{
				if(FirstCompatibleDeviceIndex == InvalidDeviceIndex)
					FirstCompatibleDeviceIndex = Index;

				STWGraphicGpu::STWGraphicGpuItem NewGpu;
				str_copy(NewGpu.m_aName, DeviceProp.deviceName);
				NewGpu.m_GpuType = GPUType;
				m_pGpuList->m_vGpus.push_back(NewGpu);

				// We always decide what the 'auto' GPU would be, even if user is forcing a GPU by name in config
				// Reminder: A worse GPU enumeration has a higher value than a better GPU enumeration, thus the '>'
				if(AutoGpuType > STWGraphicGpu::ETWGraphicsGpuType::GRAPHICS_GPU_TYPE_INTEGRATED)
				{
					str_copy(m_pGpuList->m_AutoGpu.m_aName, DeviceProp.deviceName);
					m_pGpuList->m_AutoGpu.m_GpuType = GPUType;

					AutoGpuType = GPUType;

					if(IsAutoGpu)
						FoundDeviceIndex = Index;
				}
				// We only select the first GPU that matches, because it comes first in the enumeration array, it's preferred by the system
				// Reminder: We can't break the cycle here if the name matches because we need to choose the best GPU for 'auto' mode
				if(!IsAutoGpu && !UserSelectedGpuChosen && str_comp(DeviceProp.deviceName, g_Config.m_GfxGpuName) == 0)
				{
					FoundDeviceIndex = Index;
					UserSelectedGpuChosen = true;
				}
			}
			Index++;
		}

		if(m_pGpuList->m_vGpus.empty())
		{
			m_RequiredVulkanVersionUnavailable = !HasRequiredVersionDevice;
			char aBuf[256];
			if(m_RequiredVulkanVersionUnavailable)
			{
				str_format(aBuf, sizeof(aBuf), "No device supporting the required Vulkan %d.%d API was found.", RequiredVersion.m_Major, RequiredVersion.m_Minor);
				SetWarning(EGfxWarningType::GFX_WARNING_TYPE_INIT_FAILED_NO_DEVICE_WITH_REQUIRED_VERSION, aBuf);
			}
			else
			{
				SetWarning(EGfxWarningType::GFX_WARNING_TYPE_INIT_FAILED_MISSING_INTEGRATED_GPU_DRIVER, "No vulkan compatible devices found.");
			}
			return false;
		}
		if(FoundDeviceIndex == InvalidDeviceIndex)
		{
			dbg_msg("vulkan", "configured graphics card is unavailable for the selected Vulkan version, using the automatic GPU instead.");
			FoundDeviceIndex = FirstCompatibleDeviceIndex;
		}

		{
			auto &DeviceProp = vDevicePropList[FoundDeviceIndex];

			int DevApiMajor = (int)VK_API_VERSION_MAJOR(DeviceProp.apiVersion);
			int DevApiMinor = (int)VK_API_VERSION_MINOR(DeviceProp.apiVersion);
			int DevApiPatch = (int)VK_API_VERSION_PATCH(DeviceProp.apiVersion);
			const SVulkanVersion DeviceVersion = ClampVulkanVersionToSupportedRange({DevApiMajor, DevApiMinor, DevApiPatch});
			const SVulkanVersion InstanceVersion = {
				(int)VK_API_VERSION_MAJOR(m_RequestedApiVersion),
				(int)VK_API_VERSION_MINOR(m_RequestedApiVersion),
				(int)VK_API_VERSION_PATCH(m_RequestedApiVersion)};
			const SVulkanVersion EffectiveVersion = MinVulkanVersion(InstanceVersion, DeviceVersion);
			m_EffectiveApiVersion = VK_MAKE_API_VERSION(0, EffectiveVersion.m_Major, EffectiveVersion.m_Minor, EffectiveVersion.m_Patch);

			str_copy(pRendererName, DeviceProp.deviceName, gs_GpuInfoStringSize);
			const char *pVendorNameStr = NULL;
			switch(DeviceProp.vendorID)
			{
			case 0x1002:
				pVendorNameStr = "AMD";
				break;
			case 0x1010:
				pVendorNameStr = "ImgTec";
				break;
			case 0x106B:
				pVendorNameStr = "Apple";
				break;
			case 0x10DE:
				pVendorNameStr = "NVIDIA";
				break;
			case 0x13B5:
				pVendorNameStr = "ARM";
				break;
			case 0x5143:
				pVendorNameStr = "Qualcomm";
				break;
			case 0x8086:
				pVendorNameStr = "INTEL";
				break;
			case 0x10005:
				pVendorNameStr = "Mesa";
				break;
			default:
				dbg_msg("vulkan", "unknown gpu vendor %u", DeviceProp.vendorID);
				pVendorNameStr = "unknown";
				break;
			}

			char aBuff[256];
			str_copy(pVendorName, pVendorNameStr, gs_GpuInfoStringSize);
			str_format(pVersionName, gs_GpuInfoStringSize, "Vulkan %d.%d.%d (driver: %s)", DevApiMajor, DevApiMinor, DevApiPatch, GetDriverVersion(aBuff, DeviceProp.driverVersion, DeviceProp.vendorID));
			log_info("gfx/vulkan", "effective Vulkan API %d.%d.%d (instance %u.%u.%u, device %d.%d.%d)", EffectiveVersion.m_Major, EffectiveVersion.m_Minor, EffectiveVersion.m_Patch, VK_API_VERSION_MAJOR(m_RequestedApiVersion), VK_API_VERSION_MINOR(m_RequestedApiVersion), VK_API_VERSION_PATCH(m_RequestedApiVersion), DevApiMajor, DevApiMinor, DevApiPatch);

			// get important device limits
			m_NonCoherentMemAlignment = DeviceProp.limits.nonCoherentAtomSize;
			m_OptimalImageCopyMemAlignment = DeviceProp.limits.optimalBufferCopyOffsetAlignment;
			m_MaxTextureSize = DeviceProp.limits.maxImageDimension2D;
			m_MaxSamplerAnisotropy = DeviceProp.limits.maxSamplerAnisotropy;

			m_MinUniformAlign = DeviceProp.limits.minUniformBufferOffsetAlignment;
			m_MaxMultiSample = DeviceProp.limits.framebufferColorSampleCounts;
			m_FrameTimestampPeriod = DeviceProp.limits.timestampPeriod;
			m_FrameTimestampQueriesSupported = DeviceProp.limits.timestampComputeAndGraphics == VK_TRUE && m_FrameTimestampPeriod > 0.0f;

			if(IsVerbose())
			{
				dbg_msg("vulkan", "device prop: non-coherent align: %" PRIzu ", optimal image copy align: %" PRIzu ", max texture size: %u, max sampler anisotropy: %u", (size_t)m_NonCoherentMemAlignment, (size_t)m_OptimalImageCopyMemAlignment, m_MaxTextureSize, m_MaxSamplerAnisotropy);
				dbg_msg("vulkan", "device prop: min uniform align: %u, multi sample: %u, timestamp period: %.3f", m_MinUniformAlign, (uint32_t)m_MaxMultiSample, m_FrameTimestampPeriod);
			}
		}

		VkPhysicalDevice CurDevice = vDeviceList[FoundDeviceIndex];

		uint32_t FamQueueCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(CurDevice, &FamQueueCount, nullptr);
		if(FamQueueCount == 0)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "No vulkan queue family properties found.");
			return false;
		}

		std::vector<VkQueueFamilyProperties> vQueuePropList(FamQueueCount);
		vkGetPhysicalDeviceQueueFamilyProperties(CurDevice, &FamQueueCount, vQueuePropList.data());

		uint32_t QueueNodeIndex = std::numeric_limits<uint32_t>::max();
		for(uint32_t i = 0; i < FamQueueCount; i++)
		{
			if(vQueuePropList[i].queueCount > 0 && (vQueuePropList[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
			{
				QueueNodeIndex = i;
			}
			/*if(vQueuePropList[i].queueCount > 0 && (vQueuePropList[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
			{
				QueueNodeIndex = i;
			}*/
		}

		if(QueueNodeIndex == std::numeric_limits<uint32_t>::max())
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "No vulkan queue found that matches the requirements: graphics queue.");
			return false;
		}
		m_FrameTimestampValidBits = vQueuePropList[QueueNodeIndex].timestampValidBits;
		m_FrameTimestampQueriesSupported = m_FrameTimestampQueriesSupported && m_FrameTimestampValidBits > 0;

		m_VKGPU = CurDevice;
		m_VKGraphicsQueueIndex = QueueNodeIndex;
		return true;
	}

	[[nodiscard]] bool CreateLogicalDevice(const std::vector<std::string> &vVKLayers)
	{
		std::vector<const char *> vLayerCNames;
		vLayerCNames.reserve(vVKLayers.size());
		for(const auto &Layer : vVKLayers)
			vLayerCNames.emplace_back(Layer.c_str());

		uint32_t DevPropCount = 0;
		if(vkEnumerateDeviceExtensionProperties(m_VKGPU, NULL, &DevPropCount, NULL) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Querying logical device extension properties failed.");
			return false;
		}

		std::vector<VkExtensionProperties> vDevPropList(DevPropCount);
		if(vkEnumerateDeviceExtensionProperties(m_VKGPU, NULL, &DevPropCount, vDevPropList.data()) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Querying logical device extension properties failed.");
			return false;
		}

		std::vector<const char *> vDevPropCNames;
		std::set<std::string> OurDevExt = OurDeviceExtensions();
		std::set<std::string> FoundDevExt;

		for(const auto &CurExtProp : vDevPropList)
		{
			if(OurDevExt.contains(std::string(CurExtProp.extensionName)))
			{
				vDevPropCNames.emplace_back(CurExtProp.extensionName);
				FoundDevExt.emplace(CurExtProp.extensionName);
			}
		}
		for(const auto &ReqExt : OurDevExt)
		{
			if(!FoundDevExt.contains(ReqExt))
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "Missing required Vulkan device extension: %s", ReqExt.c_str());
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, aBuf);
				return false;
			}
		}

		VkDeviceQueueCreateInfo VKQueueCreateInfo;
		VKQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		VKQueueCreateInfo.queueFamilyIndex = m_VKGraphicsQueueIndex;
		VKQueueCreateInfo.queueCount = 1;
		float QueuePrio = 1.0f;
		VKQueueCreateInfo.pQueuePriorities = &QueuePrio;
		VKQueueCreateInfo.pNext = NULL;
		VKQueueCreateInfo.flags = 0;

		VkDeviceCreateInfo VKCreateInfo;
		VKCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		VKCreateInfo.queueCreateInfoCount = 1;
		VKCreateInfo.pQueueCreateInfos = &VKQueueCreateInfo;
		VKCreateInfo.ppEnabledLayerNames = vLayerCNames.data();
		VKCreateInfo.enabledLayerCount = static_cast<uint32_t>(vLayerCNames.size());
		VKCreateInfo.ppEnabledExtensionNames = vDevPropCNames.data();
		VKCreateInfo.enabledExtensionCount = static_cast<uint32_t>(vDevPropCNames.size());
		VKCreateInfo.pNext = NULL;
		VKCreateInfo.pEnabledFeatures = NULL;
		VKCreateInfo.flags = 0;

		if(vkCreateDevice(m_VKGPU, &VKCreateInfo, nullptr, &m_VKDevice) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Logical device could not be created.");
			return false;
		}

		m_pfnCreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(vkGetDeviceProcAddr(m_VKDevice, "vkCreateSwapchainKHR"));
		m_pfnDestroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(vkGetDeviceProcAddr(m_VKDevice, "vkDestroySwapchainKHR"));
		m_pfnGetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(vkGetDeviceProcAddr(m_VKDevice, "vkGetSwapchainImagesKHR"));
		m_pfnAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(vkGetDeviceProcAddr(m_VKDevice, "vkAcquireNextImageKHR"));
		m_pfnQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(vkGetDeviceProcAddr(m_VKDevice, "vkQueuePresentKHR"));
		if(m_pfnCreateSwapchainKHR == nullptr || m_pfnDestroySwapchainKHR == nullptr || m_pfnGetSwapchainImagesKHR == nullptr || m_pfnAcquireNextImageKHR == nullptr || m_pfnQueuePresentKHR == nullptr)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Failed to resolve Vulkan swapchain function pointers. Try gfx_backend OpenGL.");
			return false;
		}

		return true;
	}

	[[nodiscard]] bool CreateSurface(SDL_Window *pWindow)
	{
		if(!SDL_Vulkan_CreateSurface(pWindow, m_VKInstance, &m_VKPresentSurface))
		{
			dbg_msg("vulkan", "error from sdl: %s", SDL_GetError());
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating a vulkan surface for the SDL window failed.");
			return false;
		}

		VkBool32 IsSupported = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(m_VKGPU, m_VKGraphicsQueueIndex, m_VKPresentSurface, &IsSupported);
		if(!IsSupported)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "The device surface does not support presenting the framebuffer to a screen. (maybe the wrong GPU was selected?)");
			return false;
		}

		return true;
	}

	void DestroySurface()
	{
		vkDestroySurfaceKHR(m_VKInstance, m_VKPresentSurface, nullptr);
	}

	[[nodiscard]] bool GetPresentationMode(VkPresentModeKHR &VKIOMode)
	{
		uint32_t PresentModeCount = 0;
		if(vkGetPhysicalDeviceSurfacePresentModesKHR(m_VKGPU, m_VKPresentSurface, &PresentModeCount, NULL) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "The device surface presentation modes could not be fetched.");
			return false;
		}

		std::vector<VkPresentModeKHR> vPresentModeList(PresentModeCount);
		if(vkGetPhysicalDeviceSurfacePresentModesKHR(m_VKGPU, m_VKPresentSurface, &PresentModeCount, vPresentModeList.data()) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "The device surface presentation modes could not be fetched.");
			return false;
		}

		std::array<VkPresentModeKHR, 2> aPreferredModes;
		if(g_Config.m_GfxVsync)
			aPreferredModes = {
				VK_PRESENT_MODE_FIFO_KHR,
				VK_PRESENT_MODE_FIFO_RELAXED_KHR};
		else
			aPreferredModes = {
				VK_PRESENT_MODE_IMMEDIATE_KHR,
				VK_PRESENT_MODE_MAILBOX_KHR};
		for(const VkPresentModeKHR PreferredMode : aPreferredModes)
		{
			for(const VkPresentModeKHR AvailableMode : vPresentModeList)
			{
				if(AvailableMode == PreferredMode)
				{
					VKIOMode = PreferredMode;
					return true;
				}
			}
		}

		if(PresentModeCount == 0)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "The device surface reported no usable presentation mode.");
			return false;
		}

		log_warn("gfx/vulkan", "Requested presentation modes were not available. Using the surface default.");
		VKIOMode = vPresentModeList[0];

		return true;
	}

	[[nodiscard]] bool GetSurfaceProperties(VkSurfaceCapabilitiesKHR &VKSurfCapabilities)
	{
		if(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_VKGPU, m_VKPresentSurface, &VKSurfCapabilities) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "The device surface capabilities could not be fetched.");
			return false;
		}
		return true;
	}

	uint32_t GetNumberOfSwapImages(const VkSurfaceCapabilitiesKHR &VKCapabilities)
	{
		uint32_t ImgNumber = VKCapabilities.minImageCount + 1;
		if(IsVerbose())
		{
			dbg_msg("vulkan", "minimal swap image count %u", VKCapabilities.minImageCount);
		}
		return (VKCapabilities.maxImageCount > 0 && ImgNumber > VKCapabilities.maxImageCount) ? VKCapabilities.maxImageCount : ImgNumber;
	}

	SSwapImgViewportExtent GetSwapImageSize(const VkSurfaceCapabilitiesKHR &VKCapabilities)
	{
		VkExtent2D RetSize = {m_CanvasWidth, m_CanvasHeight};

		if(VKCapabilities.currentExtent.width == std::numeric_limits<uint32_t>::max())
		{
			RetSize.width = std::clamp<uint32_t>(RetSize.width, VKCapabilities.minImageExtent.width, VKCapabilities.maxImageExtent.width);
			RetSize.height = std::clamp<uint32_t>(RetSize.height, VKCapabilities.minImageExtent.height, VKCapabilities.maxImageExtent.height);
		}
		else
		{
			RetSize = VKCapabilities.currentExtent;
		}

		VkExtent2D AutoViewportExtent = RetSize;
		bool UsesForcedViewport = false;
		// keep this in sync with graphics_threaded AdjustViewport's check
		if(AutoViewportExtent.height > 4 * AutoViewportExtent.width / 5 && g_GraphicsForcedAspect)
		{
			AutoViewportExtent.height = 4 * AutoViewportExtent.width / 5;
			UsesForcedViewport = true;
		}

		SSwapImgViewportExtent Ext;
		Ext.m_SwapImageViewport = RetSize;
		Ext.m_ForcedViewport = AutoViewportExtent;
		Ext.m_HasForcedViewport = UsesForcedViewport;

		return Ext;
	}

	[[nodiscard]] bool GetImageUsage(const VkSurfaceCapabilitiesKHR &VKCapabilities, VkImageUsageFlags &VKOutUsage)
	{
		std::vector<VkImageUsageFlags> vOurImgUsages = OurImageUsages();
		if(vOurImgUsages.empty())
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Framebuffer image attachment types not supported.");
			return false;
		}

		VKOutUsage = vOurImgUsages[0];

		for(const auto &ImgUsage : vOurImgUsages)
		{
			VkImageUsageFlags ImgUsageFlags = ImgUsage & VKCapabilities.supportedUsageFlags;
			if(ImgUsageFlags != ImgUsage)
			{
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Framebuffer image attachment types not supported.");
				return false;
			}

			VKOutUsage = (VKOutUsage | ImgUsage);
		}

		return true;
	}

	VkSurfaceTransformFlagBitsKHR GetTransform(const VkSurfaceCapabilitiesKHR &VKCapabilities)
	{
		if(VKCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
			return VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		return VKCapabilities.currentTransform;
	}

	[[nodiscard]] bool GetFormat()
	{
		uint32_t SurfFormats = 0;
		VkResult Res = vkGetPhysicalDeviceSurfaceFormatsKHR(m_VKGPU, m_VKPresentSurface, &SurfFormats, nullptr);
		if(Res != VK_SUCCESS && Res != VK_INCOMPLETE)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "The device surface format fetching failed.");
			return false;
		}

		std::vector<VkSurfaceFormatKHR> vSurfFormatList(SurfFormats);
		Res = vkGetPhysicalDeviceSurfaceFormatsKHR(m_VKGPU, m_VKPresentSurface, &SurfFormats, vSurfFormatList.data());
		if(Res != VK_SUCCESS && Res != VK_INCOMPLETE)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "The device surface format fetching failed.");
			return false;
		}

		if(Res == VK_INCOMPLETE)
		{
			dbg_msg("vulkan", "warning: not all surface formats are requestable with your current settings.");
		}

		if(vSurfFormatList.size() == 1 && vSurfFormatList[0].format == VK_FORMAT_UNDEFINED)
		{
			m_VKSurfFormat.format = VK_FORMAT_B8G8R8A8_UNORM;
			m_VKSurfFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
			dbg_msg("vulkan", "warning: surface format was undefined. This can potentially cause bugs.");
			return true;
		}

		for(const auto &FindFormat : vSurfFormatList)
		{
			if(FindFormat.format == VK_FORMAT_B8G8R8A8_UNORM && FindFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				m_VKSurfFormat = FindFormat;
				return true;
			}
			else if(FindFormat.format == VK_FORMAT_R8G8B8A8_UNORM && FindFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				m_VKSurfFormat = FindFormat;
				return true;
			}
		}

		dbg_msg("vulkan", "warning: surface format was not RGBA(or variants of it). This can potentially cause weird looking images(too bright etc.).");
		m_VKSurfFormat = vSurfFormatList[0];
		return true;
	}

	[[nodiscard]] bool CreateSwapChain(VkSwapchainKHR &OldSwapChain)
	{
		VkSurfaceCapabilitiesKHR VKSurfCap;
		if(!GetSurfaceProperties(VKSurfCap))
			return false;

		VkPresentModeKHR PresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		if(!GetPresentationMode(PresentMode))
			return false;

		uint32_t SwapImgCount = GetNumberOfSwapImages(VKSurfCap);

#if defined(CONF_PLATFORM_MACOS)
		if(g_Config.m_QmMacosGraphicsDiagnostics != 0)
		{
			log_info("gfx/vulkan", "swapchain present: vsync=%d mode=%d images=%u refresh=%d screen_refresh=%d", g_Config.m_GfxVsync, (int)PresentMode, SwapImgCount, g_Config.m_GfxRefreshRate, g_Config.m_GfxScreenRefreshRate);
		}
#endif

		m_VKSwapImgAndViewportExtent = GetSwapImageSize(VKSurfCap);

		VkImageUsageFlags UsageFlags;
		if(!GetImageUsage(VKSurfCap, UsageFlags))
			return false;

		VkSurfaceTransformFlagBitsKHR TransformFlagBits = GetTransform(VKSurfCap);

		if(!GetFormat())
			return false;

		OldSwapChain = m_VKSwapChain;

		VkSwapchainCreateInfoKHR SwapInfo;
		SwapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		SwapInfo.pNext = nullptr;
		SwapInfo.flags = 0;
		SwapInfo.surface = m_VKPresentSurface;
		SwapInfo.minImageCount = SwapImgCount;
		SwapInfo.imageFormat = m_VKSurfFormat.format;
		SwapInfo.imageColorSpace = m_VKSurfFormat.colorSpace;
		SwapInfo.imageExtent = m_VKSwapImgAndViewportExtent.m_SwapImageViewport;
		SwapInfo.imageArrayLayers = 1;
		SwapInfo.imageUsage = UsageFlags;
		SwapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		SwapInfo.queueFamilyIndexCount = 0;
		SwapInfo.pQueueFamilyIndices = nullptr;
		SwapInfo.preTransform = TransformFlagBits;
		SwapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		SwapInfo.presentMode = PresentMode;
		SwapInfo.clipped = true;
		SwapInfo.oldSwapchain = OldSwapChain;

		m_VKSwapChain = VK_NULL_HANDLE;
		VkResult SwapchainCreateRes = m_pfnCreateSwapchainKHR(m_VKDevice, &SwapInfo, nullptr, &m_VKSwapChain);
		const char *pCritErrorMsg = CheckVulkanCriticalError(SwapchainCreateRes);
		if(pCritErrorMsg != nullptr)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating the swap chain failed.", pCritErrorMsg);
			return false;
		}
		else if(SwapchainCreateRes == VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
			return false;

		return true;
	}

	void DestroySwapChain(bool ForceDestroy)
	{
		if(ForceDestroy)
		{
			m_pfnDestroySwapchainKHR(m_VKDevice, m_VKSwapChain, nullptr);
			m_VKSwapChain = VK_NULL_HANDLE;
		}
	}

	[[nodiscard]] bool GetSwapChainImageHandles()
	{
		uint32_t ImgCount = 0;
		if(m_pfnGetSwapchainImagesKHR(m_VKDevice, m_VKSwapChain, &ImgCount, nullptr) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Could not get swap chain images.");
			return false;
		}

		m_SwapChainImageCount = ImgCount;

		m_vSwapChainImages.resize(ImgCount);
		if(m_pfnGetSwapchainImagesKHR(m_VKDevice, m_VKSwapChain, &ImgCount, m_vSwapChainImages.data()) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Could not get swap chain images.");
			return false;
		}

		return true;
	}

	void ClearSwapChainImageHandles()
	{
		m_vSwapChainImages.clear();
	}

	void GetDeviceQueue()
	{
		vkGetDeviceQueue(m_VKDevice, m_VKGraphicsQueueIndex, 0, &m_VKGraphicsQueue);
		vkGetDeviceQueue(m_VKDevice, m_VKGraphicsQueueIndex, 0, &m_VKPresentQueue);
	}

#ifdef VK_EXT_debug_utils
	static VKAPI_ATTR VkBool32 VKAPI_CALL VKDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT MessageSeverity, VkDebugUtilsMessageTypeFlagsEXT MessageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData)
	{
		if((MessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
		{
			dbg_msg("vulkan_debug", "validation error: %s", pCallbackData->pMessage);
		}
		else
		{
			dbg_msg("vulkan_debug", "%s", pCallbackData->pMessage);
		}

		return VK_FALSE;
	}

	VkResult CreateDebugUtilsMessengerEXT(const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger)
	{
		auto pfnVulkanCreateDebugUtilsFunction = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_VKInstance, "vkCreateDebugUtilsMessengerEXT");
		if(pfnVulkanCreateDebugUtilsFunction != nullptr)
		{
			return pfnVulkanCreateDebugUtilsFunction(m_VKInstance, pCreateInfo, pAllocator, pDebugMessenger);
		}
		else
		{
			return VK_ERROR_EXTENSION_NOT_PRESENT;
		}
	}

	void DestroyDebugUtilsMessengerEXT(VkDebugUtilsMessengerEXT &DebugMessenger)
	{
		auto pfnVulkanDestroyDebugUtilsFunction = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_VKInstance, "vkDestroyDebugUtilsMessengerEXT");
		if(pfnVulkanDestroyDebugUtilsFunction != nullptr)
		{
			pfnVulkanDestroyDebugUtilsFunction(m_VKInstance, DebugMessenger, nullptr);
		}
	}
#endif

	void SetupDebugCallback()
	{
#ifdef VK_EXT_debug_utils
		VkDebugUtilsMessengerCreateInfoEXT CreateInfo = {};
		CreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		CreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		CreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT; // | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT <- too annoying
		CreateInfo.pfnUserCallback = VKDebugCallback;

		if(CreateDebugUtilsMessengerEXT(&CreateInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS)
		{
			m_DebugMessenger = VK_NULL_HANDLE;
			dbg_msg("vulkan", "didn't find vulkan debug layer.");
		}
		else
		{
			dbg_msg("vulkan", "enabled vulkan debug context.");
		}
#endif
	}

	void UnregisterDebugCallback()
	{
#ifdef VK_EXT_debug_utils
		if(m_DebugMessenger != VK_NULL_HANDLE)
		{
			DestroyDebugUtilsMessengerEXT(m_DebugMessenger);
			m_DebugMessenger = VK_NULL_HANDLE;
		}
#endif
	}

	[[nodiscard]] bool CreateImageViews()
	{
		m_vSwapChainImageViewList.resize(m_SwapChainImageCount);

		for(size_t i = 0; i < m_SwapChainImageCount; i++)
		{
			VkImageViewCreateInfo CreateInfo{};
			CreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			CreateInfo.image = m_vSwapChainImages[i];
			CreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			CreateInfo.format = m_VKSurfFormat.format;
			CreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			CreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			CreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			CreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			CreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			CreateInfo.subresourceRange.baseMipLevel = 0;
			CreateInfo.subresourceRange.levelCount = 1;
			CreateInfo.subresourceRange.baseArrayLayer = 0;
			CreateInfo.subresourceRange.layerCount = 1;

			if(vkCreateImageView(m_VKDevice, &CreateInfo, nullptr, &m_vSwapChainImageViewList[i]) != VK_SUCCESS)
			{
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Could not create image views for the swap chain framebuffers.");
				return false;
			}
		}

		return true;
	}

	void DestroyImageViews()
	{
		for(auto &ImageView : m_vSwapChainImageViewList)
		{
			vkDestroyImageView(m_VKDevice, ImageView, nullptr);
		}

		m_vSwapChainImageViewList.clear();
	}

	[[nodiscard]] bool CreateMultiSamplerImageAttachments()
	{
		m_vSwapChainMultiSamplingImages.resize(m_SwapChainImageCount);
		if(HasMultiSampling())
		{
			for(size_t i = 0; i < m_SwapChainImageCount; ++i)
			{
				if(!CreateImage(m_VKSwapImgAndViewportExtent.m_SwapImageViewport.width, m_VKSwapImgAndViewportExtent.m_SwapImageViewport.height, 1, 1, m_VKSurfFormat.format, VK_IMAGE_TILING_OPTIMAL, m_vSwapChainMultiSamplingImages[i].m_Image, m_vSwapChainMultiSamplingImages[i].m_ImgMem, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
					return false;
				m_vSwapChainMultiSamplingImages[i].m_ImgView = CreateImageView(m_vSwapChainMultiSamplingImages[i].m_Image, m_VKSurfFormat.format, VK_IMAGE_VIEW_TYPE_2D, 1, 1);
			}
		}

		return true;
	}

	void DestroyMultiSamplerImageAttachments()
	{
		if(HasMultiSampling())
		{
			m_vSwapChainMultiSamplingImages.resize(m_SwapChainImageCount);
			for(size_t i = 0; i < m_SwapChainImageCount; ++i)
			{
				vkDestroyImage(m_VKDevice, m_vSwapChainMultiSamplingImages[i].m_Image, nullptr);
				vkDestroyImageView(m_VKDevice, m_vSwapChainMultiSamplingImages[i].m_ImgView, nullptr);
				FreeImageMemBlock(m_vSwapChainMultiSamplingImages[i].m_ImgMem);
			}
		}
		m_vSwapChainMultiSamplingImages.clear();
	}

	[[nodiscard]] VkFormat RenderTargetReadbackFormat() const
	{
		return VK_FORMAT_R8G8B8A8_UNORM;
	}

	[[nodiscard]] bool CreateRenderPass(VkRenderPass &RenderPass, bool ClearAttachments, bool LoadAttachments = false, VkImageLayout FinalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VkImageLayout InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED, bool ForceSingleSample = false, VkFormat AttachmentFormat = VK_FORMAT_UNDEFINED)
	{
		if(AttachmentFormat == VK_FORMAT_UNDEFINED)
			AttachmentFormat = m_VKSurfFormat.format;

		bool HasMultiSamplingTargets = HasMultiSampling() && !ForceSingleSample;
		VkAttachmentDescription MultiSamplingColorAttachment{};
		MultiSamplingColorAttachment.format = AttachmentFormat;
		MultiSamplingColorAttachment.samples = HasMultiSamplingTargets ? GetSampleCount() : VK_SAMPLE_COUNT_1_BIT;
		MultiSamplingColorAttachment.loadOp = LoadAttachments ? VK_ATTACHMENT_LOAD_OP_LOAD : (ClearAttachments ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE);
		MultiSamplingColorAttachment.storeOp = HasMultiSamplingTargets ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
		MultiSamplingColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		MultiSamplingColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		MultiSamplingColorAttachment.initialLayout = HasMultiSamplingTargets && LoadAttachments ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : InitialLayout;
		MultiSamplingColorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentDescription ColorAttachment{};
		ColorAttachment.format = AttachmentFormat;
		ColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		ColorAttachment.loadOp = LoadAttachments ? VK_ATTACHMENT_LOAD_OP_LOAD : (ClearAttachments && !HasMultiSamplingTargets ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE);
		ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		ColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		ColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		ColorAttachment.initialLayout = InitialLayout;
		ColorAttachment.finalLayout = FinalLayout;

		VkAttachmentReference MultiSamplingColorAttachmentRef{};
		MultiSamplingColorAttachmentRef.attachment = 0;
		MultiSamplingColorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference ColorAttachmentRef{};
		ColorAttachmentRef.attachment = HasMultiSamplingTargets ? 1 : 0;
		ColorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription Subpass{};
		Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		Subpass.colorAttachmentCount = 1;
		Subpass.pColorAttachments = HasMultiSamplingTargets ? &MultiSamplingColorAttachmentRef : &ColorAttachmentRef;
		Subpass.pResolveAttachments = HasMultiSamplingTargets ? &ColorAttachmentRef : nullptr;

		std::array<VkAttachmentDescription, 2> aAttachments;
		aAttachments[0] = MultiSamplingColorAttachment;
		aAttachments[1] = ColorAttachment;

		std::array<VkSubpassDependency, 2> aDependencies{};
		aDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		aDependencies[0].dstSubpass = 0;
		aDependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		aDependencies[0].srcAccessMask = 0;
		aDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		aDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		if(LoadAttachments)
		{
			aDependencies[0].srcStageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
			aDependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
			aDependencies[0].dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
		}
		aDependencies[1].srcSubpass = 0;
		aDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		aDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		aDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		aDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		aDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		VkRenderPassCreateInfo CreateRenderPassInfo{};
		CreateRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		CreateRenderPassInfo.attachmentCount = HasMultiSamplingTargets ? 2 : 1;
		CreateRenderPassInfo.pAttachments = HasMultiSamplingTargets ? aAttachments.data() : aAttachments.data() + 1;
		CreateRenderPassInfo.subpassCount = 1;
		CreateRenderPassInfo.pSubpasses = &Subpass;
		CreateRenderPassInfo.dependencyCount = FinalLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? 2 : 1;
		CreateRenderPassInfo.pDependencies = aDependencies.data();

		if(vkCreateRenderPass(m_VKDevice, &CreateRenderPassInfo, nullptr, &RenderPass) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating the render pass failed.");
			return false;
		}

		return true;
	}

	void DestroyRenderPass()
	{
		if(m_VKRenderPass != VK_NULL_HANDLE)
			vkDestroyRenderPass(m_VKDevice, m_VKRenderPass, nullptr);
		if(m_VKRenderPassLoad != VK_NULL_HANDLE)
			vkDestroyRenderPass(m_VKDevice, m_VKRenderPassLoad, nullptr);
		if(m_VKRenderTargetRenderPass != VK_NULL_HANDLE)
			vkDestroyRenderPass(m_VKDevice, m_VKRenderTargetRenderPass, nullptr);
		m_VKRenderPass = VK_NULL_HANDLE;
		m_VKRenderPassLoad = VK_NULL_HANDLE;
		m_VKRenderTargetRenderPass = VK_NULL_HANDLE;
	}

	[[nodiscard]] bool CreateFramebuffers()
	{
		m_vFramebufferList.resize(m_SwapChainImageCount);

		for(size_t i = 0; i < m_SwapChainImageCount; i++)
		{
			std::array<VkImageView, 2> aAttachments = {
				m_vSwapChainMultiSamplingImages[i].m_ImgView,
				m_vSwapChainImageViewList[i]};

			bool HasMultiSamplingTargets = HasMultiSampling();

			VkFramebufferCreateInfo FramebufferInfo{};
			FramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			FramebufferInfo.renderPass = m_VKRenderPass;
			FramebufferInfo.attachmentCount = HasMultiSamplingTargets ? aAttachments.size() : aAttachments.size() - 1;
			FramebufferInfo.pAttachments = HasMultiSamplingTargets ? aAttachments.data() : aAttachments.data() + 1;
			FramebufferInfo.width = m_VKSwapImgAndViewportExtent.m_SwapImageViewport.width;
			FramebufferInfo.height = m_VKSwapImgAndViewportExtent.m_SwapImageViewport.height;
			FramebufferInfo.layers = 1;

			if(vkCreateFramebuffer(m_VKDevice, &FramebufferInfo, nullptr, &m_vFramebufferList[i]) != VK_SUCCESS)
			{
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating the framebuffers failed.");
				return false;
			}
		}

		return true;
	}

	void DestroyFramebuffers()
	{
		for(auto &FrameBuffer : m_vFramebufferList)
		{
			vkDestroyFramebuffer(m_VKDevice, FrameBuffer, nullptr);
		}

		m_vFramebufferList.clear();
	}

	[[nodiscard]] bool CreateShaderModule(const std::vector<uint8_t> &vCode, VkShaderModule &ShaderModule, bool ReportError = true)
	{
		VkShaderModuleCreateInfo CreateInfo{};
		CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		CreateInfo.codeSize = vCode.size();
		CreateInfo.pCode = (const uint32_t *)(vCode.data());

		if(vkCreateShaderModule(m_VKDevice, &CreateInfo, nullptr, &ShaderModule) != VK_SUCCESS)
		{
			if(ReportError)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Shader module was not created.");
			return false;
		}

		return true;
	}

	[[nodiscard]] bool CreateDescriptorSetLayouts()
	{
		VkDescriptorSetLayoutBinding SamplerLayoutBinding{};
		SamplerLayoutBinding.binding = 0;
		SamplerLayoutBinding.descriptorCount = 1;
		SamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		SamplerLayoutBinding.pImmutableSamplers = nullptr;
		SamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		std::array<VkDescriptorSetLayoutBinding, 1> aBindings = {SamplerLayoutBinding};
		VkDescriptorSetLayoutCreateInfo LayoutInfo{};
		LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		LayoutInfo.bindingCount = aBindings.size();
		LayoutInfo.pBindings = aBindings.data();

		if(vkCreateDescriptorSetLayout(m_VKDevice, &LayoutInfo, nullptr, &m_StandardTexturedDescriptorSetLayout) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating descriptor layout failed.");
			return false;
		}

		if(vkCreateDescriptorSetLayout(m_VKDevice, &LayoutInfo, nullptr, &m_Standard3DTexturedDescriptorSetLayout) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating descriptor layout failed.");
			return false;
		}
		return true;
	}

	void DestroyDescriptorSetLayouts()
	{
		vkDestroyDescriptorSetLayout(m_VKDevice, m_StandardTexturedDescriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(m_VKDevice, m_Standard3DTexturedDescriptorSetLayout, nullptr);
	}

	[[nodiscard]] bool LoadShader(const char *pFilename, std::vector<uint8_t> *&pvShaderData)
	{
		auto ShaderFileIterator = m_ShaderFiles.find(pFilename);
		if(ShaderFileIterator == m_ShaderFiles.end())
		{
			void *pShaderBuff;
			unsigned FileSize;
			if(!m_pStorage->ReadFile(pFilename, IStorage::TYPE_ALL, &pShaderBuff, &FileSize))
				return false;

			std::vector<uint8_t> vShaderBuff;
			vShaderBuff.resize(FileSize);
			mem_copy(vShaderBuff.data(), pShaderBuff, FileSize);
			free(pShaderBuff);

			ShaderFileIterator = m_ShaderFiles.insert({pFilename, {std::move(vShaderBuff)}}).first;
		}

		pvShaderData = &ShaderFileIterator->second.m_vBinary;

		return true;
	}

	[[nodiscard]] bool CreateShaders(const char *pVertName, const char *pFragName, VkPipelineShaderStageCreateInfo (&aShaderStages)[2], SShaderModule &ShaderModule, bool ReportError = true)
	{
		bool ShaderLoaded = true;

		std::vector<uint8_t> *pvVertBuff;
		std::vector<uint8_t> *pvFragBuff;
		ShaderLoaded &= LoadShader(pVertName, pvVertBuff);
		ShaderLoaded &= LoadShader(pFragName, pvFragBuff);

		ShaderModule.m_VKDevice = m_VKDevice;

		if(!ShaderLoaded)
		{
			if(ReportError)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "A shader file could not load correctly.");
			return false;
		}

		if(!CreateShaderModule(*pvVertBuff, ShaderModule.m_VertShaderModule, ReportError))
			return false;

		if(!CreateShaderModule(*pvFragBuff, ShaderModule.m_FragShaderModule, ReportError))
			return false;

		VkPipelineShaderStageCreateInfo &VertShaderStageInfo = aShaderStages[0];
		VertShaderStageInfo = {};
		VertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		VertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
		VertShaderStageInfo.module = ShaderModule.m_VertShaderModule;
		VertShaderStageInfo.pName = "main";

		VkPipelineShaderStageCreateInfo &FragShaderStageInfo = aShaderStages[1];
		FragShaderStageInfo = {};
		FragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		FragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		FragShaderStageInfo.module = ShaderModule.m_FragShaderModule;
		FragShaderStageInfo.pName = "main";
		return true;
	}

	bool GetStandardPipelineInfo(VkPipelineInputAssemblyStateCreateInfo &InputAssembly,
		VkViewport &Viewport,
		VkRect2D &Scissor,
		VkPipelineViewportStateCreateInfo &ViewportState,
		VkPipelineRasterizationStateCreateInfo &Rasterizer,
		VkPipelineMultisampleStateCreateInfo &Multisampling,
		VkPipelineColorBlendAttachmentState &ColorBlendAttachment,
		VkPipelineColorBlendStateCreateInfo &ColorBlending) const
	{
		InputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		InputAssembly.primitiveRestartEnable = VK_FALSE;

		Viewport.x = 0.0f;
		Viewport.y = 0.0f;
		Viewport.width = (float)m_VKSwapImgAndViewportExtent.m_SwapImageViewport.width;
		Viewport.height = (float)m_VKSwapImgAndViewportExtent.m_SwapImageViewport.height;
		Viewport.minDepth = 0.0f;
		Viewport.maxDepth = 1.0f;

		Scissor.offset = {0, 0};
		Scissor.extent = m_VKSwapImgAndViewportExtent.m_SwapImageViewport;

		ViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		ViewportState.viewportCount = 1;
		ViewportState.pViewports = &Viewport;
		ViewportState.scissorCount = 1;
		ViewportState.pScissors = &Scissor;

		Rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		Rasterizer.depthClampEnable = VK_FALSE;
		Rasterizer.rasterizerDiscardEnable = VK_FALSE;
		Rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		Rasterizer.lineWidth = 1.0f;
		Rasterizer.cullMode = VK_CULL_MODE_NONE;
		Rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
		Rasterizer.depthBiasEnable = VK_FALSE;

		Multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		Multisampling.sampleShadingEnable = VK_FALSE;
		Multisampling.rasterizationSamples = GetSampleCount();

		ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		ColorBlendAttachment.blendEnable = VK_TRUE;

		ColorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		ColorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		ColorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		ColorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		ColorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		ColorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

		ColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		ColorBlending.logicOpEnable = VK_FALSE;
		ColorBlending.logicOp = VK_LOGIC_OP_COPY;
		ColorBlending.attachmentCount = 1;
		ColorBlending.pAttachments = &ColorBlendAttachment;
		ColorBlending.blendConstants[0] = 0.0f;
		ColorBlending.blendConstants[1] = 0.0f;
		ColorBlending.blendConstants[2] = 0.0f;
		ColorBlending.blendConstants[3] = 0.0f;

		return true;
	}

	template<bool ForceRequireDescriptors, size_t ArraySize, size_t DescrArraySize, size_t PushArraySize>
	[[nodiscard]] bool CreateGraphicsPipeline(const char *pVertName, const char *pFragName, SPipelineContainer &PipeContainer, uint32_t Stride, std::array<VkVertexInputAttributeDescription, ArraySize> &aInputAttr,
		std::array<VkDescriptorSetLayout, DescrArraySize> &aSetLayouts, std::array<VkPushConstantRange, PushArraySize> &aPushConstants, EVulkanBackendTextureModes TexMode,
		EVulkanBackendBlendModes BlendMode, EVulkanBackendClipModes DynamicMode, bool IsLinePrim = false, VkRenderPass RenderPass = VK_NULL_HANDLE,
		VkSampleCountFlagBits SampleCount = VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM, bool EnableBlending = true, bool ReportError = true)
	{
		VkPipelineShaderStageCreateInfo aShaderStages[2];
		SShaderModule Module;
		if(!CreateShaders(pVertName, pFragName, aShaderStages, Module, ReportError))
			return false;

		bool HasSampler = TexMode == VULKAN_BACKEND_TEXTURE_MODE_TEXTURED;

		VkPipelineVertexInputStateCreateInfo VertexInputInfo{};
		VertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		VkVertexInputBindingDescription BindingDescription{};
		BindingDescription.binding = 0;
		BindingDescription.stride = Stride;
		BindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VertexInputInfo.vertexBindingDescriptionCount = 1;
		VertexInputInfo.vertexAttributeDescriptionCount = aInputAttr.size();
		VertexInputInfo.pVertexBindingDescriptions = &BindingDescription;
		VertexInputInfo.pVertexAttributeDescriptions = aInputAttr.data();

		VkPipelineInputAssemblyStateCreateInfo InputAssembly{};
		VkViewport Viewport{};
		VkRect2D Scissor{};
		VkPipelineViewportStateCreateInfo ViewportState{};
		VkPipelineRasterizationStateCreateInfo Rasterizer{};
		VkPipelineMultisampleStateCreateInfo Multisampling{};
		VkPipelineColorBlendAttachmentState ColorBlendAttachment{};
		VkPipelineColorBlendStateCreateInfo ColorBlending{};

		GetStandardPipelineInfo(InputAssembly, Viewport, Scissor, ViewportState, Rasterizer, Multisampling, ColorBlendAttachment, ColorBlending);
		InputAssembly.topology = IsLinePrim ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		if(SampleCount != VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM)
			Multisampling.rasterizationSamples = SampleCount;
		if(!EnableBlending)
			ColorBlendAttachment.blendEnable = VK_FALSE;

		VkPipelineLayoutCreateInfo PipelineLayoutInfo{};
		PipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		PipelineLayoutInfo.setLayoutCount = (HasSampler || ForceRequireDescriptors) ? aSetLayouts.size() : 0;
		PipelineLayoutInfo.pSetLayouts = (HasSampler || ForceRequireDescriptors) && !aSetLayouts.empty() ? aSetLayouts.data() : nullptr;

		PipelineLayoutInfo.pushConstantRangeCount = aPushConstants.size();
		PipelineLayoutInfo.pPushConstantRanges = !aPushConstants.empty() ? aPushConstants.data() : nullptr;

		VkPipelineLayout &PipeLayout = GetPipeLayout(PipeContainer, HasSampler, size_t(BlendMode), size_t(DynamicMode));
		VkPipeline &Pipeline = GetPipeline(PipeContainer, HasSampler, size_t(BlendMode), size_t(DynamicMode));

		if(vkCreatePipelineLayout(m_VKDevice, &PipelineLayoutInfo, nullptr, &PipeLayout) != VK_SUCCESS)
		{
			if(ReportError)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating pipeline layout failed.");
			return false;
		}

		VkGraphicsPipelineCreateInfo PipelineInfo{};
		PipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		PipelineInfo.stageCount = 2;
		PipelineInfo.pStages = aShaderStages;
		PipelineInfo.pVertexInputState = &VertexInputInfo;
		PipelineInfo.pInputAssemblyState = &InputAssembly;
		PipelineInfo.pViewportState = &ViewportState;
		PipelineInfo.pRasterizationState = &Rasterizer;
		PipelineInfo.pMultisampleState = &Multisampling;
		PipelineInfo.pColorBlendState = &ColorBlending;
		PipelineInfo.layout = PipeLayout;
		PipelineInfo.renderPass = RenderPass != VK_NULL_HANDLE ? RenderPass : m_VKRenderPass;
		PipelineInfo.subpass = 0;
		PipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

		std::array<VkDynamicState, 2> aDynamicStates = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		};

		VkPipelineDynamicStateCreateInfo DynamicStateCreate{};
		DynamicStateCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		DynamicStateCreate.dynamicStateCount = aDynamicStates.size();
		DynamicStateCreate.pDynamicStates = aDynamicStates.data();

		if(DynamicMode == VULKAN_BACKEND_CLIP_MODE_DYNAMIC_SCISSOR_AND_VIEWPORT)
		{
			PipelineInfo.pDynamicState = &DynamicStateCreate;
		}

		if(vkCreateGraphicsPipelines(m_VKDevice, VK_NULL_HANDLE, 1, &PipelineInfo, nullptr, &Pipeline) != VK_SUCCESS)
		{
			if(ReportError)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating the graphic pipeline failed.");
			return false;
		}

		return true;
	}

	[[nodiscard]] bool CreateStandardGraphicsPipelineImpl(const char *pVertName, const char *pFragName, SPipelineContainer &PipeContainer, EVulkanBackendTextureModes TexMode, EVulkanBackendBlendModes BlendMode, EVulkanBackendClipModes DynamicMode, bool IsLinePrim)
	{
		std::array<VkVertexInputAttributeDescription, 3> aAttributeDescriptions = {};

		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
		aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
		aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * (2 + 2)};

		std::array<VkDescriptorSetLayout, 1> aSetLayouts = {m_StandardTexturedDescriptorSetLayout};

		std::array<VkPushConstantRange, 1> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos)};

		return CreateGraphicsPipeline<false>(pVertName, pFragName, PipeContainer, sizeof(float) * (2 + 2) + sizeof(uint8_t) * 4, aAttributeDescriptions, aSetLayouts, aPushConstants, TexMode, BlendMode, DynamicMode, IsLinePrim);
	}

	[[nodiscard]] bool CreateStandardGraphicsPipeline(const char *pVertName, const char *pFragName, bool HasSampler, bool IsLinePipe)
	{
		bool Ret = true;

		EVulkanBackendTextureModes TexMode = HasSampler ? VULKAN_BACKEND_TEXTURE_MODE_TEXTURED : VULKAN_BACKEND_TEXTURE_MODE_NOT_TEXTURED;

		for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
		{
			for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
			{
				Ret &= CreateStandardGraphicsPipelineImpl(pVertName, pFragName, IsLinePipe ? m_StandardLinePipeline : m_StandardPipeline, TexMode, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j), IsLinePipe);
			}
		}

		return Ret;
	}

	[[nodiscard]] bool CreateStandard3DGraphicsPipelineImpl(const char *pVertName, const char *pFragName, SPipelineContainer &PipeContainer, EVulkanBackendTextureModes TexMode, EVulkanBackendBlendModes BlendMode, EVulkanBackendClipModes DynamicMode)
	{
		std::array<VkVertexInputAttributeDescription, 3> aAttributeDescriptions = {};

		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
		aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * 2};
		aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 2 + sizeof(uint8_t) * 4};

		std::array<VkDescriptorSetLayout, 1> aSetLayouts = {m_Standard3DTexturedDescriptorSetLayout};

		std::array<VkPushConstantRange, 1> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos)};

		return CreateGraphicsPipeline<false>(pVertName, pFragName, PipeContainer, sizeof(float) * 2 + sizeof(uint8_t) * 4 + sizeof(float) * 3, aAttributeDescriptions, aSetLayouts, aPushConstants, TexMode, BlendMode, DynamicMode);
	}

	[[nodiscard]] bool CreateStandard3DGraphicsPipeline(const char *pVertName, const char *pFragName, bool HasSampler)
	{
		bool Ret = true;

		EVulkanBackendTextureModes TexMode = HasSampler ? VULKAN_BACKEND_TEXTURE_MODE_TEXTURED : VULKAN_BACKEND_TEXTURE_MODE_NOT_TEXTURED;

		for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
		{
			for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
			{
				Ret &= CreateStandard3DGraphicsPipelineImpl(pVertName, pFragName, m_Standard3DPipeline, TexMode, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j));
			}
		}

		return Ret;
	}

	[[nodiscard]] bool CreateTextDescriptorSetLayout()
	{
		VkDescriptorSetLayoutBinding SamplerLayoutBinding{};
		SamplerLayoutBinding.binding = 0;
		SamplerLayoutBinding.descriptorCount = 1;
		SamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		SamplerLayoutBinding.pImmutableSamplers = nullptr;
		SamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		auto SamplerLayoutBinding2 = SamplerLayoutBinding;
		SamplerLayoutBinding2.binding = 1;

		std::array<VkDescriptorSetLayoutBinding, 2> aBindings = {SamplerLayoutBinding, SamplerLayoutBinding2};
		VkDescriptorSetLayoutCreateInfo LayoutInfo{};
		LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		LayoutInfo.bindingCount = aBindings.size();
		LayoutInfo.pBindings = aBindings.data();

		if(vkCreateDescriptorSetLayout(m_VKDevice, &LayoutInfo, nullptr, &m_TextDescriptorSetLayout) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating descriptor layout failed.");
			return false;
		}

		return true;
	}

	void DestroyTextDescriptorSetLayout()
	{
		vkDestroyDescriptorSetLayout(m_VKDevice, m_TextDescriptorSetLayout, nullptr);
	}

	[[nodiscard]] bool CreateTextGraphicsPipelineImpl(const char *pVertName, const char *pFragName, SPipelineContainer &PipeContainer, EVulkanBackendTextureModes TexMode, EVulkanBackendBlendModes BlendMode, EVulkanBackendClipModes DynamicMode)
	{
		std::array<VkVertexInputAttributeDescription, 3> aAttributeDescriptions = {};
		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
		aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
		aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * (2 + 2)};

		std::array<VkDescriptorSetLayout, 1> aSetLayouts = {m_TextDescriptorSetLayout};

		std::array<VkPushConstantRange, 2> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGTextPos)};
		aPushConstants[1] = {VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(SUniformGTextPos) + sizeof(SUniformTextGFragmentOffset), sizeof(SUniformTextGFragmentConstants)};

		return CreateGraphicsPipeline<false>(pVertName, pFragName, PipeContainer, sizeof(float) * (2 + 2) + sizeof(uint8_t) * 4, aAttributeDescriptions, aSetLayouts, aPushConstants, TexMode, BlendMode, DynamicMode);
	}

	[[nodiscard]] bool CreateTextGraphicsPipeline(const char *pVertName, const char *pFragName)
	{
		bool Ret = true;

		EVulkanBackendTextureModes TexMode = VULKAN_BACKEND_TEXTURE_MODE_TEXTURED;

		for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
		{
			for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
			{
				Ret &= CreateTextGraphicsPipelineImpl(pVertName, pFragName, m_TextPipeline, TexMode, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j));
			}
		}

		return Ret;
	}

	template<bool HasSampler>
	[[nodiscard]] bool CreateTileGraphicsPipelineImpl(const char *pVertName, const char *pFragName, bool IsBorder, SPipelineContainer &PipeContainer, EVulkanBackendTextureModes TexMode, EVulkanBackendBlendModes BlendMode, EVulkanBackendClipModes DynamicMode)
	{
		std::array<VkVertexInputAttributeDescription, HasSampler ? 2 : 1> aAttributeDescriptions = {};
		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
		if(HasSampler)
			aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R8G8B8A8_UINT, sizeof(float) * 2};

		std::array<VkDescriptorSetLayout, 1> aSetLayouts;
		aSetLayouts[0] = m_Standard3DTexturedDescriptorSetLayout;

		uint32_t VertPushConstantSize = sizeof(SUniformTileGPos);
		if(IsBorder)
			VertPushConstantSize = sizeof(SUniformTileGPosBorder);

		uint32_t FragPushConstantSize = sizeof(SUniformTileGVertColor);

		std::array<VkPushConstantRange, 2> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, VertPushConstantSize};
		aPushConstants[1] = {VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(SUniformTileGPosBorder) + sizeof(SUniformTileGVertColorAlign), FragPushConstantSize};

		return CreateGraphicsPipeline<false>(pVertName, pFragName, PipeContainer, HasSampler ? (sizeof(float) * 2 + sizeof(uint8_t) * 4) : (sizeof(float) * 2), aAttributeDescriptions, aSetLayouts, aPushConstants, TexMode, BlendMode, DynamicMode);
	}

	template<bool HasSampler>
	[[nodiscard]] bool CreateTileGraphicsPipeline(const char *pVertName, const char *pFragName, bool IsBorder)
	{
		bool Ret = true;

		EVulkanBackendTextureModes TexMode = HasSampler ? VULKAN_BACKEND_TEXTURE_MODE_TEXTURED : VULKAN_BACKEND_TEXTURE_MODE_NOT_TEXTURED;

		for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
		{
			for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
			{
				Ret &= CreateTileGraphicsPipelineImpl<HasSampler>(pVertName, pFragName, IsBorder, !IsBorder ? m_TilePipeline : m_TileBorderPipeline, TexMode, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j));
			}
		}

		return Ret;
	}

	[[nodiscard]] bool CreatePrimExGraphicsPipelineImpl(const char *pVertName, const char *pFragName, bool Rotationless, SPipelineContainer &PipeContainer, EVulkanBackendTextureModes TexMode, EVulkanBackendBlendModes BlendMode, EVulkanBackendClipModes DynamicMode)
	{
		std::array<VkVertexInputAttributeDescription, 3> aAttributeDescriptions = {};
		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
		aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
		aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * (2 + 2)};

		std::array<VkDescriptorSetLayout, 1> aSetLayouts;
		aSetLayouts[0] = m_StandardTexturedDescriptorSetLayout;
		uint32_t VertPushConstantSize = sizeof(SUniformPrimExGPos);
		if(Rotationless)
			VertPushConstantSize = sizeof(SUniformPrimExGPosRotationless);

		uint32_t FragPushConstantSize = sizeof(SUniformPrimExGVertColor);

		std::array<VkPushConstantRange, 2> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, VertPushConstantSize};
		aPushConstants[1] = {VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(SUniformPrimExGPos) + sizeof(SUniformPrimExGVertColorAlign), FragPushConstantSize};

		return CreateGraphicsPipeline<false>(pVertName, pFragName, PipeContainer, sizeof(float) * (2 + 2) + sizeof(uint8_t) * 4, aAttributeDescriptions, aSetLayouts, aPushConstants, TexMode, BlendMode, DynamicMode);
	}

	[[nodiscard]] bool CreatePrimExGraphicsPipeline(const char *pVertName, const char *pFragName, bool HasSampler, bool Rotationless)
	{
		bool Ret = true;

		EVulkanBackendTextureModes TexMode = HasSampler ? VULKAN_BACKEND_TEXTURE_MODE_TEXTURED : VULKAN_BACKEND_TEXTURE_MODE_NOT_TEXTURED;

		for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
		{
			for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
			{
				Ret &= CreatePrimExGraphicsPipelineImpl(pVertName, pFragName, Rotationless, Rotationless ? m_PrimExRotationlessPipeline : m_PrimExPipeline, TexMode, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j));
			}
		}

		return Ret;
	}

	[[nodiscard]] bool CreateUniformDescriptorSetLayout(VkDescriptorSetLayout &SetLayout, VkShaderStageFlags StageFlags)
	{
		VkDescriptorSetLayoutBinding SamplerLayoutBinding{};
		SamplerLayoutBinding.binding = 1;
		SamplerLayoutBinding.descriptorCount = 1;
		SamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		SamplerLayoutBinding.pImmutableSamplers = nullptr;
		SamplerLayoutBinding.stageFlags = StageFlags;

		std::array<VkDescriptorSetLayoutBinding, 1> aBindings = {SamplerLayoutBinding};
		VkDescriptorSetLayoutCreateInfo LayoutInfo{};
		LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		LayoutInfo.bindingCount = aBindings.size();
		LayoutInfo.pBindings = aBindings.data();

		if(vkCreateDescriptorSetLayout(m_VKDevice, &LayoutInfo, nullptr, &SetLayout) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating descriptor layout failed.");
			return false;
		}
		return true;
	}

	[[nodiscard]] bool CreateSpriteMultiUniformDescriptorSetLayout()
	{
		return CreateUniformDescriptorSetLayout(m_SpriteMultiUniformDescriptorSetLayout, VK_SHADER_STAGE_VERTEX_BIT);
	}

	[[nodiscard]] bool CreateQuadUniformDescriptorSetLayout()
	{
		return CreateUniformDescriptorSetLayout(m_QuadUniformDescriptorSetLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	void DestroyUniformDescriptorSetLayouts()
	{
		vkDestroyDescriptorSetLayout(m_VKDevice, m_QuadUniformDescriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(m_VKDevice, m_SpriteMultiUniformDescriptorSetLayout, nullptr);
	}

	[[nodiscard]] bool CreateUniformDescriptorSets(size_t RenderThreadIndex, VkDescriptorSetLayout &SetLayout, SDeviceDescriptorSet *pSets, size_t SetCount, VkBuffer BindBuffer, size_t SingleBufferInstanceSize, VkDeviceSize MemoryOffset)
	{
		VkDescriptorPool RetDescr;
		if(!GetDescriptorPoolForAlloc(RetDescr, m_vUniformBufferDescrPools[RenderThreadIndex], pSets, SetCount, &DrawProfileStats(RenderThreadIndex)))
			return false;
		VkDescriptorSetAllocateInfo DesAllocInfo{};
		DesAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		DesAllocInfo.descriptorSetCount = 1;
		DesAllocInfo.pSetLayouts = &SetLayout;
		for(size_t i = 0; i < SetCount; ++i)
		{
			DesAllocInfo.descriptorPool = pSets[i].m_pPools->m_vPools[pSets[i].m_PoolIndex].m_Pool;
			if(vkAllocateDescriptorSets(m_VKDevice, &DesAllocInfo, &pSets[i].m_Descriptor) != VK_SUCCESS)
			{
				for(size_t FreeIndex = 0; FreeIndex < SetCount; ++FreeIndex)
					FreeDescriptorSetFromPool(pSets[FreeIndex]);
				return false;
			}
			DrawProfileStats(RenderThreadIndex).m_DescriptorAllocations++;

			VkDescriptorBufferInfo BufferInfo{};
			BufferInfo.buffer = BindBuffer;
			BufferInfo.offset = MemoryOffset + SingleBufferInstanceSize * i;
			BufferInfo.range = SingleBufferInstanceSize;

			std::array<VkWriteDescriptorSet, 1> aDescriptorWrites{};

			aDescriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			aDescriptorWrites[0].dstSet = pSets[i].m_Descriptor;
			aDescriptorWrites[0].dstBinding = 1;
			aDescriptorWrites[0].dstArrayElement = 0;
			aDescriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			aDescriptorWrites[0].descriptorCount = 1;
			aDescriptorWrites[0].pBufferInfo = &BufferInfo;

			vkUpdateDescriptorSets(m_VKDevice, static_cast<uint32_t>(aDescriptorWrites.size()), aDescriptorWrites.data(), 0, nullptr);
			DrawProfileStats(RenderThreadIndex).m_DescriptorUpdates += aDescriptorWrites.size();
		}

		return true;
	}

	void DestroyUniformDescriptorSets(SDeviceDescriptorSet *pSets, size_t SetCount)
	{
		for(size_t i = 0; i < SetCount; ++i)
			FreeDescriptorSetFromPool(pSets[i]);
	}

	[[nodiscard]] bool CreateSpriteMultiGraphicsPipelineImpl(const char *pVertName, const char *pFragName, SPipelineContainer &PipeContainer, EVulkanBackendTextureModes TexMode, EVulkanBackendBlendModes BlendMode, EVulkanBackendClipModes DynamicMode)
	{
		std::array<VkVertexInputAttributeDescription, 3> aAttributeDescriptions = {};
		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
		aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
		aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * (2 + 2)};

		std::array<VkDescriptorSetLayout, 2> aSetLayouts;
		aSetLayouts[0] = m_StandardTexturedDescriptorSetLayout;
		aSetLayouts[1] = m_SpriteMultiUniformDescriptorSetLayout;

		uint32_t VertPushConstantSize = sizeof(SUniformSpriteMultiGPos);
		uint32_t FragPushConstantSize = sizeof(SUniformSpriteMultiGVertColor);

		std::array<VkPushConstantRange, 2> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, VertPushConstantSize};
		aPushConstants[1] = {VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(SUniformSpriteMultiGPos) + sizeof(SUniformSpriteMultiGVertColorAlign), FragPushConstantSize};

		return CreateGraphicsPipeline<false>(pVertName, pFragName, PipeContainer, sizeof(float) * (2 + 2) + sizeof(uint8_t) * 4, aAttributeDescriptions, aSetLayouts, aPushConstants, TexMode, BlendMode, DynamicMode);
	}

	[[nodiscard]] bool CreateSpriteMultiGraphicsPipeline(const char *pVertName, const char *pFragName)
	{
		bool Ret = true;

		EVulkanBackendTextureModes TexMode = VULKAN_BACKEND_TEXTURE_MODE_TEXTURED;

		for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
		{
			for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
			{
				Ret &= CreateSpriteMultiGraphicsPipelineImpl(pVertName, pFragName, m_SpriteMultiPipeline, TexMode, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j));
			}
		}

		return Ret;
	}

	[[nodiscard]] bool CreateSpriteMultiPushGraphicsPipelineImpl(const char *pVertName, const char *pFragName, SPipelineContainer &PipeContainer, EVulkanBackendTextureModes TexMode, EVulkanBackendBlendModes BlendMode, EVulkanBackendClipModes DynamicMode)
	{
		std::array<VkVertexInputAttributeDescription, 3> aAttributeDescriptions = {};
		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
		aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
		aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * (2 + 2)};

		std::array<VkDescriptorSetLayout, 1> aSetLayouts;
		aSetLayouts[0] = m_StandardTexturedDescriptorSetLayout;

		uint32_t VertPushConstantSize = sizeof(SUniformSpriteMultiPushGPos);
		uint32_t FragPushConstantSize = sizeof(SUniformSpriteMultiPushGVertColor);

		std::array<VkPushConstantRange, 2> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, VertPushConstantSize};
		aPushConstants[1] = {VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(SUniformSpriteMultiPushGPos), FragPushConstantSize};

		return CreateGraphicsPipeline<false>(pVertName, pFragName, PipeContainer, sizeof(float) * (2 + 2) + sizeof(uint8_t) * 4, aAttributeDescriptions, aSetLayouts, aPushConstants, TexMode, BlendMode, DynamicMode);
	}

	[[nodiscard]] bool CreateSpriteMultiPushGraphicsPipeline(const char *pVertName, const char *pFragName)
	{
		bool Ret = true;

		EVulkanBackendTextureModes TexMode = VULKAN_BACKEND_TEXTURE_MODE_TEXTURED;

		for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
		{
			for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
			{
				Ret &= CreateSpriteMultiPushGraphicsPipelineImpl(pVertName, pFragName, m_SpriteMultiPushPipeline, TexMode, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j));
			}
		}

		return Ret;
	}

	template<bool IsTextured>
	[[nodiscard]] bool CreateQuadGraphicsPipelineImpl(const char *pVertName, const char *pFragName, SPipelineContainer &PipeContainer, EVulkanBackendTextureModes TexMode, EVulkanBackendBlendModes BlendMode, EVulkanBackendClipModes DynamicMode)
	{
		std::array<VkVertexInputAttributeDescription, IsTextured ? 3 : 2> aAttributeDescriptions = {};
		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
		aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * 4};
		if(IsTextured)
			aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 4 + sizeof(uint8_t) * 4};

		std::array<VkDescriptorSetLayout, IsTextured ? 2 : 1> aSetLayouts;
		if(IsTextured)
		{
			aSetLayouts[0] = m_StandardTexturedDescriptorSetLayout;
			aSetLayouts[1] = m_QuadUniformDescriptorSetLayout;
		}
		else
		{
			aSetLayouts[0] = m_QuadUniformDescriptorSetLayout;
		}

		uint32_t PushConstantSize = sizeof(SUniformQuadGPos);

		std::array<VkPushConstantRange, 1> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, PushConstantSize};

		return CreateGraphicsPipeline<true>(pVertName, pFragName, PipeContainer, sizeof(float) * 4 + sizeof(uint8_t) * 4 + (IsTextured ? (sizeof(float) * 2) : 0), aAttributeDescriptions, aSetLayouts, aPushConstants, TexMode, BlendMode, DynamicMode);
	}

	template<bool HasSampler>
	[[nodiscard]] bool CreateQuadGraphicsPipeline(const char *pVertName, const char *pFragName)
	{
		bool Ret = true;

		EVulkanBackendTextureModes TexMode = HasSampler ? VULKAN_BACKEND_TEXTURE_MODE_TEXTURED : VULKAN_BACKEND_TEXTURE_MODE_NOT_TEXTURED;

		for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
		{
			for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
			{
				Ret &= CreateQuadGraphicsPipelineImpl<HasSampler>(pVertName, pFragName, m_QuadPipeline, TexMode, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j));
			}
		}

		return Ret;
	}

	[[nodiscard]] bool CreateMediaIslandSdfGraphicsPipeline(const char *pVertName, const char *pFragName)
	{
		std::array<VkVertexInputAttributeDescription, 3> aAttributeDescriptions = {};
		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
		aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
		aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * (2 + 2)};

		std::array<VkDescriptorSetLayout, 2> aSetLayouts = {m_StandardTexturedDescriptorSetLayout, m_QuadUniformDescriptorSetLayout};
		std::array<VkPushConstantRange, 1> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos)};

		bool Ret = true;
		for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
		{
			for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
			{
				Ret &= CreateGraphicsPipeline<true>(pVertName, pFragName, m_MediaIslandSdfPipeline, sizeof(CCommandBuffer::SVertex), aAttributeDescriptions, aSetLayouts, aPushConstants, VULKAN_BACKEND_TEXTURE_MODE_TEXTURED, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j));
			}
		}
		return Ret;
	}

	[[nodiscard]] bool CreateRoundedRectSdfGraphicsPipeline(const char *pVertName, const char *pFragName)
	{
		std::array<VkVertexInputAttributeDescription, 3> aAttributeDescriptions = {};
		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
		aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
		aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * (2 + 2)};

		std::array<VkDescriptorSetLayout, 1> aSetLayouts = {m_QuadUniformDescriptorSetLayout};
		std::array<VkPushConstantRange, 1> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos)};

		bool Ret = true;
		for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
		{
			for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
			{
				Ret &= CreateGraphicsPipeline<true>(pVertName, pFragName, m_RoundedRectSdfPipeline, sizeof(CCommandBuffer::SVertex), aAttributeDescriptions, aSetLayouts, aPushConstants, VULKAN_BACKEND_TEXTURE_MODE_NOT_TEXTURED, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j));
			}
		}
		return Ret;
	}

	[[nodiscard]] bool CreateTexturedMsdfGraphicsPipeline(const char *pVertName, const char *pFragName)
	{
		std::array<VkVertexInputAttributeDescription, 3> aAttributeDescriptions = {};
		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
		aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
		aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * (2 + 2)};

		std::array<VkDescriptorSetLayout, 2> aSetLayouts = {m_StandardTexturedDescriptorSetLayout, m_QuadUniformDescriptorSetLayout};
		std::array<VkPushConstantRange, 1> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos)};

		bool Ret = true;
		for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
		{
			for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
			{
				Ret &= CreateGraphicsPipeline<true>(pVertName, pFragName, m_TexturedMsdfPipeline, sizeof(CCommandBuffer::SVertex), aAttributeDescriptions, aSetLayouts, aPushConstants, VULKAN_BACKEND_TEXTURE_MODE_TEXTURED, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j), false, VK_NULL_HANDLE, VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM, true, false);
			}
		}
		return Ret;
	}

	[[nodiscard]] bool CreateGaussianBlurGraphicsPipeline(const char *pVertName, const char *pFragName)
	{
		std::array<VkVertexInputAttributeDescription, 2> aAttributeDescriptions{};
		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
		aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2};
		std::array<VkDescriptorSetLayout, 1> aSetLayouts = {m_StandardTexturedDescriptorSetLayout};
		std::array<VkPushConstantRange, 1> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SUniformGaussianBlur)};
		return CreateGraphicsPipeline<true>(pVertName, pFragName, m_GaussianBlurPipeline, sizeof(CCommandBuffer::SVertex), aAttributeDescriptions, aSetLayouts, aPushConstants,
			VULKAN_BACKEND_TEXTURE_MODE_TEXTURED, VULKAN_BACKEND_BLEND_MODE_NONE, VULKAN_BACKEND_CLIP_MODE_DYNAMIC_SCISSOR_AND_VIEWPORT, false,
			m_VKRenderTargetRenderPass, VK_SAMPLE_COUNT_1_BIT, false);
	}

	template<bool IsTextured>
	[[nodiscard]] bool CreateQuadGroupedGraphicsPipelineImpl(const char *pVertName, const char *pFragName, SPipelineContainer &PipeContainer, EVulkanBackendTextureModes TexMode, EVulkanBackendBlendModes BlendMode, EVulkanBackendClipModes DynamicMode)
	{
		std::array<VkVertexInputAttributeDescription, IsTextured ? 3 : 2> aAttributeDescriptions = {};
		aAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
		aAttributeDescriptions[1] = {1, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * 4};
		if(IsTextured)
			aAttributeDescriptions[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 4 + sizeof(uint8_t) * 4};

		std::array<VkDescriptorSetLayout, 1> aSetLayouts;
		aSetLayouts[0] = m_StandardTexturedDescriptorSetLayout;

		uint32_t PushConstantSize = sizeof(SUniformQuadGroupedGPos);

		std::array<VkPushConstantRange, 1> aPushConstants{};
		aPushConstants[0] = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, PushConstantSize};

		return CreateGraphicsPipeline<false>(pVertName, pFragName, PipeContainer, sizeof(float) * 4 + sizeof(uint8_t) * 4 + (IsTextured ? (sizeof(float) * 2) : 0), aAttributeDescriptions, aSetLayouts, aPushConstants, TexMode, BlendMode, DynamicMode);
	}

	template<bool HasSampler>
	[[nodiscard]] bool CreateQuadGroupedGraphicsPipeline(const char *pVertName, const char *pFragName)
	{
		bool Ret = true;

		EVulkanBackendTextureModes TexMode = HasSampler ? VULKAN_BACKEND_TEXTURE_MODE_TEXTURED : VULKAN_BACKEND_TEXTURE_MODE_NOT_TEXTURED;

		for(size_t i = 0; i < VULKAN_BACKEND_BLEND_MODE_COUNT; ++i)
		{
			for(size_t j = 0; j < VULKAN_BACKEND_CLIP_MODE_COUNT; ++j)
			{
				Ret &= CreateQuadGroupedGraphicsPipelineImpl<HasSampler>(pVertName, pFragName, m_QuadGroupedPipeline, TexMode, EVulkanBackendBlendModes(i), EVulkanBackendClipModes(j));
			}
		}

		return Ret;
	}

	[[nodiscard]] bool CreateCommandPool()
	{
		VkCommandPoolCreateInfo CreatePoolInfo{};
		CreatePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		CreatePoolInfo.queueFamilyIndex = m_VKGraphicsQueueIndex;
		CreatePoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

		m_vCommandPools.resize(m_ThreadCount);
		for(size_t i = 0; i < m_ThreadCount; ++i)
		{
			if(vkCreateCommandPool(m_VKDevice, &CreatePoolInfo, nullptr, &m_vCommandPools[i]) != VK_SUCCESS)
			{
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating the command pool failed.");
				return false;
			}
		}
		return true;
	}

	void DestroyCommandPool()
	{
		for(size_t i = 0; i < m_ThreadCount; ++i)
		{
			vkDestroyCommandPool(m_VKDevice, m_vCommandPools[i], nullptr);
		}
	}

	[[nodiscard]] bool CreateCommandBuffers()
	{
		m_vMainDrawCommandBuffers.resize(m_SwapChainImageCount);
		if(m_ThreadCount > 1)
		{
			m_vvThreadDrawCommandBuffers.resize(m_ThreadCount);
			m_vvUsedThreadDrawCommandBuffer.resize(m_ThreadCount);
			m_vHelperThreadDrawCommandBuffers.resize(m_ThreadCount);
			for(auto &ThreadDrawCommandBuffers : m_vvThreadDrawCommandBuffers)
			{
				ThreadDrawCommandBuffers.resize(m_SwapChainImageCount);
			}
			for(auto &UsedThreadDrawCommandBuffer : m_vvUsedThreadDrawCommandBuffer)
			{
				UsedThreadDrawCommandBuffer.resize(m_SwapChainImageCount, false);
			}
		}
		m_vMemoryCommandBuffers.resize(m_SwapChainImageCount);
		m_vUsedMemoryCommandBuffer.resize(m_SwapChainImageCount, false);

		VkCommandBufferAllocateInfo AllocInfo{};
		AllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		AllocInfo.commandPool = m_vCommandPools[0];
		AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		AllocInfo.commandBufferCount = (uint32_t)m_vMainDrawCommandBuffers.size();

		if(vkAllocateCommandBuffers(m_VKDevice, &AllocInfo, m_vMainDrawCommandBuffers.data()) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Allocating command buffers failed.");
			return false;
		}

		AllocInfo.commandBufferCount = (uint32_t)m_vMemoryCommandBuffers.size();

		if(vkAllocateCommandBuffers(m_VKDevice, &AllocInfo, m_vMemoryCommandBuffers.data()) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Allocating memory command buffers failed.");
			return false;
		}

		if(m_ThreadCount > 1)
		{
			size_t Count = 0;
			for(auto &ThreadDrawCommandBuffers : m_vvThreadDrawCommandBuffers)
			{
				AllocInfo.commandPool = m_vCommandPools[Count];
				++Count;
				AllocInfo.commandBufferCount = (uint32_t)ThreadDrawCommandBuffers.size();
				AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
				if(vkAllocateCommandBuffers(m_VKDevice, &AllocInfo, ThreadDrawCommandBuffers.data()) != VK_SUCCESS)
				{
					SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Allocating thread command buffers failed.");
					return false;
				}
			}
		}

		return true;
	}

	void DestroyCommandBuffer()
	{
		if(m_ThreadCount > 1)
		{
			size_t Count = 0;
			for(auto &ThreadDrawCommandBuffers : m_vvThreadDrawCommandBuffers)
			{
				vkFreeCommandBuffers(m_VKDevice, m_vCommandPools[Count], static_cast<uint32_t>(ThreadDrawCommandBuffers.size()), ThreadDrawCommandBuffers.data());
				++Count;
			}
		}

		vkFreeCommandBuffers(m_VKDevice, m_vCommandPools[0], static_cast<uint32_t>(m_vMemoryCommandBuffers.size()), m_vMemoryCommandBuffers.data());
		vkFreeCommandBuffers(m_VKDevice, m_vCommandPools[0], static_cast<uint32_t>(m_vMainDrawCommandBuffers.size()), m_vMainDrawCommandBuffers.data());

		m_vvThreadDrawCommandBuffers.clear();
		m_vvUsedThreadDrawCommandBuffer.clear();
		m_vHelperThreadDrawCommandBuffers.clear();

		m_vMainDrawCommandBuffers.clear();
		m_vMemoryCommandBuffers.clear();
		m_vUsedMemoryCommandBuffer.clear();
	}

	void CreateFrameTimestampQueries()
	{
		m_vFrameTimestampQueriesPending.clear();
		if(!m_FrameTimestampQueriesSupported)
			return;

		VkQueryPoolCreateInfo QueryPoolInfo{};
		QueryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		QueryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		QueryPoolInfo.queryCount = m_SwapChainImageCount * FRAME_TIMESTAMP_QUERY_COUNT;

		if(vkCreateQueryPool(m_VKDevice, &QueryPoolInfo, nullptr, &m_FrameTimestampQueryPool) != VK_SUCCESS)
		{
			m_FrameTimestampQueriesSupported = false;
			m_FrameTimestampQueryPool = VK_NULL_HANDLE;
			if(IsVerbose())
			{
				dbg_msg("vulkan", "frame timestamp query pool creation failed; gpu frame time profiling disabled.");
			}
			return;
		}

		m_vFrameTimestampQueriesPending.resize(m_SwapChainImageCount, false);
	}

	void DestroyFrameTimestampQueries()
	{
		if(m_FrameTimestampQueryPool != VK_NULL_HANDLE)
		{
			vkDestroyQueryPool(m_VKDevice, m_FrameTimestampQueryPool, nullptr);
			m_FrameTimestampQueryPool = VK_NULL_HANDLE;
		}
		m_vFrameTimestampQueriesPending.clear();
	}

	[[nodiscard]] bool CreateSyncObjects()
	{
		auto SyncObjectCount = m_SwapChainImageCount;
		m_vQueueSubmitSemaphores.resize(SyncObjectCount);
		m_vBusyAcquireImageSemaphores.resize(SyncObjectCount);

		m_vQueueSubmitFences.resize(SyncObjectCount);
		m_vMemoryCommandBufferFences.resize(SyncObjectCount);

		VkSemaphoreCreateInfo CreateSemaphoreInfo{};
		CreateSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo FenceInfo{};
		FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		if(vkCreateSemaphore(m_VKDevice, &CreateSemaphoreInfo, nullptr, &m_AcquireImageSemaphore) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating acquire next image semaphore failed.");
			return false;
		}
		for(size_t i = 0; i < SyncObjectCount; i++)
		{
			if(vkCreateSemaphore(m_VKDevice, &CreateSemaphoreInfo, nullptr, &m_vQueueSubmitSemaphores[i]) != VK_SUCCESS ||
				vkCreateSemaphore(m_VKDevice, &CreateSemaphoreInfo, nullptr, &m_vBusyAcquireImageSemaphores[i]) != VK_SUCCESS ||
				vkCreateFence(m_VKDevice, &FenceInfo, nullptr, &m_vQueueSubmitFences[i]) != VK_SUCCESS ||
				vkCreateFence(m_VKDevice, &FenceInfo, nullptr, &m_vMemoryCommandBufferFences[i]) != VK_SUCCESS)
			{
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating swap chain sync objects(fences, semaphores) failed.");
				return false;
			}
		}

		return true;
	}

	void DestroySyncObjects()
	{
		for(size_t i = 0; i < m_vBusyAcquireImageSemaphores.size(); i++)
		{
			if(m_vBusyAcquireImageSemaphores[i] != VK_NULL_HANDLE)
				vkDestroySemaphore(m_VKDevice, m_vBusyAcquireImageSemaphores[i], nullptr);
			if(i < m_vQueueSubmitSemaphores.size() && m_vQueueSubmitSemaphores[i] != VK_NULL_HANDLE)
				vkDestroySemaphore(m_VKDevice, m_vQueueSubmitSemaphores[i], nullptr);
			if(i < m_vQueueSubmitFences.size() && m_vQueueSubmitFences[i] != VK_NULL_HANDLE)
				vkDestroyFence(m_VKDevice, m_vQueueSubmitFences[i], nullptr);
			if(i < m_vMemoryCommandBufferFences.size() && m_vMemoryCommandBufferFences[i] != VK_NULL_HANDLE)
				vkDestroyFence(m_VKDevice, m_vMemoryCommandBufferFences[i], nullptr);
		}
		if(m_AcquireImageSemaphore != VK_NULL_HANDLE)
			vkDestroySemaphore(m_VKDevice, m_AcquireImageSemaphore, nullptr);

		m_vBusyAcquireImageSemaphores.clear();
		m_vQueueSubmitSemaphores.clear();

		m_vQueueSubmitFences.clear();
		m_vMemoryCommandBufferFences.clear();
		m_AcquireImageSemaphore = VK_NULL_HANDLE;
	}

	void DestroyBufferOfFrame(size_t ImageIndex, SFrameBuffers &Buffer)
	{
		CleanBufferPair(ImageIndex, Buffer.m_Buffer, Buffer.m_BufferMem);
	}

	void DestroyUniBufferOfFrame(size_t ImageIndex, SFrameUniformBuffers &Buffer)
	{
		CleanBufferPair(ImageIndex, Buffer.m_Buffer, Buffer.m_BufferMem);
		for(auto &DescrSet : Buffer.m_aUniformSets)
		{
			if(DescrSet.m_Descriptor != VK_NULL_HANDLE)
			{
				DestroyUniformDescriptorSets(&DescrSet, 1);
			}
		}
	}

	/*************
	 * SWAP CHAIN
	 **************/

	void CleanupVulkanSwapChain(bool ForceSwapChainDestruct)
	{
		m_StandardPipeline.Destroy(m_VKDevice);
		m_StandardLinePipeline.Destroy(m_VKDevice);
		m_Standard3DPipeline.Destroy(m_VKDevice);
		m_TextPipeline.Destroy(m_VKDevice);
		m_TilePipeline.Destroy(m_VKDevice);
		m_TileBorderPipeline.Destroy(m_VKDevice);
		m_PrimExPipeline.Destroy(m_VKDevice);
		m_PrimExRotationlessPipeline.Destroy(m_VKDevice);
		m_SpriteMultiPipeline.Destroy(m_VKDevice);
		m_SpriteMultiPushPipeline.Destroy(m_VKDevice);
		m_QuadPipeline.Destroy(m_VKDevice);
		m_QuadGroupedPipeline.Destroy(m_VKDevice);
		m_MediaIslandSdfPipeline.Destroy(m_VKDevice);
		m_RoundedRectSdfPipeline.Destroy(m_VKDevice);
		m_TexturedMsdfPipeline.Destroy(m_VKDevice);
		m_TexturedMsdfPipelineValid = false;
		SyncTexturedMsdfCapability();
		m_GaussianBlurPipeline.Destroy(m_VKDevice);
		m_GaussianBlurPipelineValid = false;

		DestroyFramebuffers();

		DestroyRenderPass();

		DestroyMultiSamplerImageAttachments();

		DestroyImageViews();
		ClearSwapChainImageHandles();

		DestroySwapChain(ForceSwapChainDestruct);

		m_SwapchainCreated = false;
	}

	template<bool IsLastCleanup>
	void CleanupVulkan(size_t SwapchainCount)
	{
		if(IsLastCleanup)
		{
			if(m_SwapchainCreated)
				CleanupVulkanSwapChain(true);

			// clean all images, buffers, buffer containers
			for(auto &Texture : m_vTextures)
			{
				if(Texture.m_VKTextDescrSet.m_Descriptor != VK_NULL_HANDLE && IsVerbose())
				{
					dbg_msg("vulkan", "text textures not cleared over cmd.");
				}
				DestroyTexture(Texture);
			}
			for(auto &RenderTarget : m_vRenderTargets)
				DestroyRenderTarget(RenderTarget);
			m_vRenderTargets.clear();

			for(auto &BufferObject : m_vBufferObjects)
			{
				if(!BufferObject.m_IsStreamedBuffer)
					FreeVertexMemBlock(BufferObject.m_BufferObject.m_Mem);
			}

			m_vBufferContainers.clear();
		}

		m_vImageLastFrameCheck.clear();

		m_vLastPipeline.clear();
		m_vThreadFrameProfileStats.clear();
		m_vDrawCommandStates.clear();
		DestroyFrameTimestampQueries();

		for(size_t i = 0; i < m_ThreadCount; ++i)
		{
			m_vStreamedVertexBuffers[i].Destroy([&](size_t ImageIndex, SFrameBuffers &Buffer) { DestroyBufferOfFrame(ImageIndex, Buffer); });
			m_vStreamedUniformBuffers[i].Destroy([&](size_t ImageIndex, SFrameUniformBuffers &Buffer) { DestroyUniBufferOfFrame(ImageIndex, Buffer); });
		}
		m_vStreamedVertexBuffers.clear();
		m_vStreamedUniformBuffers.clear();

		for(size_t i = 0; i < SwapchainCount; ++i)
		{
			ClearFrameData(i);
		}

		m_vvFrameDelayedBufferCleanup.clear();
		m_vvFrameDelayedTextureCleanup.clear();
		m_vvFrameDelayedTextTexturesCleanup.clear();

		m_StagingBufferCache.DestroyFrameData(SwapchainCount);
		m_StagingBufferCacheImage.DestroyFrameData(SwapchainCount);
		m_VertexBufferCache.DestroyFrameData(SwapchainCount);
		for(auto &ImageBufferCache : m_ImageBufferCaches)
			ImageBufferCache.second.DestroyFrameData(SwapchainCount);

		if(IsLastCleanup)
		{
			m_StagingBufferCache.Destroy(m_VKDevice);
			m_StagingBufferCacheImage.Destroy(m_VKDevice);
			m_VertexBufferCache.Destroy(m_VKDevice);
			for(auto &ImageBufferCache : m_ImageBufferCaches)
				ImageBufferCache.second.Destroy(m_VKDevice);

			m_ImageBufferCaches.clear();

			DestroyTextureSamplers();
			DestroyDescriptorPools();

			DeletePresentedImageDataImage();
		}

		DestroySyncObjects();
		DestroyCommandBuffer();

		if(IsLastCleanup)
		{
			DestroyCommandPool();
		}

		if(IsLastCleanup)
		{
			DestroyUniformDescriptorSetLayouts();
			DestroyTextDescriptorSetLayout();
			DestroyDescriptorSetLayouts();
		}
	}

	void CleanupVulkanSDL()
	{
		if(m_VKInstance != VK_NULL_HANDLE)
		{
			DestroySurface();
			vkDestroyDevice(m_VKDevice, nullptr);
			m_pfnCreateSwapchainKHR = nullptr;
			m_pfnDestroySwapchainKHR = nullptr;
			m_pfnGetSwapchainImagesKHR = nullptr;
			m_pfnAcquireNextImageKHR = nullptr;
			m_pfnQueuePresentKHR = nullptr;

			if(g_Config.m_DbgGfx == DEBUG_GFX_MODE_MINIMUM || g_Config.m_DbgGfx == DEBUG_GFX_MODE_ALL)
			{
				UnregisterDebugCallback();
			}
			vkDestroyInstance(m_VKInstance, nullptr);
			m_VKInstance = VK_NULL_HANDLE;
		}
	}

	int RecreateSwapChain()
	{
		int Ret = 0;
		VkResult WaitIdleResult = DeviceWaitIdle();
		if(WaitIdleResult != VK_SUCCESS)
		{
			const char *pCritErrorMsg = CheckVulkanCriticalError(WaitIdleResult);
			if(pCritErrorMsg != nullptr)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_SWAP_FAILED, "Waiting for device idle before recreating swap chain failed.", pCritErrorMsg);
			else
				SetError(EGfxErrorType::GFX_ERROR_TYPE_SWAP_FAILED, "Waiting for device idle before recreating swap chain failed.");
			return -1;
		}

		if(IsVerbose())
		{
			dbg_msg("vulkan", "recreating swap chain.");
		}

		VkSwapchainKHR OldSwapChain = VK_NULL_HANDLE;
		uint32_t OldSwapChainImageCount = m_SwapChainImageCount;

		if(m_SwapchainCreated)
			CleanupVulkanSwapChain(false);

		// set new multi sampling if it was requested
		if(m_NextMultiSamplingCount != std::numeric_limits<uint32_t>::max())
		{
			m_MultiSamplingCount = m_NextMultiSamplingCount;
			m_NextMultiSamplingCount = std::numeric_limits<uint32_t>::max();
		}

		if(!m_SwapchainCreated)
			Ret = InitVulkanSwapChain(OldSwapChain);

		if(OldSwapChainImageCount != m_SwapChainImageCount)
		{
			CleanupVulkan<false>(OldSwapChainImageCount);
			InitVulkan<false>();
		}

		if(OldSwapChain != VK_NULL_HANDLE)
		{
			m_pfnDestroySwapchainKHR(m_VKDevice, OldSwapChain, nullptr);
		}

		if(Ret != 0 && IsVerbose())
		{
			dbg_msg("vulkan", "recreating swap chain failed.");
		}

		return Ret;
	}

	int InitVulkanSDL(SDL_Window *pWindow, uint32_t CanvasWidth, uint32_t CanvasHeight, char *pRendererString, char *pVendorString, char *pVersionString)
	{
		std::vector<std::string> vVKExtensions;
		std::vector<std::string> vVKLayers;

		m_CanvasWidth = CanvasWidth;
		m_CanvasHeight = CanvasHeight;

		if(!ResolveRequestedVulkanApiVersion())
			return -1;

		if(!GetVulkanExtensions(pWindow, vVKExtensions))
			return -1;

		if(!GetVulkanLayers(vVKLayers))
			return -1;

		const auto CreateConfiguredVulkanInstance = [&]() {
			if(!CreateVulkanInstance(vVKLayers, vVKExtensions, true))
				return false;

			if(g_Config.m_DbgGfx == DEBUG_GFX_MODE_MINIMUM || g_Config.m_DbgGfx == DEBUG_GFX_MODE_ALL)
			{
				SetupDebugCallback();

				for(auto &VKLayer : vVKLayers)
				{
					dbg_msg("vulkan", "Validation layer: %s", VKLayer.c_str());
				}
			}
			return true;
		};

		const auto FallbackToVulkan11 = [&](const char *pReason) {
			log_warn("gfx/vulkan", "%s Falling back to Vulkan 1.1.", pReason);
			g_Config.m_QmVulkanApiVersion = 11;
			DestroyVulkanInstance();
			m_RequestedApiVersion = VK_API_VERSION_1_1;
			m_EffectiveApiVersion = VK_API_VERSION_1_1;
			m_RequiredVulkanVersionUnavailable = false;
			ResetInitializationDiagnostics();
			*m_pGpuList = {};
			if(!CreateConfiguredVulkanInstance())
			{
				DestroyVulkanInstance();
				return false;
			}
			if(!SelectGpu(pRendererString, pVendorString, pVersionString))
			{
				DestroyVulkanInstance();
				return false;
			}
			return true;
		};

		if(!CreateConfiguredVulkanInstance())
		{
			if(m_RequestedApiVersion != VK_API_VERSION_1_4 || !FallbackToVulkan11("The selected Vulkan 1.4 instance could not be created."))
				return -1;
		}
		else if(!SelectGpu(pRendererString, pVendorString, pVersionString))
		{
			if(m_RequestedApiVersion != VK_API_VERSION_1_4 || !m_RequiredVulkanVersionUnavailable || !FallbackToVulkan11("No physical device supports the selected Vulkan 1.4 API."))
				return -1;
		}

		if(!CreateLogicalDevice(vVKLayers))
			return -1;

		GetDeviceQueue();

		if(!CreateSurface(pWindow))
			return -1;

		return 0;
	}

	/************************
	 * MEMORY MANAGEMENT
	 ************************/

	uint32_t FindMemoryType(VkPhysicalDevice PhyDevice, uint32_t TypeFilter, VkMemoryPropertyFlags Properties)
	{
		VkPhysicalDeviceMemoryProperties MemProperties;
		vkGetPhysicalDeviceMemoryProperties(PhyDevice, &MemProperties);

		for(uint32_t i = 0; i < MemProperties.memoryTypeCount; i++)
		{
			if((TypeFilter & (1 << i)) && (MemProperties.memoryTypes[i].propertyFlags & Properties) == Properties)
			{
				return i;
			}
		}

		return 0;
	}

	[[nodiscard]] bool CreateBuffer(VkDeviceSize BufferSize, EMemoryBlockUsage MemUsage, VkBufferUsageFlags BufferUsage, VkMemoryPropertyFlags MemoryProperties, VkBuffer &VKBuffer, SDeviceMemoryBlock &VKBufferMemory)
	{
		VkBufferCreateInfo BufferInfo{};
		BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		BufferInfo.size = BufferSize;
		BufferInfo.usage = BufferUsage;
		BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if(vkCreateBuffer(m_VKDevice, &BufferInfo, nullptr, &VKBuffer) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER, "Buffer creation failed.");
			return false;
		}

		VkMemoryRequirements MemRequirements;
		vkGetBufferMemoryRequirements(m_VKDevice, VKBuffer, &MemRequirements);

		VkMemoryAllocateInfo MemAllocInfo{};
		MemAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		MemAllocInfo.allocationSize = MemRequirements.size;
		MemAllocInfo.memoryTypeIndex = FindMemoryType(m_VKGPU, MemRequirements.memoryTypeBits, MemoryProperties);

		VKBufferMemory.m_Size = MemRequirements.size;

		if(MemUsage == MEMORY_BLOCK_USAGE_BUFFER)
			m_pBufferMemoryUsage->store(m_pBufferMemoryUsage->load(std::memory_order_relaxed) + MemRequirements.size, std::memory_order_relaxed);
		else if(MemUsage == MEMORY_BLOCK_USAGE_STAGING)
			m_pStagingMemoryUsage->store(m_pStagingMemoryUsage->load(std::memory_order_relaxed) + MemRequirements.size, std::memory_order_relaxed);
		else if(MemUsage == MEMORY_BLOCK_USAGE_STREAM)
			m_pStreamMemoryUsage->store(m_pStreamMemoryUsage->load(std::memory_order_relaxed) + MemRequirements.size, std::memory_order_relaxed);

		if(IsVerbose())
		{
			VerboseAllocatedMemory(MemRequirements.size, m_CurImageIndex, MemUsage);
		}

		if(!AllocateVulkanMemory(&MemAllocInfo, &VKBufferMemory.m_Mem))
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER, "Allocation for buffer object failed.");
			return false;
		}

		VKBufferMemory.m_UsageType = MemUsage;

		if(vkBindBufferMemory(m_VKDevice, VKBuffer, VKBufferMemory.m_Mem, 0) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER, "Binding memory to buffer failed.");
			return false;
		}

		return true;
	}

	[[nodiscard]] bool AllocateDescriptorPool(SDeviceDescriptorPools &DescriptorPools, size_t AllocPoolSize, SFrameProfileStats *pProfileStats = nullptr)
	{
		SDeviceDescriptorPool NewPool;
		NewPool.m_Size = AllocPoolSize;

		VkDescriptorPoolSize PoolSize{};
		if(DescriptorPools.m_IsUniformPool)
			PoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		else
			PoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		PoolSize.descriptorCount = AllocPoolSize;

		VkDescriptorPoolCreateInfo PoolInfo{};
		PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		PoolInfo.poolSizeCount = 1;
		PoolInfo.pPoolSizes = &PoolSize;
		PoolInfo.maxSets = AllocPoolSize;
		PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

		if(vkCreateDescriptorPool(m_VKDevice, &PoolInfo, nullptr, &NewPool.m_Pool) != VK_SUCCESS)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Creating the descriptor pool failed.");
			return false;
		}

		DescriptorPools.m_vPools.push_back(NewPool);
		if(pProfileStats != nullptr)
			pProfileStats->m_DescriptorPoolAllocations++;

		return true;
	}

	[[nodiscard]] bool CreateDescriptorPools()
	{
		m_StandardTextureDescrPool.m_IsUniformPool = false;
		m_StandardTextureDescrPool.m_DefaultAllocSize = 1024;
		m_TextTextureDescrPool.m_IsUniformPool = false;
		m_TextTextureDescrPool.m_DefaultAllocSize = 8;

		m_vUniformBufferDescrPools.resize(m_ThreadCount);
		for(auto &UniformBufferDescrPool : m_vUniformBufferDescrPools)
		{
			UniformBufferDescrPool.m_IsUniformPool = true;
			UniformBufferDescrPool.m_DefaultAllocSize = 512;
		}

		if(!AllocateDescriptorPool(m_StandardTextureDescrPool, CCommandBuffer::MAX_TEXTURES))
			return false;
		if(!AllocateDescriptorPool(m_TextTextureDescrPool, 8))
			return false;

		for(auto &UniformBufferDescrPool : m_vUniformBufferDescrPools)
		{
			if(!AllocateDescriptorPool(UniformBufferDescrPool, 64))
				return false;
		}

		return true;
	}

	void DestroyDescriptorPools()
	{
		const auto DestroyDescriptorPoolList = [this](SDeviceDescriptorPools &DescriptorPools) {
			for(auto &DescrPool : DescriptorPools.m_vPools)
			{
				if(DescrPool.m_Pool != VK_NULL_HANDLE)
					vkDestroyDescriptorPool(m_VKDevice, DescrPool.m_Pool, nullptr);
				DescrPool = {};
			}
			DescriptorPools.m_vPools.clear();
		};

		DestroyDescriptorPoolList(m_StandardTextureDescrPool);
		DestroyDescriptorPoolList(m_TextTextureDescrPool);
		for(auto &UniformBufferDescrPool : m_vUniformBufferDescrPools)
			DestroyDescriptorPoolList(UniformBufferDescrPool);
		m_vUniformBufferDescrPools.clear();
	}

	[[nodiscard]] bool GetDescriptorPoolForAlloc(VkDescriptorPool &RetDescr, SDeviceDescriptorPools &DescriptorPools, SDeviceDescriptorSet *pSets, size_t AllocNum, SFrameProfileStats *pProfileStats)
	{
		size_t CurAllocNum = AllocNum;
		size_t CurAllocOffset = 0;
		RetDescr = VK_NULL_HANDLE;

		while(CurAllocNum > 0)
		{
			size_t AllocatedInThisRun = 0;

			bool Found = false;
			size_t DescriptorPoolIndex = std::numeric_limits<size_t>::max();
			for(size_t i = 0; i < DescriptorPools.m_vPools.size(); ++i)
			{
				auto &Pool = DescriptorPools.m_vPools[i];
				if(Pool.m_CurSize + CurAllocNum < Pool.m_Size)
				{
					AllocatedInThisRun = CurAllocNum;
					Pool.m_CurSize += CurAllocNum;
					Found = true;
					if(RetDescr == VK_NULL_HANDLE)
						RetDescr = Pool.m_Pool;
					DescriptorPoolIndex = i;
					break;
				}
				else
				{
					size_t RemainingPoolCount = Pool.m_Size - Pool.m_CurSize;
					if(RemainingPoolCount > 0)
					{
						AllocatedInThisRun = RemainingPoolCount;
						Pool.m_CurSize += RemainingPoolCount;
						Found = true;
						if(RetDescr == VK_NULL_HANDLE)
							RetDescr = Pool.m_Pool;
						DescriptorPoolIndex = i;
						break;
					}
				}
			}

			if(!Found)
			{
				DescriptorPoolIndex = DescriptorPools.m_vPools.size();

				if(!AllocateDescriptorPool(DescriptorPools, DescriptorPools.m_DefaultAllocSize, pProfileStats))
					return false;

				AllocatedInThisRun = minimum((size_t)DescriptorPools.m_DefaultAllocSize, CurAllocNum);

				auto &Pool = DescriptorPools.m_vPools.back();
				Pool.m_CurSize += AllocatedInThisRun;
				if(RetDescr == VK_NULL_HANDLE)
					RetDescr = Pool.m_Pool;
			}

			for(size_t i = CurAllocOffset; i < CurAllocOffset + AllocatedInThisRun; ++i)
			{
				pSets[i].m_pPools = &DescriptorPools;
				pSets[i].m_PoolIndex = DescriptorPoolIndex;
			}
			CurAllocOffset += AllocatedInThisRun;
			CurAllocNum -= AllocatedInThisRun;
		}

		return true;
	}

	void FreeDescriptorSetFromPool(SDeviceDescriptorSet &DescrSet)
	{
		if(DescrSet.m_PoolIndex != std::numeric_limits<size_t>::max() && DescrSet.m_pPools != nullptr)
		{
			if(DescrSet.m_PoolIndex >= DescrSet.m_pPools->m_vPools.size())
			{
				log_warn("vulkan", "descriptor set references stale pool index %" PRIzu " (pool count %" PRIzu "), skipping explicit descriptor free", DescrSet.m_PoolIndex, DescrSet.m_pPools->m_vPools.size());
				DescrSet = {};
				return;
			}
			auto &Pool = DescrSet.m_pPools->m_vPools[DescrSet.m_PoolIndex];
			if(DescrSet.m_Descriptor != VK_NULL_HANDLE)
				vkFreeDescriptorSets(m_VKDevice, Pool.m_Pool, 1, &DescrSet.m_Descriptor);
			if(Pool.m_CurSize == 0)
			{
				log_warn("vulkan", "descriptor pool accounting underflow prevented while freeing descriptor set");
				DescrSet = {};
				return;
			}
			Pool.m_CurSize -= 1;
		}
		DescrSet = {};
	}

	[[nodiscard]] bool CreateNewTexturedStandardDescriptorSets(size_t TextureSlot, size_t DescrIndex)
	{
		auto &Texture = m_vTextures[TextureSlot];

		auto &DescrSet = Texture.m_aVKStandardTexturedDescrSets[DescrIndex];

		VkDescriptorSetAllocateInfo DesAllocInfo{};
		DesAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		if(!GetDescriptorPoolForAlloc(DesAllocInfo.descriptorPool, m_StandardTextureDescrPool, &DescrSet, 1, &m_FrameProfileStats))
			return false;
		DesAllocInfo.descriptorSetCount = 1;
		DesAllocInfo.pSetLayouts = &m_StandardTexturedDescriptorSetLayout;

		if(vkAllocateDescriptorSets(m_VKDevice, &DesAllocInfo, &DescrSet.m_Descriptor) != VK_SUCCESS)
		{
			FreeDescriptorSetFromPool(DescrSet);
			return false;
		}
		m_FrameProfileStats.m_DescriptorAllocations++;

		VkDescriptorImageInfo ImageInfo{};
		ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ImageInfo.imageView = Texture.m_ImgView;
		ImageInfo.sampler = Texture.m_aSamplers[DescrIndex];

		std::array<VkWriteDescriptorSet, 1> aDescriptorWrites{};

		aDescriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		aDescriptorWrites[0].dstSet = DescrSet.m_Descriptor;
		aDescriptorWrites[0].dstBinding = 0;
		aDescriptorWrites[0].dstArrayElement = 0;
		aDescriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		aDescriptorWrites[0].descriptorCount = 1;
		aDescriptorWrites[0].pImageInfo = &ImageInfo;

		vkUpdateDescriptorSets(m_VKDevice, static_cast<uint32_t>(aDescriptorWrites.size()), aDescriptorWrites.data(), 0, nullptr);
		m_FrameProfileStats.m_DescriptorUpdates += aDescriptorWrites.size();

		return true;
	}

	void DestroyTexturedStandardDescriptorSets(CTexture &Texture, size_t DescrIndex)
	{
		auto &DescrSet = Texture.m_aVKStandardTexturedDescrSets[DescrIndex];
		FreeDescriptorSetFromPool(DescrSet);
	}

	[[nodiscard]] bool CreateRenderTargetDescriptorSet(SRenderTarget &Target, size_t DescrIndex)
	{
		auto &DescrSet = Target.m_aVKStandardTexturedDescrSets[DescrIndex];
		VkDescriptorSetAllocateInfo DesAllocInfo{};
		DesAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		if(!GetDescriptorPoolForAlloc(DesAllocInfo.descriptorPool, m_StandardTextureDescrPool, &DescrSet, 1, &m_FrameProfileStats))
			return false;
		DesAllocInfo.descriptorSetCount = 1;
		DesAllocInfo.pSetLayouts = &m_StandardTexturedDescriptorSetLayout;
		if(vkAllocateDescriptorSets(m_VKDevice, &DesAllocInfo, &DescrSet.m_Descriptor) != VK_SUCCESS)
		{
			FreeDescriptorSetFromPool(DescrSet);
			return false;
		}
		m_FrameProfileStats.m_DescriptorAllocations++;

		VkDescriptorImageInfo ImageInfo{};
		ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ImageInfo.imageView = Target.m_ImageView;
		ImageInfo.sampler = m_aSamplers[SUPPORTED_SAMPLER_TYPE_CLAMP_TO_EDGE];

		VkWriteDescriptorSet DescriptorWrite{};
		DescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DescriptorWrite.dstSet = DescrSet.m_Descriptor;
		DescriptorWrite.dstBinding = 0;
		DescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		DescriptorWrite.descriptorCount = 1;
		DescriptorWrite.pImageInfo = &ImageInfo;
		vkUpdateDescriptorSets(m_VKDevice, 1, &DescriptorWrite, 0, nullptr);
		return true;
	}

	void DestroyRenderTarget(SRenderTarget &Target)
	{
		for(auto &DescrSet : Target.m_aVKStandardTexturedDescrSets)
		{
			FreeDescriptorSetFromPool(DescrSet);
			DescrSet = {};
		}
		if(Target.m_Framebuffer != VK_NULL_HANDLE)
			vkDestroyFramebuffer(m_VKDevice, Target.m_Framebuffer, nullptr);
		if(Target.m_ImageView != VK_NULL_HANDLE)
			vkDestroyImageView(m_VKDevice, Target.m_ImageView, nullptr);
		if(Target.m_Image != VK_NULL_HANDLE)
		{
			FreeImageMemBlock(Target.m_ImageMem);
			vkDestroyImage(m_VKDevice, Target.m_Image, nullptr);
		}
		Target = {};
	}

	[[nodiscard]] bool CreateNew3DTexturedStandardDescriptorSets(size_t TextureSlot)
	{
		auto &Texture = m_vTextures[TextureSlot];

		auto &DescrSet = Texture.m_VKStandard3DTexturedDescrSet;

		VkDescriptorSetAllocateInfo DesAllocInfo{};
		DesAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		if(!GetDescriptorPoolForAlloc(DesAllocInfo.descriptorPool, m_StandardTextureDescrPool, &DescrSet, 1, &m_FrameProfileStats))
			return false;
		DesAllocInfo.descriptorSetCount = 1;
		DesAllocInfo.pSetLayouts = &m_Standard3DTexturedDescriptorSetLayout;

		if(vkAllocateDescriptorSets(m_VKDevice, &DesAllocInfo, &DescrSet.m_Descriptor) != VK_SUCCESS)
		{
			FreeDescriptorSetFromPool(DescrSet);
			return false;
		}
		m_FrameProfileStats.m_DescriptorAllocations++;

		VkDescriptorImageInfo ImageInfo{};
		ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ImageInfo.imageView = Texture.m_Img3DView;
		ImageInfo.sampler = Texture.m_Sampler3D;

		std::array<VkWriteDescriptorSet, 1> aDescriptorWrites{};

		aDescriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		aDescriptorWrites[0].dstSet = DescrSet.m_Descriptor;
		aDescriptorWrites[0].dstBinding = 0;
		aDescriptorWrites[0].dstArrayElement = 0;
		aDescriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		aDescriptorWrites[0].descriptorCount = 1;
		aDescriptorWrites[0].pImageInfo = &ImageInfo;

		vkUpdateDescriptorSets(m_VKDevice, static_cast<uint32_t>(aDescriptorWrites.size()), aDescriptorWrites.data(), 0, nullptr);
		m_FrameProfileStats.m_DescriptorUpdates += aDescriptorWrites.size();

		return true;
	}

	void DestroyTextured3DStandardDescriptorSets(CTexture &Texture)
	{
		auto &DescrSet = Texture.m_VKStandard3DTexturedDescrSet;
		FreeDescriptorSetFromPool(DescrSet);
	}

	[[nodiscard]] bool CreateNewTextDescriptorSets(size_t Texture, size_t TextureOutline)
	{
		auto &TextureText = m_vTextures[Texture];
		auto &TextureTextOutline = m_vTextures[TextureOutline];
		auto &DescrSetText = TextureText.m_VKTextDescrSet;

		VkDescriptorSetAllocateInfo DesAllocInfo{};
		DesAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		if(!GetDescriptorPoolForAlloc(DesAllocInfo.descriptorPool, m_TextTextureDescrPool, &DescrSetText, 1, &m_FrameProfileStats))
			return false;
		DesAllocInfo.descriptorSetCount = 1;
		DesAllocInfo.pSetLayouts = &m_TextDescriptorSetLayout;

		if(vkAllocateDescriptorSets(m_VKDevice, &DesAllocInfo, &DescrSetText.m_Descriptor) != VK_SUCCESS)
		{
			FreeDescriptorSetFromPool(DescrSetText);
			return false;
		}
		m_FrameProfileStats.m_DescriptorAllocations++;

		std::array<VkDescriptorImageInfo, 2> aImageInfo{};
		aImageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		aImageInfo[0].imageView = TextureText.m_ImgView;
		aImageInfo[0].sampler = TextureText.m_aSamplers[0];
		aImageInfo[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		aImageInfo[1].imageView = TextureTextOutline.m_ImgView;
		aImageInfo[1].sampler = TextureTextOutline.m_aSamplers[0];

		std::array<VkWriteDescriptorSet, 2> aDescriptorWrites{};

		aDescriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		aDescriptorWrites[0].dstSet = DescrSetText.m_Descriptor;
		aDescriptorWrites[0].dstBinding = 0;
		aDescriptorWrites[0].dstArrayElement = 0;
		aDescriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		aDescriptorWrites[0].descriptorCount = 1;
		aDescriptorWrites[0].pImageInfo = aImageInfo.data();
		aDescriptorWrites[1] = aDescriptorWrites[0];
		aDescriptorWrites[1].dstBinding = 1;
		aDescriptorWrites[1].pImageInfo = &aImageInfo[1];

		vkUpdateDescriptorSets(m_VKDevice, static_cast<uint32_t>(aDescriptorWrites.size()), aDescriptorWrites.data(), 0, nullptr);
		m_FrameProfileStats.m_DescriptorUpdates += aDescriptorWrites.size();

		return true;
	}

	void DestroyTextDescriptorSets(CTexture &Texture, CTexture &TextureOutline)
	{
		auto &DescrSet = Texture.m_VKTextDescrSet;
		FreeDescriptorSetFromPool(DescrSet);
	}

	[[nodiscard]] bool HasMultiSampling() const
	{
		return GetSampleCount() != VK_SAMPLE_COUNT_1_BIT;
	}

	[[nodiscard]] bool SupportsRenderTargetReadback() const
	{
		return m_VKRenderTargetRenderPass != VK_NULL_HANDLE;
	}

	[[nodiscard]] bool SupportsRenderTargetGaussianBlur() const
	{
		return SupportsRenderTargetReadback() && m_GaussianBlurPipelineValid;
	}

	[[nodiscard]] bool SupportsBackbufferCapture() const
	{
		// 截图路径只验证过单采样交换链；多采样附件虽可恢复，但读取流程仍保持保守限制。
		const bool CompatibleFormat = m_VKSurfFormat.format == VK_FORMAT_B8G8R8A8_UNORM || m_VKSurfFormat.format == VK_FORMAT_R8G8B8A8_UNORM;
		return SupportsRenderTargetReadback() && CompatibleFormat && m_OptimalSwapChainImageBlitting && m_OptimalRGBAImageBlitting;
	}

	[[nodiscard]] const char *RenderTargetReadbackSupportReason() const
	{
		if(m_VKRenderTargetRenderPass == VK_NULL_HANDLE)
			return "vulkan_render_target_pass_missing";
		return "supported";
	}

	VkSampleCountFlagBits GetMaxSampleCount() const
	{
		if(m_MaxMultiSample & VK_SAMPLE_COUNT_64_BIT)
			return VK_SAMPLE_COUNT_64_BIT;
		else if(m_MaxMultiSample & VK_SAMPLE_COUNT_32_BIT)
			return VK_SAMPLE_COUNT_32_BIT;
		else if(m_MaxMultiSample & VK_SAMPLE_COUNT_16_BIT)
			return VK_SAMPLE_COUNT_16_BIT;
		else if(m_MaxMultiSample & VK_SAMPLE_COUNT_8_BIT)
			return VK_SAMPLE_COUNT_8_BIT;
		else if(m_MaxMultiSample & VK_SAMPLE_COUNT_4_BIT)
			return VK_SAMPLE_COUNT_4_BIT;
		else if(m_MaxMultiSample & VK_SAMPLE_COUNT_2_BIT)
			return VK_SAMPLE_COUNT_2_BIT;

		return VK_SAMPLE_COUNT_1_BIT;
	}

	VkSampleCountFlagBits GetSampleCount() const
	{
		auto MaxSampleCount = GetMaxSampleCount();
		if(m_MultiSamplingCount >= 64 && MaxSampleCount >= VK_SAMPLE_COUNT_64_BIT)
			return VK_SAMPLE_COUNT_64_BIT;
		else if(m_MultiSamplingCount >= 32 && MaxSampleCount >= VK_SAMPLE_COUNT_32_BIT)
			return VK_SAMPLE_COUNT_32_BIT;
		else if(m_MultiSamplingCount >= 16 && MaxSampleCount >= VK_SAMPLE_COUNT_16_BIT)
			return VK_SAMPLE_COUNT_16_BIT;
		else if(m_MultiSamplingCount >= 8 && MaxSampleCount >= VK_SAMPLE_COUNT_8_BIT)
			return VK_SAMPLE_COUNT_8_BIT;
		else if(m_MultiSamplingCount >= 4 && MaxSampleCount >= VK_SAMPLE_COUNT_4_BIT)
			return VK_SAMPLE_COUNT_4_BIT;
		else if(m_MultiSamplingCount >= 2 && MaxSampleCount >= VK_SAMPLE_COUNT_2_BIT)
			return VK_SAMPLE_COUNT_2_BIT;

		return VK_SAMPLE_COUNT_1_BIT;
	}

	int InitVulkanSwapChain(VkSwapchainKHR &OldSwapChain)
	{
		OldSwapChain = VK_NULL_HANDLE;
		if(!CreateSwapChain(OldSwapChain))
			return -1;

		if(!GetSwapChainImageHandles())
			return -1;

		if(!CreateImageViews())
			return -1;

		if(!CreateMultiSamplerImageAttachments())
		{
			return -1;
		}

		m_LastPresentedSwapChainImageIndex = std::numeric_limits<decltype(m_LastPresentedSwapChainImageIndex)>::max();

		if(!CreateRenderPass(m_VKRenderPass, true))
			return -1;
		if(!CreateRenderPass(m_VKRenderPassLoad, false, true, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR))
			return -1;
		if(!CreateRenderPass(m_VKRenderTargetRenderPass, true, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true, RenderTargetReadbackFormat()))
			return -1;

		if(!CreateFramebuffers())
			return -1;

		if(!CreateStandardGraphicsPipeline("shader/vulkan/prim.vert.spv", "shader/vulkan/prim.frag.spv", false, false))
			return -1;

		if(!CreateStandardGraphicsPipeline("shader/vulkan/prim_textured.vert.spv", "shader/vulkan/prim_textured.frag.spv", true, false))
			return -1;

		if(!CreateStandardGraphicsPipeline("shader/vulkan/prim.vert.spv", "shader/vulkan/prim.frag.spv", false, true))
			return -1;

		if(!CreateStandardGraphicsPipeline("shader/vulkan/prim_textured.vert.spv", "shader/vulkan/prim_textured.frag.spv", true, true))
			return -1;

		if(!CreateStandard3DGraphicsPipeline("shader/vulkan/prim3d.vert.spv", "shader/vulkan/prim3d.frag.spv", false))
			return -1;

		if(!CreateStandard3DGraphicsPipeline("shader/vulkan/prim3d_textured.vert.spv", "shader/vulkan/prim3d_textured.frag.spv", true))
			return -1;

		if(!CreateTextGraphicsPipeline("shader/vulkan/text.vert.spv", "shader/vulkan/text.frag.spv"))
			return -1;

		if(!CreateTileGraphicsPipeline<false>("shader/vulkan/tile.vert.spv", "shader/vulkan/tile.frag.spv", false))
			return -1;

		if(!CreateTileGraphicsPipeline<true>("shader/vulkan/tile_textured.vert.spv", "shader/vulkan/tile_textured.frag.spv", false))
			return -1;

		if(!CreateTileGraphicsPipeline<false>("shader/vulkan/tile_border.vert.spv", "shader/vulkan/tile_border.frag.spv", true))
			return -1;

		if(!CreateTileGraphicsPipeline<true>("shader/vulkan/tile_border_textured.vert.spv", "shader/vulkan/tile_border_textured.frag.spv", true))
			return -1;

		if(!CreatePrimExGraphicsPipeline("shader/vulkan/primex_rotationless.vert.spv", "shader/vulkan/primex_rotationless.frag.spv", false, true))
			return -1;

		if(!CreatePrimExGraphicsPipeline("shader/vulkan/primex_tex_rotationless.vert.spv", "shader/vulkan/primex_tex_rotationless.frag.spv", true, true))
			return -1;

		if(!CreatePrimExGraphicsPipeline("shader/vulkan/primex.vert.spv", "shader/vulkan/primex.frag.spv", false, false))
			return -1;

		if(!CreatePrimExGraphicsPipeline("shader/vulkan/primex_tex.vert.spv", "shader/vulkan/primex_tex.frag.spv", true, false))
			return -1;

		if(!CreateSpriteMultiGraphicsPipeline("shader/vulkan/spritemulti.vert.spv", "shader/vulkan/spritemulti.frag.spv"))
			return -1;

		if(!CreateSpriteMultiPushGraphicsPipeline("shader/vulkan/spritemulti_push.vert.spv", "shader/vulkan/spritemulti_push.frag.spv"))
			return -1;

		if(!CreateQuadGraphicsPipeline<false>("shader/vulkan/quad.vert.spv", "shader/vulkan/quad.frag.spv"))
			return -1;

		if(!CreateMediaIslandSdfGraphicsPipeline("shader/vulkan/media_island_sdf.vert.spv", "shader/vulkan/media_island_sdf.frag.spv"))
			return -1;
		if(!CreateRoundedRectSdfGraphicsPipeline("shader/vulkan/rounded_rect_sdf.vert.spv", "shader/vulkan/rounded_rect_sdf.frag.spv"))
			return -1;
		m_TexturedMsdfPipelineValid = CreateTexturedMsdfGraphicsPipeline("shader/vulkan/textured_msdf.vert.spv", "shader/vulkan/textured_msdf.frag.spv");
		SyncTexturedMsdfCapability();
		if(!m_TexturedMsdfPipelineValid)
		{
			m_TexturedMsdfPipeline.Destroy(m_VKDevice);
			if(m_TexturedMsdfPipelineRequired)
			{
				SetError(EGfxErrorType::GFX_ERROR_TYPE_INIT, "Recreating the textured MSDF pipeline failed.");
				return -1;
			}
			SetWarning(EGfxWarningType::GFX_WARNING_TYPE_INIT_FAILED, "Textured MSDF pipeline unavailable, falling back to alpha icon atlas.");
		}
		else
		{
			m_TexturedMsdfPipelineRequired = true;
		}
		m_GaussianBlurPipelineValid = CreateGaussianBlurGraphicsPipeline("shader/vulkan/gaussian_blur.vert.spv", "shader/vulkan/gaussian_blur.frag.spv");
		if(!m_GaussianBlurPipelineValid)
			return -1;

		if(!CreateQuadGraphicsPipeline<true>("shader/vulkan/quad_textured.vert.spv", "shader/vulkan/quad_textured.frag.spv"))
			return -1;

		if(!CreateQuadGroupedGraphicsPipeline<false>("shader/vulkan/quad_grouped.vert.spv", "shader/vulkan/quad_grouped.frag.spv"))
			return -1;

		if(!CreateQuadGroupedGraphicsPipeline<true>("shader/vulkan/quad_grouped_textured.vert.spv", "shader/vulkan/quad_grouped_textured.frag.spv"))
			return -1;

		m_SwapchainCreated = true;
		return 0;
	}

	template<bool IsFirstInitialization>
	int InitVulkan()
	{
		if(IsFirstInitialization)
		{
			if(!CreateDescriptorSetLayouts())
				return -1;

			if(!CreateTextDescriptorSetLayout())
				return -1;

			if(!CreateSpriteMultiUniformDescriptorSetLayout())
				return -1;

			if(!CreateQuadUniformDescriptorSetLayout())
				return -1;

			VkSwapchainKHR OldSwapChain = VK_NULL_HANDLE;
			if(InitVulkanSwapChain(OldSwapChain) != 0)
				return -1;
		}

		if(IsFirstInitialization)
		{
			if(!CreateCommandPool())
				return -1;
		}

		if(!CreateCommandBuffers())
			return -1;

		if(!CreateSyncObjects())
			return -1;

		CreateFrameTimestampQueries();

		if(IsFirstInitialization)
		{
			if(!CreateDescriptorPools())
				return -1;

			if(!CreateTextureSamplers())
				return -1;
		}

		m_vStreamedVertexBuffers.resize(m_ThreadCount);
		m_vStreamedUniformBuffers.resize(m_ThreadCount);
		for(size_t i = 0; i < m_ThreadCount; ++i)
		{
			m_vStreamedVertexBuffers[i].Init(m_SwapChainImageCount);
			m_vStreamedUniformBuffers[i].Init(m_SwapChainImageCount);
		}

		m_vLastPipeline.resize(m_ThreadCount, VK_NULL_HANDLE);
		m_vThreadFrameProfileStats.resize(m_ThreadCount);
		m_vDrawCommandStates.resize(m_ThreadCount);

		m_vvFrameDelayedBufferCleanup.resize(m_SwapChainImageCount);
		m_vvFrameDelayedTextureCleanup.resize(m_SwapChainImageCount);
		m_vvFrameDelayedTextTexturesCleanup.resize(m_SwapChainImageCount);
		m_StagingBufferCache.Init(m_SwapChainImageCount);
		m_StagingBufferCacheImage.Init(m_SwapChainImageCount);
		m_VertexBufferCache.Init(m_SwapChainImageCount);
		for(auto &ImageBufferCache : m_ImageBufferCaches)
			ImageBufferCache.second.Init(m_SwapChainImageCount);

		m_vImageLastFrameCheck.resize(m_SwapChainImageCount, 0);

		if(IsFirstInitialization)
		{
			// check if image format supports linear blitting
			VkFormatProperties FormatProperties;
			vkGetPhysicalDeviceFormatProperties(m_VKGPU, VK_FORMAT_R8G8B8A8_UNORM, &FormatProperties);
			if((FormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0)
			{
				m_AllowsLinearBlitting = true;
			}
			if((FormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0 && (FormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0)
			{
				m_OptimalRGBAImageBlitting = true;
			}
			// check if image format supports blitting to linear tiled images
			if((FormatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0)
			{
				m_LinearRGBAImageBlitting = true;
			}

			vkGetPhysicalDeviceFormatProperties(m_VKGPU, m_VKSurfFormat.format, &FormatProperties);
			if((FormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0)
			{
				m_OptimalSwapChainImageBlitting = true;
			}
			if((FormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0)
			{
				m_OptimalSwapChainImageLinearBlitting = true;
			}
		}

		return 0;
	}

	[[nodiscard]] bool GetMemoryCommandBuffer(VkCommandBuffer *&pMemCommandBuffer)
	{
		auto &MemCommandBuffer = m_vMemoryCommandBuffers[m_CurImageIndex];
		if(!m_vUsedMemoryCommandBuffer[m_CurImageIndex])
		{
			m_vUsedMemoryCommandBuffer[m_CurImageIndex] = true;

			vkResetCommandBuffer(MemCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

			VkCommandBufferBeginInfo BeginInfo{};
			BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			if(vkBeginCommandBuffer(MemCommandBuffer, &BeginInfo) != VK_SUCCESS)
			{
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_RECORDING, "Command buffer cannot be filled anymore.");
				return false;
			}
		}
		pMemCommandBuffer = &MemCommandBuffer;
		return true;
	}

	[[nodiscard]] bool GetGraphicCommandBuffer(VkCommandBuffer *&pDrawCommandBuffer, size_t RenderThreadIndex)
	{
		if(m_ThreadCount < 2 || m_ForceSingleThreadedRender)
		{
			pDrawCommandBuffer = &m_vMainDrawCommandBuffers[m_CurImageIndex];
			return true;
		}
		else
		{
			auto &DrawCommandBuffer = m_vvThreadDrawCommandBuffers[RenderThreadIndex][m_CurImageIndex];
			if(!m_vvUsedThreadDrawCommandBuffer[RenderThreadIndex][m_CurImageIndex])
			{
				m_vvUsedThreadDrawCommandBuffer[RenderThreadIndex][m_CurImageIndex] = true;

				vkResetCommandBuffer(DrawCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

				VkCommandBufferBeginInfo BeginInfo{};
				BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;

				VkCommandBufferInheritanceInfo InheritanceInfo{};
				InheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
				InheritanceInfo.framebuffer = m_vFramebufferList[m_CurImageIndex];
				InheritanceInfo.occlusionQueryEnable = VK_FALSE;
				InheritanceInfo.renderPass = m_VKRenderPass;
				InheritanceInfo.subpass = 0;

				BeginInfo.pInheritanceInfo = &InheritanceInfo;

				if(vkBeginCommandBuffer(DrawCommandBuffer, &BeginInfo) != VK_SUCCESS)
				{
					SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_RECORDING, "Thread draw command buffer cannot be filled anymore.");
					return false;
				}
			}
			pDrawCommandBuffer = &DrawCommandBuffer;
			return true;
		}
	}

	VkCommandBuffer &GetMainGraphicCommandBuffer()
	{
		return m_vMainDrawCommandBuffers[m_CurImageIndex];
	}

	/************************
	 * STREAM BUFFERS SETUP
	 ************************/

	typedef std::function<bool(SFrameBuffers &, VkBuffer, VkDeviceSize)> TNewMemFunc;

	template<typename TStreamMemName>
	void CleanupStreamBufferDescriptors(TStreamMemName &Buffer)
	{
	}

	void CleanupStreamBufferDescriptors(SFrameUniformBuffers &Buffer)
	{
		DestroyUniformDescriptorSets(Buffer.m_aUniformSets.data(), Buffer.m_aUniformSets.size());
	}

	// returns true, if the stream memory was just allocated
	template<typename TStreamMemName, typename TInstanceTypeName, size_t InstanceTypeCount, size_t BufferCreateCount, bool UsesCurrentCountOffset>
	[[nodiscard]] bool CreateStreamBuffer(size_t RenderThreadIndex, TStreamMemName *&pBufferMem, TNewMemFunc &&NewMemFunc, SStreamMemory<TStreamMemName> &StreamUniformBuffer, VkBufferUsageFlagBits Usage, VkBuffer &NewBuffer, SDeviceMemoryBlock &NewBufferMem, size_t &BufferOffset, const void *pData, size_t DataSize)
	{
		VkBuffer Buffer = VK_NULL_HANDLE;
		SDeviceMemoryBlock BufferMem;
		size_t Offset = 0;

		uint8_t *pMem = nullptr;

		size_t BufferCountOffset = 0;
		if(UsesCurrentCountOffset)
			BufferCountOffset = StreamUniformBuffer.GetUsedCount(m_CurImageIndex);
		for(; BufferCountOffset < StreamUniformBuffer.GetBuffers(m_CurImageIndex).size(); ++BufferCountOffset)
		{
			auto &BufferOfFrame = StreamUniformBuffer.GetBuffers(m_CurImageIndex)[BufferCountOffset];
			if(BufferOfFrame.m_Size >= DataSize + BufferOfFrame.m_UsedSize)
			{
				if(BufferOfFrame.m_UsedSize == 0)
					StreamUniformBuffer.IncreaseUsedCount(m_CurImageIndex);
				Buffer = BufferOfFrame.m_Buffer;
				BufferMem = BufferOfFrame.m_BufferMem;
				Offset = BufferOfFrame.m_UsedSize;
				BufferOfFrame.m_UsedSize += DataSize;
				pMem = BufferOfFrame.m_pMappedBufferData;
				pBufferMem = &BufferOfFrame;
				break;
			}
		}

		if(BufferMem.m_Mem == VK_NULL_HANDLE)
		{
			// create memory
			VkBuffer StreamBuffer;
			SDeviceMemoryBlock StreamBufferMemory;
			const VkDeviceSize NewBufferSingleSize = sizeof(TInstanceTypeName) * InstanceTypeCount;
			const VkDeviceSize NewBufferSize = NewBufferSingleSize * BufferCreateCount;
			if(!CreateBuffer(NewBufferSize, MEMORY_BLOCK_USAGE_STREAM, Usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, StreamBuffer, StreamBufferMemory))
				return false;

			void *pMappedData = nullptr;
			if(vkMapMemory(m_VKDevice, StreamBufferMemory.m_Mem, 0, VK_WHOLE_SIZE, 0, &pMappedData) != VK_SUCCESS)
			{
				CleanBufferPair(m_CurImageIndex, StreamBuffer, StreamBufferMemory);
				return false;
			}
			DrawProfileStats(RenderThreadIndex).m_StreamBufferAllocations++;
			DrawProfileStats(RenderThreadIndex).m_StreamBufferAllocationBytes += StreamBufferMemory.m_Size;

			auto &vBuffers = StreamUniformBuffer.GetBuffers(m_CurImageIndex);
			auto &vRanges = StreamUniformBuffer.GetRanges(m_CurImageIndex);
			size_t NewBufferIndex = vBuffers.size();
			for(size_t i = 0; i < BufferCreateCount; ++i)
			{
				vBuffers.push_back(TStreamMemName(StreamBuffer, StreamBufferMemory, NewBufferSingleSize * i, NewBufferSingleSize, 0, ((uint8_t *)pMappedData) + (NewBufferSingleSize * i)));
				vRanges.push_back({});
				if(!NewMemFunc(vBuffers.back(), StreamBuffer, NewBufferSingleSize * i))
				{
					for(size_t BufferIndex = NewBufferIndex; BufferIndex < vBuffers.size(); ++BufferIndex)
						CleanupStreamBufferDescriptors(vBuffers[BufferIndex]);
					vBuffers.erase(vBuffers.begin() + NewBufferIndex, vBuffers.end());
					vRanges.erase(vRanges.begin() + NewBufferIndex, vRanges.end());
					vkUnmapMemory(m_VKDevice, StreamBufferMemory.m_Mem);
					CleanBufferPair(m_CurImageIndex, StreamBuffer, StreamBufferMemory);
					return false;
				}
			}
			auto &NewStreamBuffer = vBuffers[NewBufferIndex];

			Buffer = StreamBuffer;
			BufferMem = StreamBufferMemory;

			pBufferMem = &NewStreamBuffer;
			pMem = NewStreamBuffer.m_pMappedBufferData;
			Offset = NewStreamBuffer.m_OffsetInBuffer;
			NewStreamBuffer.m_UsedSize += DataSize;

			StreamUniformBuffer.IncreaseUsedCount(m_CurImageIndex);
		}

		// Offset here is the offset in the buffer
		if(BufferMem.m_Size - Offset < DataSize)
		{
			SetError(EGfxErrorType::GFX_ERROR_TYPE_OUT_OF_MEMORY_BUFFER, "Stream buffers are limited to CCommandBuffer::MAX_VERTICES. Exceeding it is a bug in the high level code.");
			return false;
		}

		{
			mem_copy(pMem + Offset, pData, DataSize);
		}
		auto &Stats = DrawProfileStats(RenderThreadIndex);
		Stats.m_StreamUploads++;
		Stats.m_StreamUploadBytes += DataSize;

		NewBuffer = Buffer;
		NewBufferMem = BufferMem;
		BufferOffset = Offset;

		return true;
	}

	[[nodiscard]] bool CreateStreamVertexBuffer(size_t RenderThreadIndex, VkBuffer &NewBuffer, SDeviceMemoryBlock &NewBufferMem, size_t &BufferOffset, const void *pData, size_t DataSize)
	{
		SFrameBuffers *pStreamBuffer;
		return CreateStreamBuffer<SFrameBuffers, GL_SVertexTex3DStream, CCommandBuffer::MAX_VERTICES * 2, 1, false>(
			RenderThreadIndex, pStreamBuffer, [](SFrameBuffers &, VkBuffer, VkDeviceSize) { return true; }, m_vStreamedVertexBuffers[RenderThreadIndex], VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, NewBuffer, NewBufferMem, BufferOffset, pData, DataSize);
	}

	template<typename TName, size_t InstanceMaxParticleCount, size_t MaxInstances>
	[[nodiscard]] bool GetUniformBufferObjectImpl(size_t RenderThreadIndex, bool RequiresSharedStagesDescriptor, SStreamMemory<SFrameUniformBuffers> &StreamUniformBuffer, SDeviceDescriptorSet &DescrSet, const void *pData, size_t DataSize)
	{
		VkBuffer NewBuffer;
		SDeviceMemoryBlock NewBufferMem;
		size_t BufferOffset;
		SFrameUniformBuffers *pMem;
		if(!CreateStreamBuffer<SFrameUniformBuffers, TName, InstanceMaxParticleCount, MaxInstances, true>(
			   RenderThreadIndex,
			   pMem,
			   [](SFrameBuffers &, VkBuffer, VkDeviceSize) { return true; },
			   StreamUniformBuffer, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, NewBuffer, NewBufferMem, BufferOffset, pData, DataSize))
			return false;

		const size_t UniformSetIndex = RequiresSharedStagesDescriptor ? 1 : 0;
		if(pMem->m_aUniformSets[UniformSetIndex].m_Descriptor == VK_NULL_HANDLE)
		{
			auto &SetLayout = RequiresSharedStagesDescriptor ? m_QuadUniformDescriptorSetLayout : m_SpriteMultiUniformDescriptorSetLayout;
			if(!CreateUniformDescriptorSets(RenderThreadIndex, SetLayout, &pMem->m_aUniformSets[UniformSetIndex], 1, NewBuffer, InstanceMaxParticleCount * sizeof(TName), pMem->m_OffsetInBuffer))
				return false;
		}

		DescrSet = pMem->m_aUniformSets[UniformSetIndex];
		return true;
	}

	[[nodiscard]] bool GetUniformBufferObject(size_t RenderThreadIndex, bool RequiresSharedStagesDescriptor, SDeviceDescriptorSet &DescrSet, size_t ParticleCount, const void *pData, size_t DataSize)
	{
		return GetUniformBufferObjectImpl<IGraphics::SRenderSpriteInfo, 512, 128>(RenderThreadIndex, RequiresSharedStagesDescriptor, m_vStreamedUniformBuffers[RenderThreadIndex], DescrSet, pData, DataSize);
	}

	[[nodiscard]] bool CreateIndexBuffer(void *pData, size_t DataSize, VkBuffer &Buffer, SDeviceMemoryBlock &Memory)
	{
		VkDeviceSize BufferDataSize = DataSize;

		SMemoryBlock<STAGING_BUFFER_CACHE_ID> StagingBuffer;
		if(!GetStagingBuffer(StagingBuffer, pData, DataSize))
			return false;

		SDeviceMemoryBlock VertexBufferMemory;
		VkBuffer VertexBuffer;
		if(!CreateBuffer(BufferDataSize, MEMORY_BLOCK_USAGE_BUFFER, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VertexBuffer, VertexBufferMemory))
			return false;

		if(!MemoryBarrier(VertexBuffer, 0, BufferDataSize, VK_ACCESS_INDEX_READ_BIT, true))
			return false;
		if(!CopyBuffer(StagingBuffer.m_Buffer, VertexBuffer, StagingBuffer.m_HeapData.m_OffsetToAlign, 0, BufferDataSize))
			return false;
		if(!MemoryBarrier(VertexBuffer, 0, BufferDataSize, VK_ACCESS_INDEX_READ_BIT, false))
			return false;

		UploadAndFreeStagingMemBlock(StagingBuffer);

		Buffer = VertexBuffer;
		Memory = VertexBufferMemory;
		return true;
	}

	void DestroyIndexBuffer(VkBuffer &Buffer, SDeviceMemoryBlock &Memory)
	{
		CleanBufferPair(0, Buffer, Memory);
	}

	SFrameProfileStats &DrawProfileStats(size_t RenderThreadIndex)
	{
		if(RenderThreadIndex < m_vThreadFrameProfileStats.size())
			return m_vThreadFrameProfileStats[RenderThreadIndex];
		return m_FrameProfileStats;
	}

	void ResetDrawCommandState(size_t RenderThreadIndex)
	{
		if(RenderThreadIndex < m_vDrawCommandStates.size())
			m_vDrawCommandStates[RenderThreadIndex] = {};
		if(RenderThreadIndex < m_vLastPipeline.size())
			m_vLastPipeline[RenderThreadIndex] = VK_NULL_HANDLE;
	}

	void ResetFrameProfileData()
	{
		if(m_FrameProfilingActive)
		{
			m_FrameProfileStats = {};
			for(auto &ThreadStats : m_vThreadFrameProfileStats)
				ThreadStats = {};
			m_FrameProfileStartTime = time_get_nanoseconds();
		}
		for(size_t RenderThreadIndex = 0; RenderThreadIndex < m_vDrawCommandStates.size(); ++RenderThreadIndex)
			ResetDrawCommandState(RenderThreadIndex);
		m_FrameTimestampQueryRecorded = false;
	}

	SFrameProfileStats CurrentFrameProfileStats() const
	{
		SFrameProfileStats Stats = m_FrameProfileStats;
		for(const auto &ThreadStats : m_vThreadFrameProfileStats)
			Stats.Add(ThreadStats);
		return Stats;
	}

	void LogFrameProfileStats()
	{
		if(!FrameProfilingEnabled() || m_CurFrame - m_LastFrameProfileLogFrame < 120)
			return;

		m_LastFrameProfileLogFrame = m_CurFrame;
		SFrameProfileStats Stats = CurrentFrameProfileStats();
		dbg_msg("vulkan",
			"profile frame=%" PRIu64 " cmds=%" PRIu64 " render_cmds=%" PRIu64 " prepare=%" PRIu64 " main_record=%" PRIu64 " thread_record=%" PRIu64 " estimated_draws=%" PRIu64 " draws=%" PRIu64 " "
			"pipeline=%" PRIu64 "/%" PRIu64 " vb=%" PRIu64 "/%" PRIu64 " ib=%" PRIu64 "/%" PRIu64 " dynamic=%" PRIu64 "/%" PRIu64 " descriptors=%" PRIu64 "/%" PRIu64 " "
			"desc_pool=%" PRIu64 " desc_alloc=%" PRIu64 " desc_update=%" PRIu64 " stream=%" PRIu64 "(%" PRIu64 " KiB) stream_alloc=%" PRIu64 "(%" PRIu64 " KiB) staging=%" PRIu64 "(%" PRIu64 " KiB) staging_alloc=%" PRIu64 "(%" PRIu64 " KiB) "
			"flush=%" PRIu64 "/%" PRIu64 " invalidate=%" PRIu64 "/%" PRIu64 " readback=%" PRIu64 "(%" PRIu64 " KiB) "
			"barriers=%" PRIu64 "/%" PRIu64 " submits=%" PRIu64 "/%" PRIu64 " waits=%" PRIu64 " queue_waits=%" PRIu64 " device_waits=%" PRIu64 " "
			"cpu=%" PRId64 "us gpu_last=%" PRId64 "us gpu_valid=%d prepare=%" PRId64 "us main_record=%" PRId64 "us thread_record=%" PRId64 "us wait=%" PRId64 "us submit=%" PRId64 "us queue_wait=%" PRId64 "us device_wait=%" PRId64 "us invalidate=%" PRId64 "us",
			m_CurFrame,
			Stats.m_CommandCount, Stats.m_RenderCommands,
			Stats.m_CommandPrepares, Stats.m_MainCommandRecords, Stats.m_ThreadCommandRecords,
			Stats.m_EstimatedRenderCallCount, Stats.m_DrawCalls,
			Stats.m_PipelineBinds, Stats.m_PipelineBindSkips,
			Stats.m_VertexBufferBinds, Stats.m_VertexBufferBindSkips,
			Stats.m_IndexBufferBinds, Stats.m_IndexBufferBindSkips,
			Stats.m_DynamicStateSets, Stats.m_DynamicStateSetSkips,
			Stats.m_DescriptorBinds, Stats.m_DescriptorBindSkips,
			Stats.m_DescriptorPoolAllocations,
			Stats.m_DescriptorAllocations, Stats.m_DescriptorUpdates,
			Stats.m_StreamUploads, Stats.m_StreamUploadBytes / 1024,
			Stats.m_StreamBufferAllocations, Stats.m_StreamBufferAllocationBytes / 1024,
			Stats.m_StagingUploads, Stats.m_StagingUploadBytes / 1024,
			Stats.m_StagingBufferAllocations, Stats.m_StagingBufferAllocationBytes / 1024,
			Stats.m_FlushCalls, Stats.m_FlushRanges,
			Stats.m_InvalidateCalls, Stats.m_InvalidateRanges,
			Stats.m_ReadbackRequests, Stats.m_ReadbackBytes / 1024,
			Stats.m_BarrierCalls, Stats.m_Barriers,
			Stats.m_QueueSubmits, Stats.m_QueueSubmitCommandBuffers,
			Stats.m_FenceWaits, Stats.m_QueueWaits, Stats.m_DeviceWaits,
			Stats.m_CPUFrameTime.count() / 1000,
			Stats.m_HasGPUFrameTime ? Stats.m_GPUFrameTime.count() / 1000 : 0,
			Stats.m_HasGPUFrameTime ? 1 : 0,
			Stats.m_CPUCommandPrepareTime.count() / 1000,
			Stats.m_CPUMainCommandRecordTime.count() / 1000,
			Stats.m_CPUThreadCommandRecordTime.count() / 1000,
			Stats.m_CPUFenceWaitTime.count() / 1000,
			Stats.m_CPUQueueSubmitTime.count() / 1000,
			Stats.m_CPUQueueWaitTime.count() / 1000,
			Stats.m_CPUDeviceWaitTime.count() / 1000,
			Stats.m_CPUInvalidateTime.count() / 1000);
	}

	uint32_t FrameTimestampQueryOffset(size_t ImageIndex) const
	{
		return static_cast<uint32_t>(ImageIndex * FRAME_TIMESTAMP_QUERY_COUNT);
	}

	void ReadFrameTimestampQuery(size_t ImageIndex)
	{
		if(!m_FrameTimestampQueriesSupported || m_FrameTimestampQueryPool == VK_NULL_HANDLE || ImageIndex >= m_vFrameTimestampQueriesPending.size() || !m_vFrameTimestampQueriesPending[ImageIndex])
			return;

		std::array<uint64_t, FRAME_TIMESTAMP_QUERY_COUNT> aTimestamps{};
		const VkResult Result = vkGetQueryPoolResults(m_VKDevice, m_FrameTimestampQueryPool, FrameTimestampQueryOffset(ImageIndex), FRAME_TIMESTAMP_QUERY_COUNT, sizeof(aTimestamps), aTimestamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
		if(Result != VK_SUCCESS)
			return;

		m_vFrameTimestampQueriesPending[ImageIndex] = false;

		uint64_t StartTimestamp = aTimestamps[0];
		uint64_t EndTimestamp = aTimestamps[1];
		uint64_t TimestampDelta;
		if(m_FrameTimestampValidBits < 64)
		{
			const uint64_t ValidBitsMask = (uint64_t{1} << m_FrameTimestampValidBits) - 1;
			StartTimestamp &= ValidBitsMask;
			EndTimestamp &= ValidBitsMask;
			TimestampDelta = (EndTimestamp - StartTimestamp) & ValidBitsMask;
		}
		else
		{
			TimestampDelta = EndTimestamp - StartTimestamp;
		}

		m_FrameProfileStats.m_GPUFrameTime = std::chrono::nanoseconds(static_cast<int64_t>((double)TimestampDelta * (double)m_FrameTimestampPeriod));
		m_FrameProfileStats.m_HasGPUFrameTime = true;
	}

	void BeginFrameTimestampQuery(VkCommandBuffer CommandBuffer)
	{
		if(!FrameProfilingEnabled() || !m_FrameTimestampQueriesSupported || m_FrameTimestampQueryPool == VK_NULL_HANDLE)
			return;

		const uint32_t QueryOffset = FrameTimestampQueryOffset(m_CurImageIndex);
		vkCmdResetQueryPool(CommandBuffer, m_FrameTimestampQueryPool, QueryOffset, FRAME_TIMESTAMP_QUERY_COUNT);
		vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_FrameTimestampQueryPool, QueryOffset);
		m_FrameTimestampQueryRecorded = true;
	}

	void EndFrameTimestampQuery(VkCommandBuffer CommandBuffer)
	{
		if(!m_FrameTimestampQueryRecorded || !m_FrameTimestampQueriesSupported || m_FrameTimestampQueryPool == VK_NULL_HANDLE)
			return;

		vkCmdWriteTimestamp(CommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_FrameTimestampQueryPool, FrameTimestampQueryOffset(m_CurImageIndex) + 1);
	}

	void MarkFrameTimestampQueryPending(size_t ImageIndex)
	{
		if(m_FrameTimestampQueryRecorded && m_FrameTimestampQueriesSupported && ImageIndex < m_vFrameTimestampQueriesPending.size())
			m_vFrameTimestampQueriesPending[ImageIndex] = true;
	}

	void BindVertexBuffer(size_t RenderThreadIndex, VkCommandBuffer CommandBuffer, VkBuffer Buffer, VkDeviceSize Offset)
	{
		auto &Stats = DrawProfileStats(RenderThreadIndex);
		if(RenderThreadIndex < m_vDrawCommandStates.size())
		{
			auto &State = m_vDrawCommandStates[RenderThreadIndex];
			if(State.m_VertexBuffer == Buffer && State.m_VertexBufferOffset == Offset)
			{
				Stats.m_VertexBufferBindSkips++;
				return;
			}
			State.m_VertexBuffer = Buffer;
			State.m_VertexBufferOffset = Offset;
		}

		std::array<VkBuffer, 1> aVertexBuffers = {Buffer};
		std::array<VkDeviceSize, 1> aOffsets = {Offset};
		vkCmdBindVertexBuffers(CommandBuffer, 0, 1, aVertexBuffers.data(), aOffsets.data());
		Stats.m_VertexBufferBinds++;
	}

	void BindIndexBuffer(size_t RenderThreadIndex, VkCommandBuffer CommandBuffer, VkBuffer Buffer, VkDeviceSize Offset, VkIndexType IndexType)
	{
		auto &Stats = DrawProfileStats(RenderThreadIndex);
		if(RenderThreadIndex < m_vDrawCommandStates.size())
		{
			auto &State = m_vDrawCommandStates[RenderThreadIndex];
			if(State.m_IndexBuffer == Buffer && State.m_IndexBufferOffset == Offset && State.m_IndexBufferType == IndexType)
			{
				Stats.m_IndexBufferBindSkips++;
				return;
			}
			State.m_IndexBuffer = Buffer;
			State.m_IndexBufferOffset = Offset;
			State.m_IndexBufferType = IndexType;
		}

		vkCmdBindIndexBuffer(CommandBuffer, Buffer, Offset, IndexType);
		Stats.m_IndexBufferBinds++;
	}

	void BindDescriptorSet(size_t RenderThreadIndex, VkCommandBuffer CommandBuffer, VkPipelineLayout PipeLayout, uint32_t FirstSet, const SDeviceDescriptorSet &DescrSet)
	{
		auto &Stats = DrawProfileStats(RenderThreadIndex);
		if(RenderThreadIndex < m_vDrawCommandStates.size() && FirstSet < m_vDrawCommandStates[RenderThreadIndex].m_aDescriptorSets.size())
		{
			auto &State = m_vDrawCommandStates[RenderThreadIndex];
			if(State.m_aDescriptorSets[FirstSet] == DescrSet.m_Descriptor && State.m_aDescriptorPipelineLayouts[FirstSet] == PipeLayout)
			{
				Stats.m_DescriptorBindSkips++;
				return;
			}
			State.m_aDescriptorSets[FirstSet] = DescrSet.m_Descriptor;
			State.m_aDescriptorPipelineLayouts[FirstSet] = PipeLayout;
		}

		vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PipeLayout, FirstSet, 1, &DescrSet.m_Descriptor, 0, nullptr);
		Stats.m_DescriptorBinds++;

		if(RenderThreadIndex < m_vDrawCommandStates.size())
		{
			auto &State = m_vDrawCommandStates[RenderThreadIndex];
			for(size_t SetIndex = FirstSet + 1; SetIndex < State.m_aDescriptorSets.size(); ++SetIndex)
			{
				State.m_aDescriptorSets[SetIndex] = VK_NULL_HANDLE;
				State.m_aDescriptorPipelineLayouts[SetIndex] = VK_NULL_HANDLE;
			}
		}
	}

	void DrawIndexed(size_t RenderThreadIndex, VkCommandBuffer CommandBuffer, uint32_t IndexCount, uint32_t InstanceCount, VkDeviceSize FirstIndex, int32_t VertexOffset, uint32_t FirstInstance)
	{
		DrawProfileStats(RenderThreadIndex).m_DrawCalls++;
		vkCmdDrawIndexed(CommandBuffer, IndexCount, InstanceCount, static_cast<uint32_t>(FirstIndex), VertexOffset, FirstInstance);
	}

	void Draw(size_t RenderThreadIndex, VkCommandBuffer CommandBuffer, uint32_t VertexCount, uint32_t InstanceCount, uint32_t FirstVertex, uint32_t FirstInstance)
	{
		DrawProfileStats(RenderThreadIndex).m_DrawCalls++;
		vkCmdDraw(CommandBuffer, VertexCount, InstanceCount, FirstVertex, FirstInstance);
	}

	void RecordBarrier(uint32_t MemoryBarrierCount, uint32_t BufferBarrierCount, uint32_t ImageBarrierCount)
	{
		m_FrameProfileStats.m_BarrierCalls++;
		m_FrameProfileStats.m_Barriers += MemoryBarrierCount + BufferBarrierCount + ImageBarrierCount;
	}

	VkResult FlushMappedMemoryRanges(uint32_t RangeCount, const VkMappedMemoryRange *pRanges)
	{
		m_FrameProfileStats.m_FlushCalls++;
		m_FrameProfileStats.m_FlushRanges += RangeCount;
		return vkFlushMappedMemoryRanges(m_VKDevice, RangeCount, pRanges);
	}

	VkResult InvalidateMappedMemoryRanges(uint32_t RangeCount, const VkMappedMemoryRange *pRanges)
	{
		const auto StartTime = m_FrameProfilingActive ? time_get_nanoseconds() : 0ns;
		VkResult Result = vkInvalidateMappedMemoryRanges(m_VKDevice, RangeCount, pRanges);
		if(m_FrameProfilingActive)
			m_FrameProfileStats.m_CPUInvalidateTime += time_get_nanoseconds() - StartTime;
		m_FrameProfileStats.m_InvalidateCalls++;
		m_FrameProfileStats.m_InvalidateRanges += RangeCount;
		return Result;
	}

	VkResult FlushMergedMappedMemoryRanges(uint32_t RangeCount, const VkMappedMemoryRange *pRanges)
	{
		if(RangeCount <= 1)
			return FlushMappedMemoryRanges(RangeCount, pRanges);

		m_vFrameProfileMergedFlushRanges.assign(pRanges, pRanges + RangeCount);
		std::sort(m_vFrameProfileMergedFlushRanges.begin(), m_vFrameProfileMergedFlushRanges.end(), [](const VkMappedMemoryRange &Left, const VkMappedMemoryRange &Right) {
			if(Left.memory != Right.memory)
				return std::less<VkDeviceMemory>{}(Left.memory, Right.memory);
			if(Left.offset != Right.offset)
				return Left.offset < Right.offset;
			return Left.size < Right.size;
		});

		size_t MergedCount = 0;
		for(const auto &Range : m_vFrameProfileMergedFlushRanges)
		{
			if(Range.size == 0)
				continue;

			if(MergedCount == 0)
			{
				m_vFrameProfileMergedFlushRanges[MergedCount++] = Range;
				continue;
			}

			auto &LastRange = m_vFrameProfileMergedFlushRanges[MergedCount - 1];
			const bool SameMemory = LastRange.memory == Range.memory;
			const bool LastWholeRange = LastRange.size == VK_WHOLE_SIZE;
			const bool RangeWholeRange = Range.size == VK_WHOLE_SIZE;
			const bool CanMergeWholeRange = SameMemory && (LastWholeRange || RangeWholeRange);
			const VkDeviceSize LastRangeEnd = LastWholeRange ? VK_WHOLE_SIZE : LastRange.offset + LastRange.size;
			const bool CanMergeSizedRange = SameMemory && !LastWholeRange && !RangeWholeRange && Range.offset <= LastRangeEnd;
			if(CanMergeWholeRange)
			{
				LastRange.offset = std::min(LastRange.offset, Range.offset);
				LastRange.size = VK_WHOLE_SIZE;
			}
			else if(CanMergeSizedRange)
			{
				const VkDeviceSize RangeEnd = Range.offset + Range.size;
				if(RangeEnd > LastRangeEnd)
					LastRange.size += RangeEnd - LastRangeEnd;
			}
			else
			{
				m_vFrameProfileMergedFlushRanges[MergedCount++] = Range;
			}
		}

		if(MergedCount == 0)
			return VK_SUCCESS;

		return FlushMappedMemoryRanges(static_cast<uint32_t>(MergedCount), m_vFrameProfileMergedFlushRanges.data());
	}

	VkResult QueueSubmit(VkQueue Queue, uint32_t SubmitCount, const VkSubmitInfo *pSubmits, VkFence Fence)
	{
		const auto StartTime = m_FrameProfilingActive ? time_get_nanoseconds() : 0ns;
		VkResult Result = vkQueueSubmit(Queue, SubmitCount, pSubmits, Fence);
		if(m_FrameProfilingActive)
			m_FrameProfileStats.m_CPUQueueSubmitTime += time_get_nanoseconds() - StartTime;
		m_FrameProfileStats.m_QueueSubmits += SubmitCount;
		for(uint32_t SubmitIndex = 0; SubmitIndex < SubmitCount; ++SubmitIndex)
			m_FrameProfileStats.m_QueueSubmitCommandBuffers += pSubmits[SubmitIndex].commandBufferCount;
		return Result;
	}

	VkResult WaitForFences(uint32_t FenceCount, const VkFence *pFences, VkBool32 WaitAll, uint64_t Timeout)
	{
		const auto StartTime = m_FrameProfilingActive ? time_get_nanoseconds() : 0ns;
		VkResult Result = vkWaitForFences(m_VKDevice, FenceCount, pFences, WaitAll, Timeout);
		if(m_FrameProfilingActive)
			m_FrameProfileStats.m_CPUFenceWaitTime += time_get_nanoseconds() - StartTime;
		m_FrameProfileStats.m_FenceWaits += FenceCount;
		return Result;
	}

	VkResult QueueWaitIdle(VkQueue Queue)
	{
		const auto StartTime = m_FrameProfilingActive ? time_get_nanoseconds() : 0ns;
		VkResult Result = vkQueueWaitIdle(Queue);
		if(m_FrameProfilingActive)
			m_FrameProfileStats.m_CPUQueueWaitTime += time_get_nanoseconds() - StartTime;
		m_FrameProfileStats.m_QueueWaits++;
		return Result;
	}

	VkResult DeviceWaitIdle()
	{
		const auto StartTime = m_FrameProfilingActive ? time_get_nanoseconds() : 0ns;
		VkResult Result = vkDeviceWaitIdle(m_VKDevice);
		if(m_FrameProfilingActive)
			m_FrameProfileStats.m_CPUDeviceWaitTime += time_get_nanoseconds() - StartTime;
		m_FrameProfileStats.m_DeviceWaits++;
		return Result;
	}

	/************************
	 * COMMAND IMPLEMENTATION
	 ************************/
	template<typename TName>
	[[nodiscard]] static bool IsInCommandRange(TName CMD, TName Min, TName Max)
	{
		return CMD >= Min && CMD < Max;
	}

	[[nodiscard]] ERunCommandReturnTypes RunCommand(const CCommandBuffer::SCommand *pBaseCommand) override
	{
		if(m_HasError)
		{
			// ignore all further commands
			return ERunCommandReturnTypes::RUN_COMMAND_COMMAND_ERROR;
		}

		if(IsInCommandRange<decltype(pBaseCommand->m_Cmd)>(pBaseCommand->m_Cmd, CCommandBuffer::CMD_FIRST, CCommandBuffer::CMD_COUNT))
		{
			auto &CallbackObj = m_aCommandCallbacks[CommandBufferCMDOff(CCommandBuffer::ECommandBufferCMD(pBaseCommand->m_Cmd))];
			SRenderCommandExecuteBuffer Buffer;
			Buffer.m_Command = (CCommandBuffer::ECommandBufferCMD)pBaseCommand->m_Cmd;
			Buffer.m_pRawCommand = pBaseCommand;
			Buffer.m_ThreadIndex = 0;

			if(m_CurCommandInPipe + 1 == m_CommandsInPipe)
			{
				m_LastCommandsInPipeThreadIndex = std::numeric_limits<decltype(m_LastCommandsInPipeThreadIndex)>::max();
			}

			bool CanStartThread = false;
			if(CallbackObj.m_IsRenderCommand)
			{
				m_FrameProfileStats.m_RenderCommands++;
				bool ForceSingleThread = m_ForceSingleThreadedRender || m_LastCommandsInPipeThreadIndex == std::numeric_limits<decltype(m_LastCommandsInPipeThreadIndex)>::max();

				if(!ForceSingleThread)
				{
					size_t PotentiallyNextThread = (((m_CurCommandInPipe * (m_ThreadCount - 1)) / m_CommandsInPipe) + 1);
					if(PotentiallyNextThread - 1 > m_LastCommandsInPipeThreadIndex)
					{
						CanStartThread = true;
						m_LastCommandsInPipeThreadIndex = PotentiallyNextThread - 1;
					}
					Buffer.m_ThreadIndex = m_ThreadCount > 1 ? (m_LastCommandsInPipeThreadIndex + 1) : 0;
				}
				else
				{
					Buffer.m_ThreadIndex = 0;
				}
				if(m_FrameProfilingActive)
				{
					const auto PrepareStartTime = time_get_nanoseconds();
					CallbackObj.m_FillExecuteBuffer(Buffer, pBaseCommand);
					m_FrameProfileStats.m_CPUCommandPrepareTime += time_get_nanoseconds() - PrepareStartTime;
				}
				else
					CallbackObj.m_FillExecuteBuffer(Buffer, pBaseCommand);
				m_FrameProfileStats.m_CommandPrepares++;
				m_CurRenderCallCountInPipe += Buffer.m_EstimatedRenderCallCount;
			}
			bool Ret = true;
			if(!CallbackObj.m_IsRenderCommand || (Buffer.m_ThreadIndex == 0 && !m_RenderingPaused))
			{
				Ret = CallbackObj.m_CMDIsHandled;
				const auto RecordStartTime = m_FrameProfilingActive ? time_get_nanoseconds() : 0ns;
				if(!CallbackObj.m_CommandCB(pBaseCommand, Buffer))
				{
					if(m_FrameProfilingActive)
						m_FrameProfileStats.m_CPUMainCommandRecordTime += time_get_nanoseconds() - RecordStartTime;
					m_FrameProfileStats.m_MainCommandRecords++;
					// an error occurred, stop this command and ignore all further commands
					return ERunCommandReturnTypes::RUN_COMMAND_COMMAND_ERROR;
				}
				if(m_FrameProfilingActive)
					m_FrameProfileStats.m_CPUMainCommandRecordTime += time_get_nanoseconds() - RecordStartTime;
				m_FrameProfileStats.m_MainCommandRecords++;
			}
			else if(!m_RenderingPaused)
			{
				if(CanStartThread)
				{
					StartRenderThread(m_LastCommandsInPipeThreadIndex - 1);
				}
				m_vvThreadCommandLists[Buffer.m_ThreadIndex - 1].push_back(Buffer);
			}

			++m_CurCommandInPipe;
			return Ret ? ERunCommandReturnTypes::RUN_COMMAND_COMMAND_HANDLED : ERunCommandReturnTypes::RUN_COMMAND_COMMAND_UNHANDLED;
		}

		if(m_CurCommandInPipe + 1 == m_CommandsInPipe)
		{
			m_LastCommandsInPipeThreadIndex = std::numeric_limits<decltype(m_LastCommandsInPipeThreadIndex)>::max();
		}
		++m_CurCommandInPipe;

		switch(pBaseCommand->m_Cmd)
		{
		case CCommandProcessorFragment_GLBase::CMD_INIT:
			if(!Cmd_Init(static_cast<const SCommand_Init *>(pBaseCommand)))
			{
				SetWarningPreMsg("Could not initialize Vulkan: ");
				return RUN_COMMAND_COMMAND_WARNING;
			}
			break;
		case CCommandProcessorFragment_GLBase::CMD_SHUTDOWN:
			if(!Cmd_Shutdown(static_cast<const SCommand_Shutdown *>(pBaseCommand)))
			{
				SetWarningPreMsg("Could not shutdown Vulkan: ");
				return RUN_COMMAND_COMMAND_WARNING;
			}
			break;

		case CCommandProcessorFragment_GLBase::CMD_PRE_INIT:
			if(!Cmd_PreInit(static_cast<const CCommandProcessorFragment_GLBase::SCommand_PreInit *>(pBaseCommand)))
			{
				SetWarningPreMsg("Could not initialize Vulkan: ");
				return RUN_COMMAND_COMMAND_WARNING;
			}
			break;
		case CCommandProcessorFragment_GLBase::CMD_POST_SHUTDOWN:
			if(!Cmd_PostShutdown(static_cast<const CCommandProcessorFragment_GLBase::SCommand_PostShutdown *>(pBaseCommand)))
			{
				SetWarningPreMsg("Could not shutdown Vulkan: ");
				return RUN_COMMAND_COMMAND_WARNING;
			}
			break;
		default:
			return ERunCommandReturnTypes::RUN_COMMAND_COMMAND_UNHANDLED;
		}

		return ERunCommandReturnTypes::RUN_COMMAND_COMMAND_HANDLED;
	}

	[[nodiscard]] bool Cmd_Init(const SCommand_Init *pCommand)
	{
		m_pBackendCapabilities = pCommand->m_pCapabilities;
		pCommand->m_pCapabilities->m_MediaIslandSdf = false;
		pCommand->m_pCapabilities->m_RoundedRectSdf = false;
		pCommand->m_pCapabilities->m_TexturedMsdf.store(false, std::memory_order_release);
		pCommand->m_pCapabilities->m_RenderTargetGaussianBlur = false;
		pCommand->m_pCapabilities->m_BackbufferCapture = false;
		pCommand->m_pCapabilities->m_RenderTargetExternalPassRequiresSingleSample = true;
		pCommand->m_pCapabilities->m_TileBuffering = true;
		pCommand->m_pCapabilities->m_QuadBuffering = true;
		pCommand->m_pCapabilities->m_TextBuffering = true;
		pCommand->m_pCapabilities->m_QuadContainerBuffering = true;
		pCommand->m_pCapabilities->m_ShaderSupport = true;
		m_MultiSamplingCount = (g_Config.m_GfxFsaaSamples & 0xFFFFFFFE); // ignore the uneven bit, only even multi sampling works
		pCommand->m_pCapabilities->m_RenderTargets = false;

		pCommand->m_pCapabilities->m_MipMapping = true;
		pCommand->m_pCapabilities->m_3DTextures = false;
		pCommand->m_pCapabilities->m_2DArrayTextures = true;
		pCommand->m_pCapabilities->m_NPOTTextures = true;

		pCommand->m_pCapabilities->m_ContextMajor = (int)VK_API_VERSION_MAJOR(m_EffectiveApiVersion);
		pCommand->m_pCapabilities->m_ContextMinor = (int)VK_API_VERSION_MINOR(m_EffectiveApiVersion);
		pCommand->m_pCapabilities->m_ContextPatch = (int)VK_API_VERSION_PATCH(m_EffectiveApiVersion);
		pCommand->m_pCapabilities->m_DetectedContextMajor = 0;
		pCommand->m_pCapabilities->m_DetectedContextMinor = 0;
		pCommand->m_pCapabilities->m_DetectedContextPatch = 0;

		pCommand->m_pCapabilities->m_TrianglesAsQuads = true;

		m_GlobalTextureLodBIAS = g_Config.m_GfxGLTextureLODBIAS;
		m_pTextureMemoryUsage = pCommand->m_pTextureMemoryUsage;
		m_pBufferMemoryUsage = pCommand->m_pBufferMemoryUsage;
		m_pStreamMemoryUsage = pCommand->m_pStreamMemoryUsage;
		m_pStagingMemoryUsage = pCommand->m_pStagingMemoryUsage;

		*pCommand->m_pReadPresentedImageDataFunc = [this](uint32_t &Width, uint32_t &Height, CImageInfo::EImageFormat &Format, std::vector<uint8_t> &vDstData) {
			return GetPresentedImageData(Width, Height, Format, vDstData);
		};

		m_pWindow = pCommand->m_pWindow;

		*pCommand->m_pInitError = m_VKInstance != VK_NULL_HANDLE ? 0 : -1;

		if(m_VKInstance == VK_NULL_HANDLE)
		{
			*pCommand->m_pInitError = -2;
			return false;
		}

		m_pStorage = pCommand->m_pStorage;
		if(InitVulkan<true>() != 0)
		{
			*pCommand->m_pInitError = -2;
			return false;
		}
		pCommand->m_pCapabilities->m_ContextMajor = (int)VK_API_VERSION_MAJOR(m_EffectiveApiVersion);
		pCommand->m_pCapabilities->m_ContextMinor = (int)VK_API_VERSION_MINOR(m_EffectiveApiVersion);
		pCommand->m_pCapabilities->m_ContextPatch = (int)VK_API_VERSION_PATCH(m_EffectiveApiVersion);
		pCommand->m_pCapabilities->m_DetectedContextMajor = pCommand->m_pCapabilities->m_ContextMajor;
		pCommand->m_pCapabilities->m_DetectedContextMinor = pCommand->m_pCapabilities->m_ContextMinor;
		pCommand->m_pCapabilities->m_DetectedContextPatch = pCommand->m_pCapabilities->m_ContextPatch;
		pCommand->m_pCapabilities->m_MediaIslandSdf = true;
		pCommand->m_pCapabilities->m_RoundedRectSdf = true;
		SyncTexturedMsdfCapability();
		pCommand->m_pCapabilities->m_RenderTargets = SupportsRenderTargetReadback();
		pCommand->m_pCapabilities->m_RenderTargetGaussianBlur = SupportsRenderTargetGaussianBlur();
		pCommand->m_pCapabilities->m_BackbufferCapture = SupportsBackbufferCapture();
		pCommand->m_pCapabilities->m_pRenderTargetSupportReason = RenderTargetReadbackSupportReason();

		std::array<uint32_t, (size_t)CCommandBuffer::MAX_VERTICES / 4 * 6> aIndices;
		int Primq = 0;
		for(int i = 0; i < CCommandBuffer::MAX_VERTICES / 4 * 6; i += 6)
		{
			aIndices[i] = Primq;
			aIndices[i + 1] = Primq + 1;
			aIndices[i + 2] = Primq + 2;
			aIndices[i + 3] = Primq;
			aIndices[i + 4] = Primq + 2;
			aIndices[i + 5] = Primq + 3;
			Primq += 4;
		}

		if(!PrepareFrame())
			return false;
		if(m_HasError)
		{
			*pCommand->m_pInitError = -2;
			return false;
		}

		if(!CreateIndexBuffer(aIndices.data(), sizeof(uint32_t) * aIndices.size(), m_IndexBuffer, m_IndexBufferMemory))
		{
			*pCommand->m_pInitError = -2;
			return false;
		}
		if(!CreateIndexBuffer(aIndices.data(), sizeof(uint32_t) * aIndices.size(), m_RenderIndexBuffer, m_RenderIndexBufferMemory))
		{
			*pCommand->m_pInitError = -2;
			return false;
		}
		m_CurRenderIndexPrimitiveCount = CCommandBuffer::MAX_VERTICES / 4;

		m_CanAssert = true;

		return true;
	}

	[[nodiscard]] bool Cmd_Shutdown(const SCommand_Shutdown *pCommand)
	{
		VkResult WaitIdleResult = DeviceWaitIdle();
		if(WaitIdleResult != VK_SUCCESS)
		{
			const char *pCritErrorMsg = CheckVulkanCriticalError(WaitIdleResult);
			if(pCritErrorMsg != nullptr)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Waiting for device idle during shutdown failed.", pCritErrorMsg);
			else
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Waiting for device idle during shutdown failed.");
			return false;
		}

		DestroyIndexBuffer(m_IndexBuffer, m_IndexBufferMemory);
		DestroyIndexBuffer(m_RenderIndexBuffer, m_RenderIndexBufferMemory);

		CleanupVulkan<true>(m_SwapChainImageCount);
		SyncTexturedMsdfCapability();
		m_pBackendCapabilities = nullptr;

		return true;
	}

	[[nodiscard]] bool Cmd_Texture_Destroy(const CCommandBuffer::SCommand_Texture_Destroy *pCommand)
	{
		size_t ImageIndex = (size_t)pCommand->m_Slot;
		auto &Texture = m_vTextures[ImageIndex];

		m_vvFrameDelayedTextureCleanup[m_CurImageIndex].push_back(Texture);

		Texture = CTexture{};

		return true;
	}

	[[nodiscard]] bool Cmd_Texture_Create(const CCommandBuffer::SCommand_Texture_Create *pCommand)
	{
		int Slot = pCommand->m_Slot;
		int Width = pCommand->m_Width;
		int Height = pCommand->m_Height;
		int Flags = pCommand->m_Flags;
		uint8_t *pData = pCommand->m_pData;

		if(!CreateTextureCMD(Slot, Width, Height, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, Flags, pData))
		{
			free(pData);
			return false;
		}

		free(pData);

		return true;
	}

	[[nodiscard]] bool Cmd_Texture_Update(const CCommandBuffer::SCommand_Texture_Update *pCommand)
	{
		size_t IndexTex = pCommand->m_Slot;
		uint8_t *pData = pCommand->m_pData;

		if(!UpdateTexture(IndexTex, VK_FORMAT_R8G8B8A8_UNORM, pData, pCommand->m_X, pCommand->m_Y, pCommand->m_Width, pCommand->m_Height))
		{
			free(pData);
			return false;
		}

		free(pData);

		return true;
	}

	[[nodiscard]] bool Cmd_RenderTarget_Create(const CCommandBuffer::SCommand_RenderTarget_Create *pCommand)
	{
		if(pCommand->m_TargetId < 0 || pCommand->m_Width <= 0 || pCommand->m_Height <= 0 || m_VKRenderTargetRenderPass == VK_NULL_HANDLE)
			return true;
		const size_t TargetId = (size_t)pCommand->m_TargetId;
		while(TargetId >= m_vRenderTargets.size())
			m_vRenderTargets.resize((m_vRenderTargets.size() * 2) + 1);

		SRenderTarget &Target = m_vRenderTargets[TargetId];
		DestroyRenderTarget(Target);
		Target.m_Width = pCommand->m_Width;
		Target.m_Height = pCommand->m_Height;
		if(!CreateImage(Target.m_Width, Target.m_Height, 1, 1, RenderTargetReadbackFormat(), VK_IMAGE_TILING_OPTIMAL, Target.m_Image, Target.m_ImageMem, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT))
		{
			DestroyRenderTarget(Target);
			return false;
		}
		Target.m_ImageView = CreateImageView(Target.m_Image, RenderTargetReadbackFormat(), VK_IMAGE_VIEW_TYPE_2D, 1, 1);
		if(Target.m_ImageView == VK_NULL_HANDLE)
		{
			DestroyRenderTarget(Target);
			return false;
		}

		VkFramebufferCreateInfo FramebufferInfo{};
		FramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		FramebufferInfo.renderPass = m_VKRenderTargetRenderPass;
		FramebufferInfo.attachmentCount = 1;
		FramebufferInfo.pAttachments = &Target.m_ImageView;
		FramebufferInfo.width = Target.m_Width;
		FramebufferInfo.height = Target.m_Height;
		FramebufferInfo.layers = 1;
		if(vkCreateFramebuffer(m_VKDevice, &FramebufferInfo, nullptr, &Target.m_Framebuffer) != VK_SUCCESS)
		{
			DestroyRenderTarget(Target);
			return false;
		}
		if(!CreateRenderTargetDescriptorSet(Target, 0) || !CreateRenderTargetDescriptorSet(Target, 1))
		{
			DestroyRenderTarget(Target);
			return false;
		}
		return true;
	}

	[[nodiscard]] bool Cmd_RenderTarget_Destroy(const CCommandBuffer::SCommand_RenderTarget_Destroy *pCommand)
	{
		if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size())
			return true;
		if(m_RenderingPaused)
		{
			// 渲染暂停时 GPU 已空闲且无活跃录制，直接销毁即可，
			// 避免对已结束且已提交的交换链渲染通道再次调用 vkCmdEndRenderPass
			DestroyRenderTarget(m_vRenderTargets[pCommand->m_TargetId]);
			return true;
		}
		if(m_RenderTargetActive)
		{
			// 渲染目标渲染通道进行中：既不能中途提交当前命令缓冲（会打断渲染通道），
			// 也不能在渲染通道中间结束命令缓冲，延迟到 Cmd_RenderTarget_End 后统一销毁
			if(m_ActiveRenderTargetId == pCommand->m_TargetId)
				return true;
			m_vPendingRenderTargetDestroy.emplace_back(pCommand->m_TargetId, m_vRenderTargets[pCommand->m_TargetId].m_Image);
			return true;
		}
		if(m_vRenderTargets[pCommand->m_TargetId].m_Image != VK_NULL_HANDLE && !SubmitCurrentCommandsAndRestartSwapPass())
			return false;
		DestroyRenderTarget(m_vRenderTargets[pCommand->m_TargetId]);
		return true;
	}

	[[nodiscard]] bool Cmd_RenderTarget_Begin(const CCommandBuffer::SCommand_RenderTarget_Begin *pCommand)
	{
		if(m_RenderingPaused)
			return true;
		if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size() || m_RenderTargetActive)
			return true;
		SRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
		if(Target.m_Framebuffer == VK_NULL_HANDLE)
			return true;
		EndSwapRenderPassForExternalWork();
		auto &CommandBuffer = GetMainGraphicCommandBuffer();
		if(Target.m_Layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && !ImageBarrier(CommandBuffer, Target.m_Image, 0, 1, 0, 1, RenderTargetReadbackFormat(), Target.m_Layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL))
			return false;
		Target.m_Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkRenderPassBeginInfo RenderPassInfo{};
		RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		RenderPassInfo.renderPass = m_VKRenderTargetRenderPass;
		RenderPassInfo.framebuffer = Target.m_Framebuffer;
		RenderPassInfo.renderArea.offset = {0, 0};
		RenderPassInfo.renderArea.extent = {Target.m_Width, Target.m_Height};
		VkClearValue ClearColorVal = {{{pCommand->m_ClearColor.r, pCommand->m_ClearColor.g, pCommand->m_ClearColor.b, pCommand->m_ClearColor.a}}};
		RenderPassInfo.clearValueCount = 1;
		RenderPassInfo.pClearValues = &ClearColorVal;
		vkCmdBeginRenderPass(CommandBuffer, &RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		m_SavedHasDynamicViewport = m_HasDynamicViewport;
		m_SavedDynamicViewportOffset = m_DynamicViewportOffset;
		m_SavedDynamicViewportSize = m_DynamicViewportSize;
		m_HasDynamicViewport = true;
		m_DynamicViewportOffset = {0, 0};
		m_DynamicViewportSize = {Target.m_Width, Target.m_Height};
		m_RenderTargetActive = true;
		m_ActiveRenderTargetId = pCommand->m_TargetId;
		for(auto &LastPipe : m_vLastPipeline)
			LastPipe = VK_NULL_HANDLE;
		return true;
	}

	[[nodiscard]] bool Cmd_RenderTarget_End(const CCommandBuffer::SCommand_RenderTarget_End *pCommand)
	{
		(void)pCommand;
		if(m_RenderingPaused)
			return true;
		if(!m_RenderTargetActive || m_ActiveRenderTargetId < 0 || (size_t)m_ActiveRenderTargetId >= m_vRenderTargets.size())
			return true;
		auto &CommandBuffer = GetMainGraphicCommandBuffer();
		vkCmdEndRenderPass(CommandBuffer);
		SRenderTarget &Target = m_vRenderTargets[m_ActiveRenderTargetId];
		Target.m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		m_RenderTargetActive = false;
		m_ActiveRenderTargetId = -1;
		m_HasDynamicViewport = m_SavedHasDynamicViewport;
		m_DynamicViewportOffset = m_SavedDynamicViewportOffset;
		m_DynamicViewportSize = m_SavedDynamicViewportSize;
		BeginSwapRenderPass(m_VKRenderPassLoad);

		// 处理渲染目标渲染通道期间收到的销毁请求：先提交并等待 GPU 完成，再逐个销毁
		if(!m_vPendingRenderTargetDestroy.empty())
		{
			if(!SubmitCurrentCommandsAndRestartSwapPass())
				return false;
			for(const auto &PendingDestroy : m_vPendingRenderTargetDestroy)
			{
				if(PendingDestroy.first < m_vRenderTargets.size() && PendingDestroy.second != VK_NULL_HANDLE &&
					m_vRenderTargets[PendingDestroy.first].m_Image == PendingDestroy.second)
					DestroyRenderTarget(m_vRenderTargets[PendingDestroy.first]);
			}
			m_vPendingRenderTargetDestroy.clear();
		}
		return true;
	}

	[[nodiscard]] bool Cmd_RenderTarget_Readback(const CCommandBuffer::SCommand_RenderTarget_Readback *pCommand)
	{
		if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size() || pCommand->m_pImage == nullptr)
			return true;
		if(m_RenderingPaused)
			return true;
		SRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
		if(Target.m_Image == VK_NULL_HANDLE || Target.m_Width == 0 || Target.m_Height == 0)
			return true;
		if(!SubmitCurrentCommandsAndRestartSwapPass())
			return false;

		uint8_t *pResImageData;
		if(!PreparePresentedImageDataImage(pResImageData, Target.m_Width, Target.m_Height))
			return false;
		VkCommandBuffer *pCommandBuffer;
		if(!GetMemoryCommandBuffer(pCommandBuffer))
			return false;
		VkCommandBuffer &CommandBuffer = *pCommandBuffer;
		if(!ImageBarrier(CommandBuffer, Target.m_Image, 0, 1, 0, 1, RenderTargetReadbackFormat(), Target.m_Layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL))
			return false;
		Target.m_Layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		if(!ImageBarrier(CommandBuffer, m_GetPresentedImgDataHelperImage, 0, 1, 0, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
			return false;

		bool IsB8G8R8A8 = RenderTargetReadbackFormat() == VK_FORMAT_B8G8R8A8_UNORM || RenderTargetReadbackFormat() == VK_FORMAT_B8G8R8A8_SRGB;
		const bool IsR8G8B8A8 = RenderTargetReadbackFormat() == VK_FORMAT_R8G8B8A8_UNORM || RenderTargetReadbackFormat() == VK_FORMAT_R8G8B8A8_SRGB;
		if((IsR8G8B8A8 || IsB8G8R8A8) && m_OptimalSwapChainImageBlitting && m_OptimalRGBAImageBlitting && m_LinearRGBAImageBlitting)
		{
			VkImageBlit ImageBlitRegion{};
			ImageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ImageBlitRegion.srcSubresource.layerCount = 1;
			ImageBlitRegion.srcOffsets[1] = {(int32_t)Target.m_Width, (int32_t)Target.m_Height, 1};
			ImageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ImageBlitRegion.dstSubresource.layerCount = 1;
			ImageBlitRegion.dstOffsets[1] = {(int32_t)Target.m_Width, (int32_t)Target.m_Height, 1};
			vkCmdBlitImage(CommandBuffer, Target.m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_GetPresentedImgDataHelperImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ImageBlitRegion, VK_FILTER_NEAREST);
			IsB8G8R8A8 = false;
		}
		else
		{
			VkImageCopy ImageCopyRegion{};
			ImageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ImageCopyRegion.srcSubresource.layerCount = 1;
			ImageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ImageCopyRegion.dstSubresource.layerCount = 1;
			ImageCopyRegion.extent = {Target.m_Width, Target.m_Height, 1};
			vkCmdCopyImage(CommandBuffer, Target.m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_GetPresentedImgDataHelperImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ImageCopyRegion);
		}
		if(!ImageBarrier(CommandBuffer, m_GetPresentedImgDataHelperImage, 0, 1, 0, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL))
			return false;
		if(!ImageBarrier(CommandBuffer, Target.m_Image, 0, 1, 0, 1, RenderTargetReadbackFormat(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
			return false;
		Target.m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkEndCommandBuffer(CommandBuffer);
		m_vUsedMemoryCommandBuffer[m_CurImageIndex] = false;
		VkSubmitInfo SubmitInfo{};
		SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		SubmitInfo.commandBufferCount = 1;
		SubmitInfo.pCommandBuffers = &CommandBuffer;
		VkResult ResetFenceResult = vkResetFences(m_VKDevice, 1, &m_GetPresentedImgDataHelperFence);
		if(ResetFenceResult != VK_SUCCESS)
		{
			const char *pErr = CheckVulkanCriticalError(ResetFenceResult);
			if(pErr != nullptr)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Resetting render target readback fence failed: %s.", pErr);
			else
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Resetting render target readback fence failed.");
			return false;
		}
		VkResult QueueSubmitResult = QueueSubmit(m_VKGraphicsQueue, 1, &SubmitInfo, m_GetPresentedImgDataHelperFence);
		if(QueueSubmitResult != VK_SUCCESS)
		{
			const char *pErr = CheckVulkanCriticalError(QueueSubmitResult);
			if(pErr != nullptr)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting render target readback command buffer failed: %s.", pErr);
			else
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_SUBMIT_FAILED, "Submitting render target readback command buffer failed.");
			return false;
		}
		VkResult WaitForFencesResult = WaitForFences(1, &m_GetPresentedImgDataHelperFence, VK_TRUE, std::numeric_limits<uint64_t>::max());
		if(WaitForFencesResult != VK_SUCCESS)
		{
			const char *pErr = CheckVulkanCriticalError(WaitForFencesResult);
			if(pErr != nullptr)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Waiting for render target readback fence failed: %s.", pErr);
			else
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Waiting for render target readback fence failed.");
			return false;
		}

		VkMappedMemoryRange MemRange{};
		MemRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		MemRange.memory = m_GetPresentedImgDataHelperMem.m_Mem;
		MemRange.offset = m_GetPresentedImgDataHelperMappedLayoutOffset;
		MemRange.size = VK_WHOLE_SIZE;
		VkResult InvalidateResult = InvalidateMappedMemoryRanges(1, &MemRange);
		if(InvalidateResult != VK_SUCCESS)
		{
			const char *pErr = CheckVulkanCriticalError(InvalidateResult);
			if(pErr != nullptr)
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Invalidating render target readback memory failed: %s.", pErr);
			else
				SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_CMD_FAILED, "Invalidating render target readback memory failed.");
			return false;
		}

		const size_t PixelSize = 4;
		const size_t ImageSize = (size_t)Target.m_Width * Target.m_Height * PixelSize;
		auto *pPixels = static_cast<uint8_t *>(malloc(ImageSize));
		if(pPixels == nullptr)
			return false;
		for(uint32_t Y = 0; Y < Target.m_Height; ++Y)
		{
			mem_copy(pPixels + (size_t)Y * Target.m_Width * PixelSize, pResImageData + (size_t)Y * m_GetPresentedImgDataHelperMappedLayoutPitch, (size_t)Target.m_Width * PixelSize);
		}
		if(IsB8G8R8A8)
		{
			for(size_t Pixel = 0; Pixel < (size_t)Target.m_Width * Target.m_Height; ++Pixel)
				std::swap(pPixels[Pixel * 4], pPixels[Pixel * 4 + 2]);
		}
		pCommand->m_pImage->m_Width = Target.m_Width;
		pCommand->m_pImage->m_Height = Target.m_Height;
		pCommand->m_pImage->m_Format = CImageInfo::FORMAT_RGBA;
		pCommand->m_pImage->m_pData = pPixels;
		return true;
	}

	[[nodiscard]] bool Cmd_RenderTarget_Draw(const CCommandBuffer::SCommand_RenderTarget_Draw *pCommand)
	{
		if(m_RenderingPaused)
			return true;
		if(pCommand->m_TargetId < 0 || (size_t)pCommand->m_TargetId >= m_vRenderTargets.size() || pCommand->m_W <= 0.0f || pCommand->m_H <= 0.0f || pCommand->m_pVertices == nullptr || pCommand->m_PrimCount == 0)
			return true;
		SRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
		if(Target.m_Image == VK_NULL_HANDLE || Target.m_aVKStandardTexturedDescrSets[0].m_Descriptor == VK_NULL_HANDLE)
			return true;
		FinishRenderThreads();
		auto &CommandBuffer = GetMainGraphicCommandBuffer();
		ExecutePendingRenderThreadCommandBuffers(CommandBuffer);
		if(Target.m_Layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			if(!ImageBarrier(CommandBuffer, Target.m_Image, 0, 1, 0, 1, RenderTargetReadbackFormat(), Target.m_Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
				return false;
			Target.m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		SRenderCommandExecuteBuffer ExecBuffer;
		ExecBuffer.m_ThreadIndex = 0;
		ExecBufferFillDynamicStates(pCommand->m_State, ExecBuffer);
		const size_t BlendModeIndex = GetBlendModeIndex(pCommand->m_State);
		const size_t DynamicIndex = GetDynamicModeIndexFromExecBuffer(ExecBuffer);
		const size_t AddressModeIndex = GetAddressModeIndex(pCommand->m_State);
		auto &PipeLayout = GetStandardPipeLayout(false, true, BlendModeIndex, DynamicIndex);
		auto &PipeLine = GetStandardPipe(false, true, BlendModeIndex, DynamicIndex);
		BindPipeline(0, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

		VkBuffer VKBuffer;
		SDeviceMemoryBlock VKBufferMem;
		size_t BufferOff = 0;
		if(!CreateStreamVertexBuffer(0, VKBuffer, VKBufferMem, BufferOff, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex) * pCommand->m_PrimCount * 4))
			return false;
		std::array<VkBuffer, 1> aVertexBuffers = {VKBuffer};
		std::array<VkDeviceSize, 1> aOffsets = {(VkDeviceSize)BufferOff};
		vkCmdBindVertexBuffers(CommandBuffer, 0, 1, aVertexBuffers.data(), aOffsets.data());
		vkCmdBindIndexBuffer(CommandBuffer, m_RenderIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PipeLayout, 0, 1, &Target.m_aVKStandardTexturedDescrSets[AddressModeIndex].m_Descriptor, 0, nullptr);

		std::array<float, (size_t)4 * 2> m;
		GetStateMatrix(pCommand->m_State, m);
		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos), m.data());
		vkCmdDrawIndexed(CommandBuffer, pCommand->m_PrimCount * 6, 1, 0, 0, 0);
		ResetDrawCommandState(0);
		return true;
	}

	[[nodiscard]] bool Cmd_RenderTarget_CaptureBackbuffer(const CCommandBuffer::SCommand_RenderTarget_CaptureBackbuffer *pCommand)
	{
		if(m_RenderingPaused || !SupportsBackbufferCapture() || HasMultiSampling() || m_RenderTargetActive || !m_SwapRenderPassActive || pCommand->m_TargetId < 0 ||
			(size_t)pCommand->m_TargetId >= m_vRenderTargets.size() || m_CurImageIndex >= m_vSwapChainImages.size())
			return true;
		SRenderTarget &Target = m_vRenderTargets[pCommand->m_TargetId];
		if(Target.m_Image == VK_NULL_HANDLE || Target.m_Width == 0 || Target.m_Height == 0 ||
			(Target.m_Layout != VK_IMAGE_LAYOUT_UNDEFINED && Target.m_Layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
			return true;

		EndSwapRenderPassForExternalWork();
		auto &CommandBuffer = GetMainGraphicCommandBuffer();
		VkImage &SwapImage = m_vSwapChainImages[m_CurImageIndex];
		if(!ImageBarrier(CommandBuffer, SwapImage, 0, 1, 0, 1, m_VKSurfFormat.format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL))
			return false;
		if(!ImageBarrier(CommandBuffer, Target.m_Image, 0, 1, 0, 1, RenderTargetReadbackFormat(), Target.m_Layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
			return false;
		Target.m_Layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

		VkImageBlit BlitRegion{};
		BlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		BlitRegion.srcSubresource.layerCount = 1;
		const VkExtent2D SourceViewport = m_VKSwapImgAndViewportExtent.GetPresentedImageViewport();
		BlitRegion.srcOffsets[1] = {
			(int32_t)SourceViewport.width,
			(int32_t)SourceViewport.height,
			1};
		BlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		BlitRegion.dstSubresource.layerCount = 1;
		BlitRegion.dstOffsets[0] = {0, (int32_t)Target.m_Height, 0};
		BlitRegion.dstOffsets[1] = {(int32_t)Target.m_Width, 0, 1};
		vkCmdBlitImage(CommandBuffer, SwapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			Target.m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &BlitRegion,
			m_OptimalSwapChainImageLinearBlitting ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);

		if(!ImageBarrier(CommandBuffer, Target.m_Image, 0, 1, 0, 1, RenderTargetReadbackFormat(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
			return false;
		Target.m_Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		if(!ImageBarrier(CommandBuffer, SwapImage, 0, 1, 0, 1, m_VKSurfFormat.format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR))
			return false;

		BeginSwapRenderPass(m_VKRenderPassLoad);
		return true;
	}

	[[nodiscard]] bool Cmd_RenderTarget_GaussianBlurPass(const CCommandBuffer::SCommand_RenderTarget_GaussianBlurPass *pCommand)
	{
		if(m_RenderingPaused)
			return true;
		if(!m_GaussianBlurPipelineValid || HasMultiSampling() || !m_RenderTargetActive || m_ActiveRenderTargetId < 0 || pCommand->m_SourceTargetId < 0 ||
			(size_t)m_ActiveRenderTargetId >= m_vRenderTargets.size() || (size_t)pCommand->m_SourceTargetId >= m_vRenderTargets.size() ||
			pCommand->m_SourceTargetId == m_ActiveRenderTargetId || pCommand->m_Radius < 1 || pCommand->m_Radius > IGraphics::GAUSSIAN_BLUR_MAX_RADIUS)
			return true;
		SRenderTarget &Source = m_vRenderTargets[pCommand->m_SourceTargetId];
		const SRenderTarget &Destination = m_vRenderTargets[m_ActiveRenderTargetId];
		if(Source.m_Image == VK_NULL_HANDLE || Source.m_Layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
			Source.m_Width != Destination.m_Width || Source.m_Height != Destination.m_Height ||
			Source.m_aVKStandardTexturedDescrSets[VULKAN_BACKEND_ADDRESS_MODE_CLAMP_EDGES].m_Descriptor == VK_NULL_HANDLE)
			return true;

		FinishRenderThreads();
		auto &CommandBuffer = GetMainGraphicCommandBuffer();
		ExecutePendingRenderThreadCommandBuffers(CommandBuffer);
		CCommandBuffer::SState State{};
		State.m_Texture = -1;
		State.m_BlendMode = EBlendMode::NONE;
		State.m_ClipEnable = false;
		SRenderCommandExecuteBuffer ExecBuffer;
		ExecBuffer.m_ThreadIndex = 0;
		ExecBufferFillDynamicStates(State, ExecBuffer);
		constexpr size_t BlendModeIndex = VULKAN_BACKEND_BLEND_MODE_NONE;
		constexpr size_t DynamicIndex = VULKAN_BACKEND_CLIP_MODE_DYNAMIC_SCISSOR_AND_VIEWPORT;
		auto &PipeLayout = GetPipeLayout(m_GaussianBlurPipeline, true, BlendModeIndex, DynamicIndex);
		auto &PipeLine = GetPipeline(m_GaussianBlurPipeline, true, BlendModeIndex, DynamicIndex);
		BindPipeline(0, CommandBuffer, ExecBuffer, PipeLine, State);

		CCommandBuffer::SVertex aVertices[4]{};
		aVertices[0].m_Pos = vec2(-1.0f, -1.0f);
		aVertices[0].m_Tex = vec2(0.0f, 0.0f);
		aVertices[1].m_Pos = vec2(1.0f, -1.0f);
		aVertices[1].m_Tex = vec2(1.0f, 0.0f);
		aVertices[2].m_Pos = vec2(1.0f, 1.0f);
		aVertices[2].m_Tex = vec2(1.0f, 1.0f);
		aVertices[3].m_Pos = vec2(-1.0f, 1.0f);
		aVertices[3].m_Tex = vec2(0.0f, 1.0f);
		VkBuffer VertexBuffer;
		SDeviceMemoryBlock VertexBufferMem;
		size_t VertexBufferOffset = 0;
		if(!CreateStreamVertexBuffer(0, VertexBuffer, VertexBufferMem, VertexBufferOffset, aVertices, sizeof(aVertices)))
			return false;
		const std::array<VkBuffer, 1> aVertexBuffers = {VertexBuffer};
		const std::array<VkDeviceSize, 1> aOffsets = {(VkDeviceSize)VertexBufferOffset};
		vkCmdBindVertexBuffers(CommandBuffer, 0, 1, aVertexBuffers.data(), aOffsets.data());
		vkCmdBindIndexBuffer(CommandBuffer, m_RenderIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, PipeLayout, 0, 1,
			&Source.m_aVKStandardTexturedDescrSets[VULKAN_BACKEND_ADDRESS_MODE_CLAMP_EDGES].m_Descriptor, 0, nullptr);

		SUniformGaussianBlur PushConstants{};
		PushConstants.m_TexelOffset = vec2(
			pCommand->m_Horizontal ? 1.0f / Source.m_Width : 0.0f,
			pCommand->m_Horizontal ? 0.0f : 1.0f / Source.m_Height);
		PushConstants.m_Radius = pCommand->m_Radius;
		PushConstants.m_aWeights = pCommand->m_aWeights;
		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &PushConstants);
		vkCmdDrawIndexed(CommandBuffer, 6, 1, 0, 0, 0);
		ResetDrawCommandState(0);
		return true;
	}

	[[nodiscard]] bool Cmd_TextTextures_Create(const CCommandBuffer::SCommand_TextTextures_Create *pCommand)
	{
		int Slot = pCommand->m_Slot;
		int SlotOutline = pCommand->m_SlotOutline;
		int Width = pCommand->m_Width;
		int Height = pCommand->m_Height;

		uint8_t *pTmpData = pCommand->m_pTextData;
		uint8_t *pTmpData2 = pCommand->m_pTextOutlineData;

		if(!CreateTextureCMD(Slot, Width, Height, VK_FORMAT_R8_UNORM, VK_FORMAT_R8_UNORM, TextureFlag::NO_MIPMAPS, pTmpData))
		{
			free(pTmpData);
			free(pTmpData2);
			return false;
		}
		if(!CreateTextureCMD(SlotOutline, Width, Height, VK_FORMAT_R8_UNORM, VK_FORMAT_R8_UNORM, TextureFlag::NO_MIPMAPS, pTmpData2))
		{
			free(pTmpData);
			free(pTmpData2);
			return false;
		}

		if(!CreateNewTextDescriptorSets(Slot, SlotOutline))
		{
			free(pTmpData);
			free(pTmpData2);
			return false;
		}

		free(pTmpData);
		free(pTmpData2);

		return true;
	}

	[[nodiscard]] bool Cmd_TextTextures_Destroy(const CCommandBuffer::SCommand_TextTextures_Destroy *pCommand)
	{
		size_t ImageIndex = (size_t)pCommand->m_Slot;
		size_t ImageIndexOutline = (size_t)pCommand->m_SlotOutline;
		auto &Texture = m_vTextures[ImageIndex];
		auto &TextureOutline = m_vTextures[ImageIndexOutline];

		m_vvFrameDelayedTextTexturesCleanup[m_CurImageIndex].emplace_back(Texture, TextureOutline);

		Texture = {};
		TextureOutline = {};

		return true;
	}

	[[nodiscard]] bool Cmd_TextTexture_Update(const CCommandBuffer::SCommand_TextTexture_Update *pCommand)
	{
		size_t IndexTex = pCommand->m_Slot;
		uint8_t *pData = pCommand->m_pData;

		if(!UpdateTexture(IndexTex, VK_FORMAT_R8_UNORM, pData, pCommand->m_X, pCommand->m_Y, pCommand->m_Width, pCommand->m_Height))
		{
			free(pData);
			return false;
		}

		free(pData);

		return true;
	}

	void Cmd_Clear_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_Clear *pCommand)
	{
		if(!pCommand->m_ForceClear)
		{
			bool ColorChanged = m_aClearColor[0] != pCommand->m_Color.r || m_aClearColor[1] != pCommand->m_Color.g ||
					    m_aClearColor[2] != pCommand->m_Color.b || m_aClearColor[3] != pCommand->m_Color.a;
			m_aClearColor[0] = pCommand->m_Color.r;
			m_aClearColor[1] = pCommand->m_Color.g;
			m_aClearColor[2] = pCommand->m_Color.b;
			m_aClearColor[3] = pCommand->m_Color.a;
			if(ColorChanged)
				ExecBuffer.m_ClearColorInRenderThread = true;
		}
		else
		{
			ExecBuffer.m_ClearColorInRenderThread = true;
		}
		ExecBuffer.m_EstimatedRenderCallCount = 0;
	}

	[[nodiscard]] bool Cmd_Clear(const SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_Clear *pCommand)
	{
		if(ExecBuffer.m_ClearColorInRenderThread)
		{
			std::array<VkClearAttachment, 1> aAttachments = {VkClearAttachment{VK_IMAGE_ASPECT_COLOR_BIT, 0, VkClearValue{VkClearColorValue{{pCommand->m_Color.r, pCommand->m_Color.g, pCommand->m_Color.b, pCommand->m_Color.a}}}}};
			std::array<VkClearRect, 1> aClearRects = {VkClearRect{{{0, 0}, m_VKSwapImgAndViewportExtent.m_SwapImageViewport}, 0, 1}};

			VkCommandBuffer *pCommandBuffer;
			if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
				return false;
			auto &CommandBuffer = *pCommandBuffer;
			vkCmdClearAttachments(CommandBuffer, aAttachments.size(), aAttachments.data(), aClearRects.size(), aClearRects.data());
		}

		return true;
	}

	void Cmd_Render_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_Render *pCommand)
	{
		bool IsTextured = GetIsTextured(pCommand->m_State);
		if(IsTextured)
		{
			size_t AddressModeIndex = GetAddressModeIndex(pCommand->m_State);
			ExecBuffer.m_aDescriptors[0] = m_vTextures[pCommand->m_State.m_Texture].m_aVKStandardTexturedDescrSets[AddressModeIndex];
		}

		ExecBuffer.m_IndexBuffer = m_IndexBuffer;

		ExecBuffer.m_EstimatedRenderCallCount = 1;

		ExecBufferFillDynamicStates(pCommand->m_State, ExecBuffer);
	}

	[[nodiscard]] bool Cmd_Render(const CCommandBuffer::SCommand_Render *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
	{
		return RenderStandard<CCommandBuffer::SVertex, false>(ExecBuffer, pCommand->m_State, pCommand->m_PrimType, pCommand->m_pVertices, pCommand->m_PrimCount);
	}

	void Cmd_RenderMediaIslandSdf_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderMediaIslandSdf *pCommand)
	{
		const size_t AddressModeIndex = GetAddressModeIndex(pCommand->m_State);
		if(pCommand->m_BackdropTargetId >= 0 && (size_t)pCommand->m_BackdropTargetId < m_vRenderTargets.size() &&
			m_vRenderTargets[pCommand->m_BackdropTargetId].m_aVKStandardTexturedDescrSets[AddressModeIndex].m_Descriptor != VK_NULL_HANDLE)
		{
			ExecBuffer.m_aDescriptors[0] = m_vRenderTargets[pCommand->m_BackdropTargetId].m_aVKStandardTexturedDescrSets[AddressModeIndex];
		}
		else if(pCommand->m_State.m_Texture >= 0 && (size_t)pCommand->m_State.m_Texture < m_vTextures.size())
		{
			ExecBuffer.m_aDescriptors[0] = m_vTextures[pCommand->m_State.m_Texture].m_aVKStandardTexturedDescrSets[AddressModeIndex];
		}
		ExecBuffer.m_IndexBuffer = m_IndexBuffer;
		ExecBuffer.m_EstimatedRenderCallCount = 1;
		ExecBufferFillDynamicStates(pCommand->m_State, ExecBuffer);
	}

	void Cmd_RenderRoundedRectSdf_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderRoundedRectSdf *pCommand)
	{
		ExecBuffer.m_IndexBuffer = m_IndexBuffer;
		ExecBuffer.m_EstimatedRenderCallCount = 1;
		ExecBufferFillDynamicStates(pCommand->m_State, ExecBuffer);
	}

	void Cmd_RenderTexturedMsdf_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderTexturedMsdf *pCommand)
	{
		const size_t AddressModeIndex = GetAddressModeIndex(pCommand->m_State);
		ExecBuffer.m_aDescriptors[0] = m_vTextures[pCommand->m_State.m_Texture].m_aVKStandardTexturedDescrSets[AddressModeIndex];
		ExecBuffer.m_IndexBuffer = m_IndexBuffer;
		ExecBuffer.m_EstimatedRenderCallCount = 1;
		ExecBufferFillDynamicStates(pCommand->m_State, ExecBuffer);
	}

	[[nodiscard]] bool Cmd_RenderMediaIslandSdf(const CCommandBuffer::SCommand_RenderMediaIslandSdf *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
	{
		static_assert(sizeof(IGraphics::SMediaIslandSdfParams) <= 512 * sizeof(IGraphics::SRenderSpriteInfo));

		std::array<float, (size_t)4 * 2> m;
		GetStateMatrix(pCommand->m_State, m);

		bool IsTextured;
		size_t BlendModeIndex;
		size_t DynamicIndex;
		size_t AddressModeIndex;
		GetStateIndices(ExecBuffer, pCommand->m_State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
		if(!IsTextured)
			return false;
		(void)AddressModeIndex;
		auto &PipeLayout = GetPipeLayout(m_MediaIslandSdfPipeline, true, BlendModeIndex, DynamicIndex);
		auto &PipeLine = GetPipeline(m_MediaIslandSdfPipeline, true, BlendModeIndex, DynamicIndex);

		VkCommandBuffer *pCommandBuffer;
		if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
			return false;
		auto &CommandBuffer = *pCommandBuffer;
		BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

		VkBuffer VKBuffer;
		SDeviceMemoryBlock VKBufferMem;
		size_t BufferOff = 0;
		if(!CreateStreamVertexBuffer(ExecBuffer.m_ThreadIndex, VKBuffer, VKBufferMem, BufferOff, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex) * pCommand->m_PrimCount * 4))
			return false;
		BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, VKBuffer, (VkDeviceSize)BufferOff);
		BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, ExecBuffer.m_aDescriptors[0]);

		SDeviceDescriptorSet UniDescrSet;
		if(!GetUniformBufferObject(ExecBuffer.m_ThreadIndex, true, UniDescrSet, 1, &pCommand->m_Params, sizeof(pCommand->m_Params)))
			return false;
		BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 1, UniDescrSet);
		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos), m.data());
		DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, 6, 1, 0, 0, 0);
		return true;
	}

	[[nodiscard]] bool Cmd_RenderRoundedRectSdf(const CCommandBuffer::SCommand_RenderRoundedRectSdf *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
	{
		std::array<float, (size_t)4 * 2> m;
		GetStateMatrix(pCommand->m_State, m);

		bool IsTextured;
		size_t BlendModeIndex;
		size_t DynamicIndex;
		size_t AddressModeIndex;
		GetStateIndices(ExecBuffer, pCommand->m_State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
		(void)IsTextured;
		(void)AddressModeIndex;
		auto &PipeLayout = GetPipeLayout(m_RoundedRectSdfPipeline, false, BlendModeIndex, DynamicIndex);
		auto &PipeLine = GetPipeline(m_RoundedRectSdfPipeline, false, BlendModeIndex, DynamicIndex);

		VkCommandBuffer *pCommandBuffer;
		if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
			return false;
		auto &CommandBuffer = *pCommandBuffer;
		BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

		VkBuffer VKBuffer;
		SDeviceMemoryBlock VKBufferMem;
		size_t BufferOff = 0;
		if(!CreateStreamVertexBuffer(ExecBuffer.m_ThreadIndex, VKBuffer, VKBufferMem, BufferOff, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex) * pCommand->m_PrimCount * 4))
			return false;
		BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, VKBuffer, (VkDeviceSize)BufferOff);
		BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

		SDeviceDescriptorSet UniDescrSet;
		if(!GetUniformBufferObject(ExecBuffer.m_ThreadIndex, true, UniDescrSet, 1, &pCommand->m_Params, sizeof(pCommand->m_Params)))
			return false;
		BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, UniDescrSet);
		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos), m.data());
		DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, 6, 1, 0, 0, 0);
		return true;
	}

	[[nodiscard]] bool Cmd_RenderTexturedMsdf(const CCommandBuffer::SCommand_RenderTexturedMsdf *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
	{
		if(!m_TexturedMsdfPipelineValid)
			return true;

		std::array<float, (size_t)4 * 2> m;
		GetStateMatrix(pCommand->m_State, m);

		bool IsTextured;
		size_t BlendModeIndex;
		size_t DynamicIndex;
		size_t AddressModeIndex;
		GetStateIndices(ExecBuffer, pCommand->m_State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
		(void)IsTextured;
		(void)AddressModeIndex;
		auto &PipeLayout = GetPipeLayout(m_TexturedMsdfPipeline, true, BlendModeIndex, DynamicIndex);
		auto &PipeLine = GetPipeline(m_TexturedMsdfPipeline, true, BlendModeIndex, DynamicIndex);

		VkCommandBuffer *pCommandBuffer;
		if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
			return false;
		auto &CommandBuffer = *pCommandBuffer;
		BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

		VkBuffer VKBuffer;
		SDeviceMemoryBlock VKBufferMem;
		size_t BufferOff = 0;
		if(!CreateStreamVertexBuffer(ExecBuffer.m_ThreadIndex, VKBuffer, VKBufferMem, BufferOff, pCommand->m_pVertices, sizeof(CCommandBuffer::SVertex) * pCommand->m_PrimCount * 4))
			return false;
		BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, VKBuffer, (VkDeviceSize)BufferOff);
		BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, ExecBuffer.m_aDescriptors[0]);

		SDeviceDescriptorSet UniDescrSet;
		if(!GetUniformBufferObject(ExecBuffer.m_ThreadIndex, true, UniDescrSet, 1, &pCommand->m_MsdfParams, sizeof(pCommand->m_MsdfParams)))
			return false;
		BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 1, UniDescrSet);
		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos), m.data());
		DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, 6, 1, 0, 0, 0);
		return true;
	}

	[[nodiscard]] bool Cmd_ReadPixel(const CCommandBuffer::SCommand_TrySwapAndReadPixel *pCommand)
	{
		if(!*pCommand->m_pSwapped && !NextFrame())
			return false;
		*pCommand->m_pSwapped = true;

		uint32_t Width;
		uint32_t Height;
		CImageInfo::EImageFormat Format;
		if(GetPresentedImageDataImpl(Width, Height, Format, m_vReadPixelHelper, false, pCommand->m_Position))
		{
			*pCommand->m_pColor = ColorRGBA(m_vReadPixelHelper[0] / 255.0f, m_vReadPixelHelper[1] / 255.0f, m_vReadPixelHelper[2] / 255.0f, 1.0f);
		}
		else
		{
			*pCommand->m_pColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		}

		return true;
	}

	[[nodiscard]] bool Cmd_Screenshot(const CCommandBuffer::SCommand_TrySwapAndScreenshot *pCommand)
	{
		if(!*pCommand->m_pSwapped && !NextFrame())
			return false;
		*pCommand->m_pSwapped = true;

		uint32_t Width;
		uint32_t Height;
		CImageInfo::EImageFormat Format;
		if(GetPresentedImageDataImpl(Width, Height, Format, m_vScreenshotHelper, true, {}))
		{
			const size_t ImgSize = (size_t)Width * (size_t)Height * CImageInfo::PixelSize(Format);
			pCommand->m_pImage->m_pData = static_cast<uint8_t *>(malloc(ImgSize));
			mem_copy(pCommand->m_pImage->m_pData, m_vScreenshotHelper.data(), ImgSize);
		}
		else
		{
			pCommand->m_pImage->m_pData = nullptr;
		}
		pCommand->m_pImage->m_Width = (int)Width;
		pCommand->m_pImage->m_Height = (int)Height;
		pCommand->m_pImage->m_Format = Format;

		return true;
	}

	void Cmd_RenderTex3D_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderTex3D *pCommand)
	{
		bool IsTextured = GetIsTextured(pCommand->m_State);
		if(IsTextured)
		{
			ExecBuffer.m_aDescriptors[0] = m_vTextures[pCommand->m_State.m_Texture].m_VKStandard3DTexturedDescrSet;
		}

		ExecBuffer.m_IndexBuffer = m_IndexBuffer;

		ExecBuffer.m_EstimatedRenderCallCount = 1;

		ExecBufferFillDynamicStates(pCommand->m_State, ExecBuffer);
	}

	[[nodiscard]] bool Cmd_RenderTex3D(const CCommandBuffer::SCommand_RenderTex3D *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
	{
		return RenderStandard<CCommandBuffer::SVertexTex3DStream, true>(ExecBuffer, pCommand->m_State, pCommand->m_PrimType, pCommand->m_pVertices, pCommand->m_PrimCount);
	}

	void Cmd_Update_Viewport_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_Update_Viewport *pCommand)
	{
		ExecBuffer.m_EstimatedRenderCallCount = 0;
	}

	[[nodiscard]] bool Cmd_Update_Viewport(const CCommandBuffer::SCommand_Update_Viewport *pCommand)
	{
		if(pCommand->m_ByResize)
		{
			if(IsVerbose())
			{
				dbg_msg("vulkan", "got resize event.");
			}
			m_CanvasWidth = (uint32_t)pCommand->m_Width;
			m_CanvasHeight = (uint32_t)pCommand->m_Height;
#ifndef CONF_PLATFORM_MACOS
			m_RecreateSwapChain = true;
#endif
		}
		else
		{
			auto Viewport = m_VKSwapImgAndViewportExtent.GetPresentedImageViewport();
			if(pCommand->m_X != 0 || pCommand->m_Y != 0 || (uint32_t)pCommand->m_Width != Viewport.width || (uint32_t)pCommand->m_Height != Viewport.height)
			{
				if(!m_ForceSingleThreadedRender)
					FinishRenderThreads();
				m_ForceSingleThreadedRender = true;
				m_HasDynamicViewport = true;

				// convert viewport from OGL to vulkan
				const int32_t ViewportWidth = std::max<int32_t>(1, pCommand->m_Width);
				const int32_t ViewportHeight = std::max<int32_t>(1, pCommand->m_Height);
				int32_t ViewportY = (int32_t)Viewport.height - ((int32_t)pCommand->m_Y + ViewportHeight);
				uint32_t ViewportH = (uint32_t)ViewportHeight;
				m_DynamicViewportOffset = {(int32_t)pCommand->m_X, ViewportY};
				m_DynamicViewportSize = {(uint32_t)ViewportWidth, ViewportH};
			}
			else
			{
				m_HasDynamicViewport = false;
			}
		}

		return true;
	}

	[[nodiscard]] bool Cmd_VSync(const CCommandBuffer::SCommand_VSync *pCommand)
	{
		if(IsVerbose())
		{
			dbg_msg("vulkan", "queueing swap chain recreation because vsync was changed");
		}
		m_RecreateSwapChain = true;
		*pCommand->m_pRetOk = true;

		return true;
	}

	[[nodiscard]] bool Cmd_MultiSampling(const CCommandBuffer::SCommand_MultiSampling *pCommand)
	{
		if(IsVerbose())
		{
			dbg_msg("vulkan", "queueing swap chain recreation because multi sampling was changed");
		}
		m_RecreateSwapChain = true;

		uint32_t MSCount = (std::min(pCommand->m_RequestedMultiSamplingCount, (uint32_t)GetMaxSampleCount()) & 0xFFFFFFFE); // ignore the uneven bits
		m_NextMultiSamplingCount = MSCount;

		*pCommand->m_pRetMultiSamplingCount = MSCount;
		*pCommand->m_pRetOk = true;

		return true;
	}

	[[nodiscard]] bool Cmd_Swap(const CCommandBuffer::SCommand_Swap *pCommand)
	{
		return NextFrame();
	}

	[[nodiscard]] bool Cmd_CreateBufferObject(const CCommandBuffer::SCommand_CreateBufferObject *pCommand)
	{
		bool IsOneFrameBuffer = (pCommand->m_Flags & IGraphics::EBufferObjectCreateFlags::BUFFER_OBJECT_CREATE_FLAGS_ONE_TIME_USE_BIT) != 0;
		if(!CreateBufferObject((size_t)pCommand->m_BufferIndex, pCommand->m_pUploadData, (VkDeviceSize)pCommand->m_DataSize, IsOneFrameBuffer))
			return false;
		if(pCommand->m_DeletePointer)
			free(pCommand->m_pUploadData);

		return true;
	}

	[[nodiscard]] bool Cmd_UpdateBufferObject(const CCommandBuffer::SCommand_UpdateBufferObject *pCommand)
	{
		size_t BufferIndex = (size_t)pCommand->m_BufferIndex;
		bool DeletePointer = pCommand->m_DeletePointer;
		VkDeviceSize Offset = (VkDeviceSize)((intptr_t)pCommand->m_pOffset);
		void *pUploadData = pCommand->m_pUploadData;
		VkDeviceSize DataSize = (VkDeviceSize)pCommand->m_DataSize;

		SMemoryBlock<STAGING_BUFFER_CACHE_ID> StagingBuffer;
		if(!GetStagingBuffer(StagingBuffer, pUploadData, DataSize))
			return false;

		const auto &MemBlock = m_vBufferObjects[BufferIndex].m_BufferObject.m_Mem;
		VkBuffer VertexBuffer = MemBlock.m_Buffer;
		if(!MemoryBarrier(VertexBuffer, Offset + MemBlock.m_HeapData.m_OffsetToAlign, DataSize, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, true))
			return false;
		if(!CopyBuffer(StagingBuffer.m_Buffer, VertexBuffer, StagingBuffer.m_HeapData.m_OffsetToAlign, Offset + MemBlock.m_HeapData.m_OffsetToAlign, DataSize))
			return false;
		if(!MemoryBarrier(VertexBuffer, Offset + MemBlock.m_HeapData.m_OffsetToAlign, DataSize, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, false))
			return false;

		UploadAndFreeStagingMemBlock(StagingBuffer);

		if(DeletePointer)
			free(pUploadData);

		return true;
	}

	[[nodiscard]] bool Cmd_RecreateBufferObject(const CCommandBuffer::SCommand_RecreateBufferObject *pCommand)
	{
		DeleteBufferObject((size_t)pCommand->m_BufferIndex);
		bool IsOneFrameBuffer = (pCommand->m_Flags & IGraphics::EBufferObjectCreateFlags::BUFFER_OBJECT_CREATE_FLAGS_ONE_TIME_USE_BIT) != 0;
		return CreateBufferObject((size_t)pCommand->m_BufferIndex, pCommand->m_pUploadData, (VkDeviceSize)pCommand->m_DataSize, IsOneFrameBuffer);
	}

	[[nodiscard]] bool Cmd_CopyBufferObject(const CCommandBuffer::SCommand_CopyBufferObject *pCommand)
	{
		size_t ReadBufferIndex = (size_t)pCommand->m_ReadBufferIndex;
		size_t WriteBufferIndex = (size_t)pCommand->m_WriteBufferIndex;
		auto &ReadMemBlock = m_vBufferObjects[ReadBufferIndex].m_BufferObject.m_Mem;
		auto &WriteMemBlock = m_vBufferObjects[WriteBufferIndex].m_BufferObject.m_Mem;
		VkBuffer ReadBuffer = ReadMemBlock.m_Buffer;
		VkBuffer WriteBuffer = WriteMemBlock.m_Buffer;

		VkDeviceSize DataSize = (VkDeviceSize)pCommand->m_CopySize;
		VkDeviceSize ReadOffset = (VkDeviceSize)pCommand->m_ReadOffset + ReadMemBlock.m_HeapData.m_OffsetToAlign;
		VkDeviceSize WriteOffset = (VkDeviceSize)pCommand->m_WriteOffset + WriteMemBlock.m_HeapData.m_OffsetToAlign;

		if(!MemoryBarrier(ReadBuffer, ReadOffset, DataSize, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, true))
			return false;
		if(!MemoryBarrier(WriteBuffer, WriteOffset, DataSize, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, true))
			return false;
		if(!CopyBuffer(ReadBuffer, WriteBuffer, ReadOffset, WriteOffset, DataSize))
			return false;
		if(!MemoryBarrier(WriteBuffer, WriteOffset, DataSize, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, false))
			return false;
		if(!MemoryBarrier(ReadBuffer, ReadOffset, DataSize, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, false))
			return false;

		return true;
	}

	[[nodiscard]] bool Cmd_DeleteBufferObject(const CCommandBuffer::SCommand_DeleteBufferObject *pCommand)
	{
		size_t BufferIndex = (size_t)pCommand->m_BufferIndex;
		DeleteBufferObject(BufferIndex);

		return true;
	}

	[[nodiscard]] bool Cmd_CreateBufferContainer(const CCommandBuffer::SCommand_CreateBufferContainer *pCommand)
	{
		size_t ContainerIndex = (size_t)pCommand->m_BufferContainerIndex;
		while(ContainerIndex >= m_vBufferContainers.size())
			m_vBufferContainers.resize((m_vBufferContainers.size() * 2) + 1);

		m_vBufferContainers[ContainerIndex].m_BufferObjectIndex = pCommand->m_VertBufferBindingIndex;

		return true;
	}

	[[nodiscard]] bool Cmd_UpdateBufferContainer(const CCommandBuffer::SCommand_UpdateBufferContainer *pCommand)
	{
		size_t ContainerIndex = (size_t)pCommand->m_BufferContainerIndex;
		m_vBufferContainers[ContainerIndex].m_BufferObjectIndex = pCommand->m_VertBufferBindingIndex;

		return true;
	}

	[[nodiscard]] bool Cmd_DeleteBufferContainer(const CCommandBuffer::SCommand_DeleteBufferContainer *pCommand)
	{
		size_t ContainerIndex = (size_t)pCommand->m_BufferContainerIndex;
		bool DeleteAllBO = pCommand->m_DestroyAllBO;
		if(DeleteAllBO)
		{
			size_t BufferIndex = (size_t)m_vBufferContainers[ContainerIndex].m_BufferObjectIndex;
			DeleteBufferObject(BufferIndex);
		}

		return true;
	}

	[[nodiscard]] bool Cmd_IndicesRequiredNumNotify(const CCommandBuffer::SCommand_IndicesRequiredNumNotify *pCommand)
	{
		size_t IndicesCount = pCommand->m_RequiredIndicesNum;
		if(m_CurRenderIndexPrimitiveCount < IndicesCount / 6)
		{
			m_vvFrameDelayedBufferCleanup[m_CurImageIndex].push_back({m_RenderIndexBuffer, m_RenderIndexBufferMemory});
			std::vector<uint32_t> vIndices(IndicesCount);
			uint32_t Primq = 0;
			for(size_t i = 0; i < IndicesCount; i += 6)
			{
				vIndices[i] = Primq;
				vIndices[i + 1] = Primq + 1;
				vIndices[i + 2] = Primq + 2;
				vIndices[i + 3] = Primq;
				vIndices[i + 4] = Primq + 2;
				vIndices[i + 5] = Primq + 3;
				Primq += 4;
			}
			if(!CreateIndexBuffer(vIndices.data(), vIndices.size() * sizeof(uint32_t), m_RenderIndexBuffer, m_RenderIndexBufferMemory))
				return false;
			m_CurRenderIndexPrimitiveCount = IndicesCount / 6;
		}

		return true;
	}

	void Cmd_RenderTileLayer_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderTileLayer *pCommand)
	{
		RenderTileLayer_FillExecuteBuffer(ExecBuffer, pCommand->m_IndicesDrawNum, pCommand->m_State, pCommand->m_BufferContainerIndex);
	}

	[[nodiscard]] bool Cmd_RenderTileLayer(const CCommandBuffer::SCommand_RenderTileLayer *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
	{
		vec2 Scale{};
		vec2 Off{};
		return RenderTileLayer(ExecBuffer, pCommand->m_State, false, pCommand->m_Color, Scale, Off, (size_t)pCommand->m_IndicesDrawNum, pCommand->m_pIndicesOffsets, pCommand->m_pDrawCount);
	}

	void Cmd_RenderBorderTile_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderBorderTile *pCommand)
	{
		RenderTileLayer_FillExecuteBuffer(ExecBuffer, 1, pCommand->m_State, pCommand->m_BufferContainerIndex);
	}

	[[nodiscard]] bool Cmd_RenderBorderTile(const CCommandBuffer::SCommand_RenderBorderTile *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
	{
		vec2 Scale = pCommand->m_Scale;
		vec2 Off = pCommand->m_Offset;
		unsigned int DrawNum = pCommand->m_DrawNum * 6;
		return RenderTileLayer(ExecBuffer, pCommand->m_State, true, pCommand->m_Color, Scale, Off, 1, &pCommand->m_pIndicesOffset, &DrawNum);
	}

	void Cmd_RenderQuadLayer_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderQuadLayer *pCommand)
	{
		size_t BufferContainerIndex = (size_t)pCommand->m_BufferContainerIndex;
		size_t BufferObjectIndex = (size_t)m_vBufferContainers[BufferContainerIndex].m_BufferObjectIndex;
		const auto &BufferObject = m_vBufferObjects[BufferObjectIndex];

		ExecBuffer.m_Buffer = BufferObject.m_CurBuffer;
		ExecBuffer.m_BufferOff = BufferObject.m_CurBufferOffset;

		bool IsTextured = GetIsTextured(pCommand->m_State);
		if(IsTextured)
		{
			size_t AddressModeIndex = GetAddressModeIndex(pCommand->m_State);
			ExecBuffer.m_aDescriptors[0] = m_vTextures[pCommand->m_State.m_Texture].m_aVKStandardTexturedDescrSets[AddressModeIndex];
		}

		ExecBuffer.m_IndexBuffer = m_RenderIndexBuffer;

		ExecBuffer.m_EstimatedRenderCallCount = ((pCommand->m_QuadNum - 1) / gs_GraphicsMaxQuadsRenderCount) + 1;

		ExecBufferFillDynamicStates(pCommand->m_State, ExecBuffer);
	}

	[[nodiscard]] bool Cmd_RenderQuadLayer(const CCommandBuffer::SCommand_RenderQuadLayer *pCommand, SRenderCommandExecuteBuffer &ExecBuffer, bool Grouped)
	{
		std::array<float, (size_t)4 * 2> m;
		GetStateMatrix(pCommand->m_State, m);

		bool CanBeGrouped = Grouped || pCommand->m_QuadNum == 1;

		bool IsTextured;
		size_t BlendModeIndex;
		size_t DynamicIndex;
		size_t AddressModeIndex;
		GetStateIndices(ExecBuffer, pCommand->m_State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
		auto &PipeLayout = GetPipeLayout(CanBeGrouped ? m_QuadGroupedPipeline : m_QuadPipeline, IsTextured, BlendModeIndex, DynamicIndex);
		auto &PipeLine = GetPipeline(CanBeGrouped ? m_QuadGroupedPipeline : m_QuadPipeline, IsTextured, BlendModeIndex, DynamicIndex);

		VkCommandBuffer *pCommandBuffer;
		if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
			return false;
		auto &CommandBuffer = *pCommandBuffer;

		BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

		BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_Buffer, (VkDeviceSize)ExecBuffer.m_BufferOff);

		BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

		if(IsTextured)
		{
			BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, ExecBuffer.m_aDescriptors[0]);
		}

		uint32_t DrawCount = (uint32_t)pCommand->m_QuadNum;

		if(CanBeGrouped)
		{
			SUniformQuadGroupedGPos PushConstantVertex;
			mem_copy(&PushConstantVertex.m_BOPush, &pCommand->m_pQuadInfo[0], sizeof(PushConstantVertex.m_BOPush));

			mem_copy(PushConstantVertex.m_aPos, m.data(), sizeof(PushConstantVertex.m_aPos));
			vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SUniformQuadGroupedGPos), &PushConstantVertex);

			VkDeviceSize IndexOffset = (VkDeviceSize)((ptrdiff_t)(pCommand->m_QuadOffset) * 6);
			DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, static_cast<uint32_t>(DrawCount * 6), 1, IndexOffset, 0, 0);
		}
		else
		{
			SUniformQuadGPos PushConstantVertex;
			mem_copy(PushConstantVertex.m_aPos, m.data(), sizeof(PushConstantVertex.m_aPos));
			PushConstantVertex.m_QuadOffset = pCommand->m_QuadOffset;

			vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantVertex), &PushConstantVertex);

			size_t RenderOffset = 0;
			while(DrawCount > 0)
			{
				uint32_t RealDrawCount = (DrawCount > gs_GraphicsMaxQuadsRenderCount ? gs_GraphicsMaxQuadsRenderCount : DrawCount);
				VkDeviceSize IndexOffset = (VkDeviceSize)((ptrdiff_t)(pCommand->m_QuadOffset + RenderOffset) * 6);

				// create uniform buffer
				SDeviceDescriptorSet UniDescrSet;
				if(!GetUniformBufferObject(ExecBuffer.m_ThreadIndex, true, UniDescrSet, RealDrawCount, (const float *)(pCommand->m_pQuadInfo + RenderOffset), RealDrawCount * sizeof(SQuadRenderInfo)))
					return false;

				BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, IsTextured ? 1 : 0, UniDescrSet);
				if(RenderOffset > 0)
				{
					int32_t QuadOffset = pCommand->m_QuadOffset + RenderOffset;
					vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, sizeof(SUniformQuadGPos) - sizeof(int32_t), sizeof(int32_t), &QuadOffset);
				}

				DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, static_cast<uint32_t>(RealDrawCount * 6), 1, IndexOffset, 0, 0);
				RenderOffset += RealDrawCount;
				DrawCount -= RealDrawCount;
			}
		}

		return true;
	}

	void Cmd_RenderText_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderText *pCommand)
	{
		size_t BufferContainerIndex = (size_t)pCommand->m_BufferContainerIndex;
		size_t BufferObjectIndex = (size_t)m_vBufferContainers[BufferContainerIndex].m_BufferObjectIndex;
		const auto &BufferObject = m_vBufferObjects[BufferObjectIndex];

		ExecBuffer.m_Buffer = BufferObject.m_CurBuffer;
		ExecBuffer.m_BufferOff = BufferObject.m_CurBufferOffset;

		ExecBuffer.m_aDescriptors[0] = m_vTextures[pCommand->m_TextTextureIndex].m_VKTextDescrSet;

		ExecBuffer.m_IndexBuffer = m_RenderIndexBuffer;

		ExecBuffer.m_EstimatedRenderCallCount = 1;

		ExecBufferFillDynamicStates(pCommand->m_State, ExecBuffer);
	}

	[[nodiscard]] bool Cmd_RenderText(const CCommandBuffer::SCommand_RenderText *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
	{
		std::array<float, (size_t)4 * 2> m;
		GetStateMatrix(pCommand->m_State, m);

		bool IsTextured;
		size_t BlendModeIndex;
		size_t DynamicIndex;
		size_t AddressModeIndex;
		GetStateIndices(ExecBuffer, pCommand->m_State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
		IsTextured = true; // text is always textured
		auto &PipeLayout = GetPipeLayout(m_TextPipeline, IsTextured, BlendModeIndex, DynamicIndex);
		auto &PipeLine = GetPipeline(m_TextPipeline, IsTextured, BlendModeIndex, DynamicIndex);

		VkCommandBuffer *pCommandBuffer;
		if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
			return false;
		auto &CommandBuffer = *pCommandBuffer;

		BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

		BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_Buffer, (VkDeviceSize)ExecBuffer.m_BufferOff);

		BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

		BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, ExecBuffer.m_aDescriptors[0]);

		SUniformGTextPos PosTexSizeConstant;
		mem_copy(PosTexSizeConstant.m_aPos, m.data(), m.size() * sizeof(float));
		PosTexSizeConstant.m_TextureSize = pCommand->m_TextureSize;

		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGTextPos), &PosTexSizeConstant);

		SUniformTextFragment FragmentConstants;

		FragmentConstants.m_Constants.m_TextColor = pCommand->m_TextColor;
		FragmentConstants.m_Constants.m_TextOutlineColor = pCommand->m_TextOutlineColor;
		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(SUniformGTextPos) + sizeof(SUniformTextGFragmentOffset), sizeof(SUniformTextFragment), &FragmentConstants);

		DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, static_cast<uint32_t>(pCommand->m_DrawNum), 1, 0, 0, 0);

		return true;
	}

	void BufferContainer_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SState &State, size_t BufferContainerIndex, size_t DrawCalls)
	{
		size_t BufferObjectIndex = (size_t)m_vBufferContainers[BufferContainerIndex].m_BufferObjectIndex;
		const auto &BufferObject = m_vBufferObjects[BufferObjectIndex];

		ExecBuffer.m_Buffer = BufferObject.m_CurBuffer;
		ExecBuffer.m_BufferOff = BufferObject.m_CurBufferOffset;

		bool IsTextured = GetIsTextured(State);
		if(IsTextured)
		{
			size_t AddressModeIndex = GetAddressModeIndex(State);
			ExecBuffer.m_aDescriptors[0] = m_vTextures[State.m_Texture].m_aVKStandardTexturedDescrSets[AddressModeIndex];
		}

		ExecBuffer.m_IndexBuffer = m_RenderIndexBuffer;

		ExecBuffer.m_EstimatedRenderCallCount = DrawCalls;

		ExecBufferFillDynamicStates(State, ExecBuffer);
	}

	void Cmd_RenderQuadContainer_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderQuadContainer *pCommand)
	{
		BufferContainer_FillExecuteBuffer(ExecBuffer, pCommand->m_State, (size_t)pCommand->m_BufferContainerIndex, 1);
	}

	[[nodiscard]] bool Cmd_RenderQuadContainer(const CCommandBuffer::SCommand_RenderQuadContainer *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
	{
		std::array<float, (size_t)4 * 2> m;
		GetStateMatrix(pCommand->m_State, m);

		bool IsTextured;
		size_t BlendModeIndex;
		size_t DynamicIndex;
		size_t AddressModeIndex;
		GetStateIndices(ExecBuffer, pCommand->m_State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
		auto &PipeLayout = GetStandardPipeLayout(false, IsTextured, BlendModeIndex, DynamicIndex);
		auto &PipeLine = GetStandardPipe(false, IsTextured, BlendModeIndex, DynamicIndex);

		VkCommandBuffer *pCommandBuffer;
		if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
			return false;
		auto &CommandBuffer = *pCommandBuffer;

		BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

		BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_Buffer, (VkDeviceSize)ExecBuffer.m_BufferOff);

		VkDeviceSize IndexOffset = (VkDeviceSize)((ptrdiff_t)pCommand->m_pOffset);

		BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, IndexOffset, VK_INDEX_TYPE_UINT32);

		if(IsTextured)
		{
			BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, ExecBuffer.m_aDescriptors[0]);
		}

		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformGPos), m.data());

		DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, static_cast<uint32_t>(pCommand->m_DrawNum), 1, 0, 0, 0);

		return true;
	}

	void Cmd_RenderQuadContainerEx_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderQuadContainerEx *pCommand)
	{
		BufferContainer_FillExecuteBuffer(ExecBuffer, pCommand->m_State, (size_t)pCommand->m_BufferContainerIndex, 1);
	}

	[[nodiscard]] bool Cmd_RenderQuadContainerEx(const CCommandBuffer::SCommand_RenderQuadContainerEx *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
	{
		std::array<float, (size_t)4 * 2> m;
		GetStateMatrix(pCommand->m_State, m);

		bool IsRotationless = !(pCommand->m_Rotation != 0);
		bool IsTextured;
		size_t BlendModeIndex;
		size_t DynamicIndex;
		size_t AddressModeIndex;
		GetStateIndices(ExecBuffer, pCommand->m_State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
		auto &PipeLayout = GetPipeLayout(IsRotationless ? m_PrimExRotationlessPipeline : m_PrimExPipeline, IsTextured, BlendModeIndex, DynamicIndex);
		auto &PipeLine = GetPipeline(IsRotationless ? m_PrimExRotationlessPipeline : m_PrimExPipeline, IsTextured, BlendModeIndex, DynamicIndex);

		VkCommandBuffer *pCommandBuffer;
		if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
			return false;
		auto &CommandBuffer = *pCommandBuffer;

		BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

		BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_Buffer, (VkDeviceSize)ExecBuffer.m_BufferOff);

		VkDeviceSize IndexOffset = (VkDeviceSize)((ptrdiff_t)pCommand->m_pOffset);

		BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, IndexOffset, VK_INDEX_TYPE_UINT32);

		if(IsTextured)
		{
			BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, ExecBuffer.m_aDescriptors[0]);
		}

		SUniformPrimExGVertColor PushConstantColor;
		SUniformPrimExGPos PushConstantVertex;
		size_t VertexPushConstantSize = sizeof(PushConstantVertex);

		PushConstantColor = pCommand->m_VertexColor;
		mem_copy(PushConstantVertex.m_aPos, m.data(), sizeof(PushConstantVertex.m_aPos));

		if(!IsRotationless)
		{
			PushConstantVertex.m_Rotation = pCommand->m_Rotation;
			PushConstantVertex.m_Center = {pCommand->m_Center.x, pCommand->m_Center.y};
		}
		else
		{
			VertexPushConstantSize = sizeof(SUniformPrimExGPosRotationless);
		}

		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, VertexPushConstantSize, &PushConstantVertex);
		vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(SUniformPrimExGPos) + sizeof(SUniformPrimExGVertColorAlign), sizeof(PushConstantColor), &PushConstantColor);

		DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, static_cast<uint32_t>(pCommand->m_DrawNum), 1, 0, 0, 0);

		return true;
	}

	void Cmd_RenderQuadContainerAsSpriteMultiple_FillExecuteBuffer(SRenderCommandExecuteBuffer &ExecBuffer, const CCommandBuffer::SCommand_RenderQuadContainerAsSpriteMultiple *pCommand)
	{
		BufferContainer_FillExecuteBuffer(ExecBuffer, pCommand->m_State, (size_t)pCommand->m_BufferContainerIndex, ((pCommand->m_DrawCount - 1) / gs_GraphicsMaxParticlesRenderCount) + 1);
	}

	[[nodiscard]] bool Cmd_RenderQuadContainerAsSpriteMultiple(const CCommandBuffer::SCommand_RenderQuadContainerAsSpriteMultiple *pCommand, SRenderCommandExecuteBuffer &ExecBuffer)
	{
		std::array<float, (size_t)4 * 2> m;
		GetStateMatrix(pCommand->m_State, m);

		bool CanBePushed = pCommand->m_DrawCount <= 1;

		bool IsTextured;
		size_t BlendModeIndex;
		size_t DynamicIndex;
		size_t AddressModeIndex;
		GetStateIndices(ExecBuffer, pCommand->m_State, IsTextured, BlendModeIndex, DynamicIndex, AddressModeIndex);
		auto &PipeLayout = GetPipeLayout(CanBePushed ? m_SpriteMultiPushPipeline : m_SpriteMultiPipeline, IsTextured, BlendModeIndex, DynamicIndex);
		auto &PipeLine = GetPipeline(CanBePushed ? m_SpriteMultiPushPipeline : m_SpriteMultiPipeline, IsTextured, BlendModeIndex, DynamicIndex);

		VkCommandBuffer *pCommandBuffer;
		if(!GetGraphicCommandBuffer(pCommandBuffer, ExecBuffer.m_ThreadIndex))
			return false;
		auto &CommandBuffer = *pCommandBuffer;

		BindPipeline(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer, PipeLine, pCommand->m_State);

		BindVertexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_Buffer, (VkDeviceSize)ExecBuffer.m_BufferOff);

		VkDeviceSize IndexOffset = (VkDeviceSize)((ptrdiff_t)pCommand->m_pOffset);
		BindIndexBuffer(ExecBuffer.m_ThreadIndex, CommandBuffer, ExecBuffer.m_IndexBuffer, IndexOffset, VK_INDEX_TYPE_UINT32);

		BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 0, ExecBuffer.m_aDescriptors[0]);

		if(CanBePushed)
		{
			SUniformSpriteMultiPushGVertColor PushConstantColor;
			SUniformSpriteMultiPushGPos PushConstantVertex;

			PushConstantColor = pCommand->m_VertexColor;

			mem_copy(PushConstantVertex.m_aPos, m.data(), sizeof(PushConstantVertex.m_aPos));
			PushConstantVertex.m_Center = pCommand->m_Center;

			for(size_t i = 0; i < pCommand->m_DrawCount; ++i)
				PushConstantVertex.m_aPSR[i] = vec4(pCommand->m_pRenderInfo[i].m_Pos.x, pCommand->m_pRenderInfo[i].m_Pos.y, pCommand->m_pRenderInfo[i].m_Scale, pCommand->m_pRenderInfo[i].m_Rotation);

			vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SUniformSpriteMultiPushGPosBase) + sizeof(vec4) * pCommand->m_DrawCount, &PushConstantVertex);
			vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(SUniformSpriteMultiPushGPos), sizeof(PushConstantColor), &PushConstantColor);
		}
		else
		{
			SUniformSpriteMultiGVertColor PushConstantColor;
			SUniformSpriteMultiGPos PushConstantVertex;

			PushConstantColor = pCommand->m_VertexColor;

			mem_copy(PushConstantVertex.m_aPos, m.data(), sizeof(PushConstantVertex.m_aPos));
			PushConstantVertex.m_Center = pCommand->m_Center;

			vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantVertex), &PushConstantVertex);
			vkCmdPushConstants(CommandBuffer, PipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(SUniformSpriteMultiGPos) + sizeof(SUniformSpriteMultiGVertColorAlign), sizeof(PushConstantColor), &PushConstantColor);
		}

		const int RSPCount = 512;
		int DrawCount = pCommand->m_DrawCount;
		size_t RenderOffset = 0;

		while(DrawCount > 0)
		{
			int UniformCount = (DrawCount > RSPCount ? RSPCount : DrawCount);

			if(!CanBePushed)
			{
				// create uniform buffer
				SDeviceDescriptorSet UniDescrSet;
				if(!GetUniformBufferObject(ExecBuffer.m_ThreadIndex, false, UniDescrSet, UniformCount, (const float *)(pCommand->m_pRenderInfo + RenderOffset), UniformCount * sizeof(IGraphics::SRenderSpriteInfo)))
					return false;

				BindDescriptorSet(ExecBuffer.m_ThreadIndex, CommandBuffer, PipeLayout, 1, UniDescrSet);
			}

			DrawIndexed(ExecBuffer.m_ThreadIndex, CommandBuffer, static_cast<uint32_t>(pCommand->m_DrawNum), UniformCount, 0, 0, 0);

			RenderOffset += RSPCount;
			DrawCount -= RSPCount;
		}

		return true;
	}

	[[nodiscard]] bool Cmd_WindowCreateNtf(const CCommandBuffer::SCommand_WindowCreateNtf *pCommand)
	{
		log_debug("vulkan", "creating new surface.");
		m_pWindow = SDL_GetWindowFromID(pCommand->m_WindowId);
		if(m_RenderingPaused)
		{
#ifdef CONF_PLATFORM_ANDROID
			if(!CreateSurface(m_pWindow))
				return false;
#endif
			m_RecreateSwapChain = true;
			m_RenderingPaused = false;
			if(!PureMemoryFrame())
				return false;
			if(!PrepareFrame())
				return false;
		}

		return true;
	}

	[[nodiscard]] bool Cmd_WindowDestroyNtf(const CCommandBuffer::SCommand_WindowDestroyNtf *pCommand)
	{
		log_debug("vulkan", "surface got destroyed.");
		if(!m_RenderingPaused)
		{
			if(!WaitFrame())
				return false;
			m_RenderingPaused = true;
			VkResult WaitIdleResult = DeviceWaitIdle();
			if(WaitIdleResult != VK_SUCCESS)
			{
				const char *pCritErrorMsg = CheckVulkanCriticalError(WaitIdleResult);
				if(pCritErrorMsg != nullptr)
					SetError(EGfxErrorType::GFX_ERROR_TYPE_SWAP_FAILED, "Waiting for device idle before destroying window surface failed.", pCritErrorMsg);
				else
					SetError(EGfxErrorType::GFX_ERROR_TYPE_SWAP_FAILED, "Waiting for device idle before destroying window surface failed.");
				return false;
			}
			CleanupVulkanSwapChain(true);
		}

		return true;
	}

	[[nodiscard]] bool Cmd_PreInit(const CCommandProcessorFragment_GLBase::SCommand_PreInit *pCommand)
	{
		m_pGpuList = pCommand->m_pGpuList;
		if(InitVulkanSDL(pCommand->m_pWindow, pCommand->m_Width, pCommand->m_Height, pCommand->m_pRendererString, pCommand->m_pVendorString, pCommand->m_pVersionString) != 0)
		{
			m_VKInstance = VK_NULL_HANDLE;
		}

		RegisterCommands();

		m_ThreadCount = g_Config.m_GfxRenderThreadCount;
		if(m_ThreadCount <= 1)
			m_ThreadCount = 1;
		else
		{
			m_ThreadCount = std::clamp<decltype(m_ThreadCount)>(m_ThreadCount, 3, std::max<decltype(m_ThreadCount)>(3, std::thread::hardware_concurrency()));
		}
		if(pCommand->m_pVendorString != nullptr && str_find_nocase(pCommand->m_pVendorString, "AMD") != nullptr && m_ThreadCount > 1)
		{
			dbg_msg("vulkan", "forcing single-threaded rendering on AMD to avoid driver crashes with dynamic viewport usage");
			m_ThreadCount = 1;
		}

		// start threads
		dbg_assert(m_ThreadCount != 2, "Either use 1 main thread or at least 2 extra rendering threads.");
		if(m_ThreadCount > 1)
		{
			m_vvThreadCommandLists.resize(m_ThreadCount - 1);
			m_vThreadHelperHadCommands.resize(m_ThreadCount - 1, false);
			for(auto &ThreadCommandList : m_vvThreadCommandLists)
			{
				ThreadCommandList.reserve(256);
			}

			m_vpRenderThreads.reserve(m_ThreadCount - 1);
			for(size_t i = 0; i < m_ThreadCount - 1; ++i)
			{
				auto *pRenderThread = new SRenderThread();
				std::unique_lock<std::mutex> Lock(pRenderThread->m_Mutex);
				m_vpRenderThreads.emplace_back(pRenderThread);
				pRenderThread->m_Thread = std::thread([this, i]() { RunThread(i); });
				// wait until thread started
				pRenderThread->m_Cond.wait(Lock, [pRenderThread]() -> bool { return pRenderThread->m_Started; });
			}
		}

		return true;
	}

	[[nodiscard]] bool Cmd_PostShutdown(const CCommandProcessorFragment_GLBase::SCommand_PostShutdown *pCommand)
	{
		for(size_t i = 0; i < m_ThreadCount - 1; ++i)
		{
			auto *pThread = m_vpRenderThreads[i].get();
			{
				std::unique_lock<std::mutex> Lock(pThread->m_Mutex);
				pThread->m_Finished = true;
				pThread->m_Cond.notify_one();
			}
			pThread->m_Thread.join();
		}
		m_vpRenderThreads.clear();
		m_vvThreadCommandLists.clear();
		m_vThreadHelperHadCommands.clear();

		m_ThreadCount = 1;

		CleanupVulkanSDL();

		return true;
	}

	void StartCommands(size_t CommandCount, size_t EstimatedRenderCallCount) override
	{
		m_CommandsInPipe = CommandCount;
		m_RenderCallsInPipe = EstimatedRenderCallCount;
		m_CurCommandInPipe = 0;
		m_CurRenderCallCountInPipe = 0;
		m_FrameProfileStats.m_CommandCount += CommandCount;
		m_FrameProfileStats.m_EstimatedRenderCallCount += EstimatedRenderCallCount;
	}

	void EndCommands() override
	{
		FinishRenderThreads();
		m_CommandsInPipe = 0;
		m_RenderCallsInPipe = 0;
	}

	/****************
	 * RENDER THREADS
	 *****************/

	void RunThread(size_t ThreadIndex)
	{
		auto *pThread = m_vpRenderThreads[ThreadIndex].get();
		std::unique_lock<std::mutex> Lock(pThread->m_Mutex);
		pThread->m_Started = true;
		pThread->m_Cond.notify_one();

		while(!pThread->m_Finished)
		{
			pThread->m_Cond.wait(Lock, [pThread]() -> bool { return pThread->m_IsRendering || pThread->m_Finished; });
			pThread->m_Cond.notify_one();

			// set this to true, if you want to benchmark the render thread times
			static constexpr bool s_BenchmarkRenderThreads = false;
			std::chrono::nanoseconds ThreadRenderTime = 0ns;
			if(IsVerbose() && s_BenchmarkRenderThreads)
			{
				ThreadRenderTime = time_get_nanoseconds();
			}

			if(!pThread->m_Finished)
			{
				bool HasErrorFromCmd = false;
				for(auto &NextCmd : m_vvThreadCommandLists[ThreadIndex])
				{
					const auto RecordStartTime = m_FrameProfilingActive ? time_get_nanoseconds() : 0ns;
					if(!m_aCommandCallbacks[CommandBufferCMDOff(NextCmd.m_Command)].m_CommandCB(NextCmd.m_pRawCommand, NextCmd))
					{
						if(m_FrameProfilingActive)
							m_vThreadFrameProfileStats[ThreadIndex + 1].m_CPUThreadCommandRecordTime += time_get_nanoseconds() - RecordStartTime;
						m_vThreadFrameProfileStats[ThreadIndex + 1].m_ThreadCommandRecords++;
						// an error occurred, the thread will not continue execution
						HasErrorFromCmd = true;
						break;
					}
					if(m_FrameProfilingActive)
						m_vThreadFrameProfileStats[ThreadIndex + 1].m_CPUThreadCommandRecordTime += time_get_nanoseconds() - RecordStartTime;
					m_vThreadFrameProfileStats[ThreadIndex + 1].m_ThreadCommandRecords++;
				}
				m_vvThreadCommandLists[ThreadIndex].clear();

				if(!HasErrorFromCmd && m_vvUsedThreadDrawCommandBuffer[ThreadIndex + 1][m_CurImageIndex])
				{
					auto &GraphicThreadCommandBuffer = m_vvThreadDrawCommandBuffers[ThreadIndex + 1][m_CurImageIndex];
					VkResult EndThreadCommandBufferResult = vkEndCommandBuffer(GraphicThreadCommandBuffer);
					if(EndThreadCommandBufferResult != VK_SUCCESS)
					{
						const char *pCritErrorMsg = CheckVulkanCriticalError(EndThreadCommandBufferResult);
						if(pCritErrorMsg != nullptr)
							SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_RECORDING, "Thread draw command buffer cannot be ended anymore.", pCritErrorMsg);
						else
							SetError(EGfxErrorType::GFX_ERROR_TYPE_RENDER_RECORDING, "Thread draw command buffer cannot be ended anymore.");
						m_vvUsedThreadDrawCommandBuffer[ThreadIndex + 1][m_CurImageIndex] = false;
					}
					ResetDrawCommandState(ThreadIndex + 1);
				}
			}

			if(IsVerbose() && s_BenchmarkRenderThreads)
			{
				dbg_msg("vulkan", "render thread %" PRIzu " took %d ns to finish", ThreadIndex, (int)(time_get_nanoseconds() - ThreadRenderTime).count());
			}

			pThread->m_IsRendering = false;
		}
	}
};

CCommandProcessorFragment_GLBase *CreateVulkanCommandProcessorFragment()
{
	return new CCommandProcessorFragment_Vulkan();
}

#endif
