#ifndef ENGINE_CLIENT_BACKEND_VULKAN_VULKAN_RECORDING_BATCH_H
#define ENGINE_CLIENT_BACKEND_VULKAN_VULKAN_RECORDING_BATCH_H

#include <atomic>
#include <condition_variable>
#include <type_traits>
#include <vector>

// 调用方在整个批次内持有工作线程锁；队列只借用绘制命令，不拥有上传数据。
template<typename TCommand, typename TRecord, typename TFinish, typename TAbort>
void RunVulkanRecordingBatch(std::vector<TCommand> &vCommands, const std::atomic<bool> &HasError, bool &IsRendering, std::condition_variable &Completion, TRecord &&Record, TFinish &&Finish, TAbort &&Abort) noexcept
{
	static_assert(std::is_nothrow_invocable_v<TAbort, bool>);
	static_assert(std::is_nothrow_destructible_v<TCommand>);
	bool Completed = false;
	bool Exception = false;
	try
	{
		bool Recorded = true;
		for(auto &Command : vCommands)
		{
			if(HasError || !Record(Command))
			{
				Recorded = false;
				break;
			}
		}
		if(Recorded && !HasError)
			Completed = Finish();
	}
	catch(...)
	{
		Exception = true;
	}
	if(!Completed)
		Abort(Exception);
	// 完成通知前释放借用；出错的命令和尾部命令都不能再次执行。
	vCommands.clear();
	IsRendering = false;
	Completion.notify_all();
}

#endif
