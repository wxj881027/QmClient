#ifndef TEST_SUPPORT_QM_TEST_ENGINE_H
#define TEST_SUPPORT_QM_TEST_ENGINE_H

#include <engine/engine.h>

#include <memory>

// 不启动作业线程的文字/渲染测试只满足接口依赖；后台队列必须明确拒绝，
// 防止配置误开 worker 后把任务交给一个永远不会执行的池。
class CQmRejectingTestEngine final : public IEngine
{
public:
	void Init() override {}
	void AddJob(std::shared_ptr<IJob>) override {}
	bool TryAddJob(std::shared_ptr<IJob>) override { return false; }
	void ShutdownJobs() override {}
	void SetAdditionalLogger(std::shared_ptr<ILogger> &&) override {}
};

#endif
