// 拖尾样式几何构建基准：每次迭代调用一次生产 qm_tee_trail::BuildEffect。
// 输入是一条由真实 CTrailState 采样出的弯曲变速轨迹；不复制样式算法，不触碰文件、网络或 GPU。
#include <game/client/components/qmclient/tee_trail_styles.h>
#include <game/client/components/tclient/qm_tee_trail.h>

#include <benchmark/benchmark.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace
{
	// 固定输入：所有 case 共用同一条轨迹和同一组渲染参数，便于跨样式比较单次构建成本。
	constexpr int TRAIL_TICKS = 60;
	constexpr double TRAIL_START_TIME = 100.0; // 游戏 tick 单位；绝对值不影响几何，只决定年龄基准
	constexpr float TRAIL_SAMPLE_LIFE = 200.0f; // 采样阶段寿命，保证 60 tick 内环形队列不被老化清空
	constexpr float TRAIL_WIDTH = 12.0f;
	constexpr float TRAIL_PIXEL_SIZE = 1.0f;
	constexpr int TRAIL_SEED = 20261101;
	constexpr float TRAIL_POINT_LIFE = 120.0f; // 导出后显式寿命，让每轮的剩余寿命分布完全一致
	constexpr bool TRAIL_PRESET_PALETTE = true; // 与 qm_tee_trail_style_colors 默认值一致

	// 满长度输入下 BuildEffect 的采样步长已由 MAX_RENDER_POINTS 预算决定，PixelSize 只改变柔边宽度、
	// 不改变每轮构建量，因此不再单列 0.25f 变体；需要观察屏幕空间细分时另建短轨迹 case。

	vec2 TrailPosition(double Distance)
	{
		// 两段不同波长的横向漂移叠加成连续弯道；斜率有界，相邻 tick 的位移不会触发跳变重置。
		const float S = float(Distance);
		return vec2(S, 68.0f * std::sin(S * 0.0042f) + 22.0f * std::sin(S * 0.0125f));
	}

	class CTrailStyleFixture
	{
		static std::vector<CTrailPart> SampleTrail()
		{
			qm_tee_trail::CTrailState State;
			double Distance = 0.0;
			double Time = TRAIL_START_TIME;
			for(int Tick = 0; Tick <= TRAIL_TICKS; ++Tick)
			{
				// 速度按正弦起伏约 10~40 世界单位/游戏 tick，低速与高速段的采样密度都会被覆盖。
				const float Speed = 25.0f + 15.0f * std::sin(float(Tick) * 0.27f);
				State.Update(TrailPosition(Distance), Time, Speed, TRAIL_SAMPLE_LIFE);
				Distance += double(Speed);
				Time += 1.0;
			}
			std::vector<CTrailPart> vTrail;
			State.Export(vTrail);
			// 时间轴保留生产采样值；颜色和寿命显式固定，避免几何成本随配置或当前帧漂移。
			for(size_t i = 0; i < vTrail.size(); ++i)
			{
				const float Along = vTrail.size() > 1 ? float(i) / float(vTrail.size() - 1) : 0.0f;
				vTrail[i].m_Col = ColorRGBA(1.0f - 0.55f * Along, 0.55f + 0.35f * Along, 0.45f + 0.55f * Along, 1.0f);
				vTrail[i].m_Life = TRAIL_POINT_LIFE;
			}
			return vTrail;
		}

	public:
		// 生产环形队列上限 192 个采样点，导出再加一个端帽，因此约 200 点就是实际最长轨迹。
		std::vector<CTrailPart> m_vTrail = SampleTrail();
		// 输出缓冲由 case 复用；首轮 BuildEffect 内部 reserve(MAX_QUADS) 之后每轮 clear 不再重新分配。
		std::vector<qm_tee_trail::SQuad> m_vQuads;
		// 与生产 CTrails 一样持有像素噪声缓存；预检首轮构建后计时循环处于热缓存稳态。
		qm_tee_trail::CPixelNoiseCache m_PixelNoiseCache;
		// CurTime 固定为端帽时间 +1：整条轨迹可见，且每轮年龄分布完全相同。
		double m_CurTime = m_vTrail.empty() ? 0.0 : m_vTrail.front().m_Time + 1.0;
	};

	bool FiniteQuads(const std::vector<qm_tee_trail::SQuad> &vQuads)
	{
		for(const auto &Quad : vQuads)
			for(int i = 0; i < 4; ++i)
				if(!std::isfinite(Quad.m_aPos[i].x) || !std::isfinite(Quad.m_aPos[i].y) ||
					!std::isfinite(Quad.m_aColor[i].r) || !std::isfinite(Quad.m_aColor[i].g) ||
					!std::isfinite(Quad.m_aColor[i].b) || !std::isfinite(Quad.m_aColor[i].a))
					return false;
		return true;
	}

	// 准备与预检在计时外；每轮迭代清空复用缓冲后只调用一次生产 BuildEffect，构建一条满长度轨迹。
	void RunTrailStyleGeometry(benchmark::State &State, int Style)
	{
		auto pFixture = std::make_unique<CTrailStyleFixture>();
		CTrailStyleFixture &Fixture = *pFixture;
		if(Fixture.m_vTrail.size() < qm_tee_trail::MAX_POINTS)
		{
			State.SkipWithError("production CTrailState did not retain the full trail");
			return;
		}
		const auto BuildOnce = [&] {
			qm_tee_trail::BuildEffect(Fixture.m_vTrail, Style, TRAIL_PRESET_PALETTE, Fixture.m_CurTime, TRAIL_WIDTH, TRAIL_SEED,
				Fixture.m_vQuads, TRAIL_PIXEL_SIZE, true, false, &Fixture.m_PixelNoiseCache);
		};
		// 预检一次生产构建：当前样式必须产出有界、有限且非空的几何，否则不输出貌似有效的耗时。
		Fixture.m_vQuads.clear();
		BuildOnce();
		if(Fixture.m_vQuads.empty() || Fixture.m_vQuads.size() > qm_tee_trail::MAX_QUADS || !FiniteQuads(Fixture.m_vQuads))
		{
			State.SkipWithError("production BuildEffect produced invalid geometry for this style");
			return;
		}
		size_t TotalQuads = 0;
		for(auto _ : State)
		{
			Fixture.m_vQuads.clear();
			BuildOnce();
			// 工作量单位：每次迭代构建 1 条满长度轨迹；四元组数量作为几何规模证据。
			TotalQuads += Fixture.m_vQuads.size();
			benchmark::DoNotOptimize(Fixture.m_vQuads.data());
		}
		State.SetItemsProcessed(State.iterations());
		State.counters["trail_points"] = static_cast<double>(Fixture.m_vTrail.size());
		State.counters["quads_per_trail"] = static_cast<double>(TotalQuads) / static_cast<double>(State.iterations());
		State.SetLabel("warm;cpu;production-build;preset-palette;width=12;px=1;points=" + std::to_string(Fixture.m_vTrail.size()));
	}

	void BM_TrailStyleGeometryOriginal(benchmark::State &State) { RunTrailStyleGeometry(State, qm_tee_trail::STYLE_ORIGINAL); }
	void BM_TrailStyleGeometryManga(benchmark::State &State) { RunTrailStyleGeometry(State, qm_tee_trail::STYLE_MANGA); }
	void BM_TrailStyleGeometryMagic(benchmark::State &State) { RunTrailStyleGeometry(State, qm_tee_trail::STYLE_MAGIC); }
	void BM_TrailStyleGeometryPixel(benchmark::State &State) { RunTrailStyleGeometry(State, qm_tee_trail::STYLE_PIXEL); }
	void BM_TrailStyleGeometryAurora(benchmark::State &State) { RunTrailStyleGeometry(State, qm_tee_trail::STYLE_AURORA); }
	void BM_TrailStyleGeometryInk(benchmark::State &State) { RunTrailStyleGeometry(State, qm_tee_trail::STYLE_INK); }
	void BM_TrailStyleGeometryIgnis(benchmark::State &State) { RunTrailStyleGeometry(State, qm_tee_trail::STYLE_IGNIS); }
	void BM_TrailStyleGeometryStardust(benchmark::State &State) { RunTrailStyleGeometry(State, qm_tee_trail::STYLE_STARDUST); }
	void BM_TrailStyleGeometryBlackFlash(benchmark::State &State) { RunTrailStyleGeometry(State, qm_tee_trail::STYLE_BLACKFLASH); }
}

// 每个已解析样式一个 case：旧配置编号 4/5 只由 ResolveStyle 映射，行为测试已覆盖，不重复注册。
BENCHMARK(BM_TrailStyleGeometryOriginal)->Name("TrailStyleGeometry/BuildEffectOriginal");
BENCHMARK(BM_TrailStyleGeometryManga)->Name("TrailStyleGeometry/BuildEffectManga");
BENCHMARK(BM_TrailStyleGeometryMagic)->Name("TrailStyleGeometry/BuildEffectMagic");
BENCHMARK(BM_TrailStyleGeometryPixel)->Name("TrailStyleGeometry/BuildEffectPixel");
BENCHMARK(BM_TrailStyleGeometryAurora)->Name("TrailStyleGeometry/BuildEffectAurora");
BENCHMARK(BM_TrailStyleGeometryInk)->Name("TrailStyleGeometry/BuildEffectInk");
BENCHMARK(BM_TrailStyleGeometryIgnis)->Name("TrailStyleGeometry/BuildEffectIgnis");
BENCHMARK(BM_TrailStyleGeometryStardust)->Name("TrailStyleGeometry/BuildEffectStardust");
BENCHMARK(BM_TrailStyleGeometryBlackFlash)->Name("TrailStyleGeometry/BuildEffectBlackFlash");
