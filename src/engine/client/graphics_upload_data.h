#ifndef ENGINE_CLIENT_GRAPHICS_UPLOAD_DATA_H
#define ENGINE_CLIENT_GRAPHICS_UPLOAD_DATA_H

#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

struct CGraphicsUploadDeleter
{
	void operator()(void *pData) const noexcept { std::free(pData); }
};

template<typename T>
using CGraphicsUploadData = std::unique_ptr<T, CGraphicsUploadDeleter>;

// 命令区只保存可平凡复制的原始指针；消费时清空来源，并在处理器栈上持有资源。
// 借用的命令区数据不转移，也不由处理器释放。
template<typename T, typename TRelease = CGraphicsUploadDeleter>
std::unique_ptr<T, TRelease> TakeGraphicsUpload(T *&pData, bool Owned = true, TRelease Release = {}) noexcept
{
	static_assert(std::is_nothrow_move_constructible_v<TRelease>);
	static_assert(std::is_nothrow_invocable_v<TRelease &, T *>);
	return std::unique_ptr<T, TRelease>(Owned ? std::exchange(pData, nullptr) : nullptr, std::move(Release));
}

#endif
