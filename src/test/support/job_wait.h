#ifndef TEST_SUPPORT_JOB_WAIT_H
#define TEST_SUPPORT_JOB_WAIT_H

#include <engine/shared/jobs.h>

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

// 成功路径先等待真正完成，再关闭作业池；关闭会取消仍在排队或运行的任务。
inline ::testing::AssertionResult WaitForJobDone(const IJob &Job)
{
	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while(!Job.Done() && std::chrono::steady_clock::now() < Deadline)
		std::this_thread::yield();
	if(Job.State() != IJob::STATE_DONE)
		return ::testing::AssertionFailure() << "job did not finish within two seconds, state=" << Job.State();
	return ::testing::AssertionSuccess();
}

#endif
