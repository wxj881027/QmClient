#ifndef ANDROID_ASSET_UNPACK_H
#define ANDROID_ASSET_UNPACK_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace android_assets
{
	enum class EStage
	{
		DONE,
		INVALID_LENGTH,
		READ,
		WRITE,
		PREPARATION,
		INPUT_CLOSE,
		OUTPUT_CLOSE,
		PUBLISH,
	};

	// 调用方持有输入和候选；两个关闭回调必须不抛异常，并且只执行一次。
	// 完整复制、输入关闭与输出收尾全部成功后才发布候选。
	template<typename TRead, typename TWrite, typename TCloseInput, typename TCloseOutput, typename TPublish>
	EStage Unpack(std::int64_t Length, TRead &&Read, TWrite &&Write, TCloseInput &&CloseInput, TCloseOutput &&CloseOutput, TPublish &&Publish) noexcept
	{
		static_assert(std::is_nothrow_invocable_v<TCloseInput &>);
		static_assert(std::is_nothrow_invocable_v<TCloseOutput &, bool>);
		EStage Stage = Length < 0 ? EStage::INVALID_LENGTH : EStage::DONE;
		try
		{
			std::array<unsigned char, 16 * 1024> aBuffer{};
			for(auto Remaining = Length; Remaining > 0;)
			{
				const auto Requested = static_cast<std::size_t>(std::min(Remaining, static_cast<std::int64_t>(aBuffer.size())));
				std::size_t ReadBytes = 0;
				if(!Read(aBuffer.data(), Requested, ReadBytes) || ReadBytes == 0 || ReadBytes > Requested)
				{
					Stage = EStage::READ;
					break;
				}
				if(!Write(aBuffer.data(), ReadBytes))
				{
					Stage = EStage::WRITE;
					break;
				}
				Remaining -= static_cast<std::int64_t>(ReadBytes);
			}
		}
		catch(...)
		{
			Stage = EStage::PREPARATION;
		}
		const bool InputClosed = CloseInput();
		const bool OutputClosed = CloseOutput(Stage == EStage::DONE);
		if(Stage != EStage::DONE)
			return Stage;
		if(!InputClosed)
			return EStage::INPUT_CLOSE;
		if(!OutputClosed)
			return EStage::OUTPUT_CLOSE;
		try
		{
			return Publish() ? EStage::DONE : EStage::PUBLISH;
		}
		catch(...)
		{
			return EStage::PUBLISH;
		}
	}
}

#endif
