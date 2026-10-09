#include <engine/http.h>

#include <game/client/components/qmclient/skin_download_session.h>

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
			auto pSession = std::make_unique<CQmSkinDownloadSession>(Http, "https://benchmark.test/skin.png", "", std::make_shared<CUnavailableSkinJob>(), [](const char *, bool) { return std::make_shared<CPendingSkinRequest>(); }, [](std::shared_ptr<IHttpRequest>) -> std::shared_ptr<IQmSkinDataJob> { return nullptr; }, [](std::shared_ptr<IJob>) {});
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
