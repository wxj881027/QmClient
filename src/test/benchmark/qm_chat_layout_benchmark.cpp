#include <game/client/components/qmclient/chat_input_layout.h>
#include <game/client/components/qmclient/qm_chat_text_layout.h>

#include <benchmark/benchmark.h>

static void BM_ChatInputLayout(benchmark::State &State)
{
	float LineWidth = static_cast<float>(State.range(0));
	if(QmChatResolveInputLayout(5.0f, 100.0f, LineWidth, 20.0f, 60.0f).m_MessageMaxWidth <= 1.0f)
	{
		State.SkipWithError("Chat input has no usable body width");
		return;
	}
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(LineWidth);
		const auto Layout = QmChatResolveInputLayout(5.0f, 100.0f, LineWidth, 20.0f, 60.0f);
		benchmark::DoNotOptimize(Layout);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_ChatInputLayout)->Arg(190)->Arg(800);

static void BM_ChatMessageIndent(benchmark::State &State)
{
	CTextCursor Original;
	Original.SetPosition(vec2(20.0f, 50.0f));
	Original.m_LineWidth = 200.0f;
	Original.m_X = static_cast<float>(State.range(0));
	CTextCursor Check = Original;
	QmChatApplyMessageIndent(Check);
	if(Check.m_LineWidth < 100.0f || Check.m_StartX + Check.m_LineWidth != 220.0f)
	{
		State.SkipWithError("Chat indent collapses body or moves the right edge");
		return;
	}
	for(auto _ : State)
	{
		// 每次从前缀布局重新生成正文游标，不能对已缩进游标重复计时。
		benchmark::DoNotOptimize(Original);
		CTextCursor Cursor = Original;
		QmChatApplyMessageIndent(Cursor);
		benchmark::DoNotOptimize(Cursor);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_ChatMessageIndent)->Arg(50)->Arg(219);
