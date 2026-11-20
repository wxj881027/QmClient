#ifndef BASE_BORROWED_ALLOCATOR_H
#define BASE_BORROWED_ALLOCATOR_H

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

// 可选内存域只借用，不接管；须存活到全部容器及其存储销毁。
// 分配须满足请求的对齐，释放须配对且不抛异常；不依赖PMR运行库。
class IAllocationResource
{
public:
	virtual ~IAllocationResource() = default;
	virtual void *Allocate(std::size_t Bytes, std::size_t Alignment) = 0;
	virtual void Deallocate(void *pData, std::size_t Bytes, std::size_t Alignment) noexcept = 0;

	static void *AllocateDefault(std::size_t Bytes, std::size_t Alignment)
	{
		if(Alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
			return ::operator new(Bytes, std::align_val_t(Alignment));
		return ::operator new(Bytes);
	}
	static void DeallocateDefault(void *pData, std::size_t Bytes, std::size_t Alignment) noexcept
	{
		(void)Bytes;
		if(Alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
			::operator delete(pData, std::align_val_t(Alignment));
		else
			::operator delete(pData);
	}
};

// 默认直接委托std::allocator，不改变全局分配器或默认内存域。
// 容器赋值不传播内存域；交换须使用同一内存域，移动构造保留借用。
template<typename T>
class CBorrowedAllocator
{
	IAllocationResource *m_pResource = nullptr;

public:
	using value_type = T;
	using propagate_on_container_copy_assignment = std::false_type;
	using propagate_on_container_move_assignment = std::false_type;
	using propagate_on_container_swap = std::false_type;
	using is_always_equal = std::false_type;

	CBorrowedAllocator() noexcept = default;
	CBorrowedAllocator(IAllocationResource *pResource) noexcept :
		m_pResource(pResource) {}
	template<typename U>
	CBorrowedAllocator(const CBorrowedAllocator<U> &Other) noexcept :
		m_pResource(Other.Resource())
	{
	}
	template<typename U>
	struct rebind
	{
		using other = CBorrowedAllocator<U>;
	};

	T *allocate(std::size_t Count)
	{
		if(Count > std::numeric_limits<std::size_t>::max() / sizeof(T))
			throw std::bad_array_new_length();
		if(!m_pResource)
			return std::allocator<T>{}.allocate(Count);
		auto *pData = m_pResource->Allocate(Count * sizeof(T), alignof(T));
		if(!pData)
			throw std::bad_alloc();
		return static_cast<T *>(pData);
	}
	void deallocate(T *pData, std::size_t Count) noexcept
	{
		if(m_pResource)
			m_pResource->Deallocate(pData, Count * sizeof(T), alignof(T));
		else
			std::allocator<T>{}.deallocate(pData, Count);
	}

	IAllocationResource *Resource() const noexcept { return m_pResource; }
	// 复制构造容器时回到默认堆，避免隐式延长外部内存域的借用。
	CBorrowedAllocator select_on_container_copy_construction() const noexcept { return {}; }
	template<typename U>
	bool operator==(const CBorrowedAllocator<U> &Other) const noexcept
	{
		return m_pResource == Other.Resource();
	}
};

#endif
