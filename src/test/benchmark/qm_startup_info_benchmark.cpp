#include <game/client/components/qmclient/custom_community_info.h>
#include <game/client/components/qmclient/skin7_info_job.h>

#include <benchmark/benchmark.h>

#include <cstring>
#include <string>

namespace
{
	class CCommunityResponse : public IHttpRequest
	{
	public:
		explicit CCommunityResponse(const std::string &Body) : IHttpRequest("https://benchmark.test/communities.json")
		{
			WriteToMemory();
			m_pBuffer = static_cast<unsigned char *>(malloc(Body.size() + 1));
			memcpy(m_pBuffer, Body.c_str(), Body.size() + 1);
			m_ResponseLength = Body.size();
			m_State = EHttpState::DONE;
		}
		void Header(const char *) override {}
	};

	// 响应准备在计时外；每轮解析并释放完整响应，计量单位为响应字节。
	void BM_QmCustomCommunityRefresh(benchmark::State &State)
	{
		std::string Body = "{\"communities\":[";
		for(int i = 0; i < State.range(0); ++i)
		{
			if(i)
				Body += ',';
			Body += "{\"id\":\"community" + std::to_string(i) + "\",\"name\":\"Test Community\",\"has_finishes\":false,\"servers\":[{\"name\":\"Test\",\"flagId\":0,\"servers\":{\"Race\":[\"127.0.0.1:8303\"]}}]}";
		}
		Body += "]}";
		CCommunityResponse Response(Body);
		auto pCheck = QmLoadCustomCommunityInfo(nullptr, &Response);
		if(!pCheck || (*pCheck)["communities"].u.array.length != State.range(0))
		{
			State.SkipWithError("community response precheck failed");
			return;
		}
		for(auto _ : State)
		{
			auto pInfo = QmLoadCustomCommunityInfo(nullptr, &Response);
			benchmark::DoNotOptimize(pInfo.get());
		}
		State.SetBytesProcessed(State.iterations() * Body.size());
	}

	// 六个部件的一份皮肤描述；每轮解析和释放，磁盘读取由后台任务负责。
	void BM_QmSkin7InfoParse(benchmark::State &State)
	{
		const std::string Body = R"({"skin":{"body":{"filename":"standard","custom_colors":true,"hue":42,"sat":128,"lgt":128},"marking":{"filename":"stripes","custom_colors":"true","hue":10,"alp":128},"decoration":{"filename":"none"},"hands":{"filename":"standard"},"feet":{"filename":"standard"},"eyes":{"filename":"standard"}}})";
		if(!QmParseSkin7Info(Body.data(), Body.size()))
		{
			State.SkipWithError("skin description precheck failed");
			return;
		}
		for(auto _ : State)
		{
			auto pInfo = QmParseSkin7Info(Body.data(), Body.size());
			benchmark::DoNotOptimize(pInfo.get());
		}
		State.SetBytesProcessed(State.iterations() * Body.size());
	}
}

BENCHMARK(BM_QmCustomCommunityRefresh)->Arg(1)->Arg(32)->Arg(128);
BENCHMARK(BM_QmSkin7InfoParse);
