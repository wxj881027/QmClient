#include <engine/http.h>

#include <game/client/components/qmclient/skin_download_session.h>
#include <game/client/components/settings_resource_jobs.h>

#include <benchmark/benchmark.h>

#include <memory>
#include <vector>

namespace
{
	class CUnavailableSkinJob : public IQmSkinDataJob
	{
		void Run() override {}

	public:
		CUnavailableSkinJob()
		{
			Abortable(true);
			Abort();
		}
		bool HasData() const override { return false; }
	};
	class CPendingSkinRequest : public IHttpRequest
	{
	public:
		CPendingSkinRequest() : IHttpRequest("https://benchmark.test/skin.png") { m_State = EHttpState::RUNNING; }
		void Header(const char *) override {}
	};
	class CPendingSkinHttp : public IHttp
	{
	public:
		void Run(std::shared_ptr<IHttpRequest>) override {}
		bool HasIpresolveBug() const override { return false; }
	};

	// 准备与取消在计时外；每轮轮询整批仍未完成的请求，计量单位为会话。
	void BM_QmSkinDownloadPendingPoll(benchmark::State &State)
	{
		CPendingSkinHttp Http;
		std::vector<std::unique_ptr<CQmSkinDownloadSession>> vSessions;
		for(int i = 0; i < State.range(0); ++i)
		{
			auto pSession = std::make_unique<CQmSkinDownloadSession>(Http, std::vector<CQmSkinDownloadSession::SSource>{{"https://benchmark.test/skin.png", ESkinSource::OFFICIAL}}, std::make_shared<CUnavailableSkinJob>(), [](const char *, bool) { return std::make_shared<CPendingSkinRequest>(); }, [](std::shared_ptr<IHttpRequest>) -> std::shared_ptr<IQmSkinDataJob> { return nullptr; }, [](std::shared_ptr<IJob>) {});
			if(pSession->Poll() != CQmSkinDownloadSession::EResult::WAITING)
			{
				State.SkipWithError("pending session setup failed");
				return;
			}
			vSessions.push_back(std::move(pSession));
		}
		for(auto _ : State)
			for(const auto &pSession : vSessions)
				benchmark::DoNotOptimize(pSession->Poll());
		State.SetItemsProcessed(State.iterations() * State.range(0));
	}
}
BENCHMARK(BM_QmSkinDownloadPendingPoll)->Arg(1)->Arg(32)->Arg(128);

// 每轮处理完整的混合优先级快照；准备在计时外，分类与遍历分配包含在计时内。
static void BM_QmSkinLoadRequestPriority(benchmark::State &State)
{
	std::vector<int> Requests;
	for(int i = 0; i < State.range(0); ++i)
		Requests.push_back(i);
	for(auto _ : State)
	{
		int Visited = 0;
		SettingsVisitSkinLoadRequests(Requests, [](int Index) { return static_cast<ESettingsResourcePriority>(Index % 3); }, [](int Index) { return Index < 32; }, [&](int Index) { benchmark::DoNotOptimize(Index); ++Visited; });
		benchmark::DoNotOptimize(Visited);
	}
	State.SetItemsProcessed(State.iterations() * State.range(0));
}
BENCHMARK(BM_QmSkinLoadRequestPriority)->Arg(32)->Arg(256)->Arg(2048);
