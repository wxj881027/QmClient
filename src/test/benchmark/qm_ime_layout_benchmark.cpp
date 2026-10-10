#include <game/client/QmUi/QmImeCandidateLayout.h>

#include <benchmark/benchmark.h>

static void BM_ImeCandidatePageLayout(benchmark::State &State)
{
	const int Count = static_cast<int>(State.range(0));
	std::array<qm_ime_overlay::SCandidateMeasure, qm_ime_overlay::MAX_CANDIDATES> aMeasures;
	aMeasures.fill({18.0f, 40.0f});
	qm_ime_overlay::SCandidateLayoutConfig Config;
	Config.m_Gap = 6.0f;
	Config.m_TrailingWidth = 24.0f;
	Config.m_PaddingX = 6.0f;
	Config.m_MinTextWidth = 16.0f;
	Config.m_MaxPanelWidth = 450.0f;
	const CUIRect Panel = {4.0f, 10.0f, 450.0f, 22.0f};
	if(qm_ime_overlay::BuildCandidateRowLayoutForPanel(aMeasures, Count, Config, Panel).m_Count != Count)
	{
		State.SkipWithError("IME page dropped candidates");
		return;
	}
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(aMeasures);
		const auto Layout = qm_ime_overlay::BuildCandidateRowLayoutForPanel(aMeasures, Count, Config, Panel);
		benchmark::DoNotOptimize(Layout);
	}
	State.SetItemsProcessed(State.iterations() * Count);
}
BENCHMARK(BM_ImeCandidatePageLayout)->Arg(7)->Arg(9)->Arg(16);
