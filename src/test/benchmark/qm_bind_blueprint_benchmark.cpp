#include <game/client/components/qmclient/bind_workshop.h>

#include <benchmark/benchmark.h>

// 蓝图画布的性能预算：视口查询与编译都不能随节点总数线性增长。
// 参数化 fixture 必须用 DEFINE_F + REGISTER_F(...)->Arg(n)，BENCHMARK_F 会双重注册。
namespace
{
	using namespace qm_bind_workshop;

	class QmBlueprintBenchmark : public benchmark::Fixture
	{
	public:
		void SetUp(const benchmark::State &State) override
		{
			m_Graph.Clear();
			SNode Event;
			Event.m_Type = ENodeType::EVENT_PRESS;
			const int EventId = m_Graph.AddNode(Event);
			int Previous = EventId;
			for(int Index = 0; Index < State.range(0); ++Index)
			{
				SNode Node;
				Node.m_X = static_cast<float>(Index % 40) * 200.0f;
				Node.m_Y = static_cast<float>(Index / 40) * 120.0f;
				Node.m_Command = "echo node";
				const int Id = m_Graph.AddNode(Node);
				m_Graph.Connect(Previous, Id);
				Previous = Id;
			}
		}

		CBlueprintGraph m_Graph;
	};

	BENCHMARK_DEFINE_F(QmBlueprintBenchmark, VisibleNodes)(benchmark::State &State)
	{
		for(auto _ : State)
		{
			const std::vector<int> vVisible = m_Graph.VisibleNodes(0.0f, 0.0f, 600.0f, 400.0f);
			benchmark::DoNotOptimize(vVisible.size());
		}
	}
	BENCHMARK_REGISTER_F(QmBlueprintBenchmark, VisibleNodes)->Arg(200)->Arg(1000)->Arg(5000);

	BENCHMARK_DEFINE_F(QmBlueprintBenchmark, MoveNode)(benchmark::State &State)
	{
		int Index = 1;
		for(auto _ : State)
		{
			m_Graph.MoveNode(Index, static_cast<float>(Index % 40) * 200.0f + 1.0f, 20.0f);
			Index = Index % static_cast<int>(m_Graph.Nodes().size()) + 1;
		}
	}
	BENCHMARK_REGISTER_F(QmBlueprintBenchmark, MoveNode)->Arg(1000)->Arg(5000);

	BENCHMARK_DEFINE_F(QmBlueprintBenchmark, Compile)(benchmark::State &State)
	{
		for(auto _ : State)
		{
			const SCompileResult Result = m_Graph.Compile("h");
			benchmark::DoNotOptimize(Result.m_Bytes);
		}
	}
	BENCHMARK_REGISTER_F(QmBlueprintBenchmark, Compile)->Arg(200)->Arg(1000);
}
