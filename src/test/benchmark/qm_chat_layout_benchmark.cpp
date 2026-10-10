#include <game/client/components/qmclient/chat_input_layout.h>
#include <game/client/components/qmclient/chat_scrollbar.h>
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

// 每次测量真实 HUD 映射后的裁剪计算；不启动 GPU 或客户端。
static void BM_ChatInputViewportClip(benchmark::State &State)
{
	SQmChatViewport Viewport{{-50.0f, 25.0f, static_cast<float>(State.range(0)), 300.0f}, vec2(1920.0f, 1080.0f)};
	CUIRect Body{5.0f, 250.0f, 150.0f, 20.0f};
	const auto Check = Viewport.ClipPixels(Body);
	if(Check.w <= 0.0f || Check.h <= 0.0f)
	{
		State.SkipWithError("Chat body clip is empty");
		return;
	}
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(Viewport);
		benchmark::DoNotOptimize(Body);
		const auto Clip = Viewport.ClipPixels(Body);
		benchmark::DoNotOptimize(Clip);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_ChatInputViewportClip)->Arg(400)->Arg(800);

// 计算游戏与预览共用的轨道和滑块；左右参数覆盖相同生产职责。
static void BM_ChatScrollbarGeometry(benchmark::State &State)
{
	CUIRect Bounds{0.0f, 50.0f, 200.0f, 250.0f};
	const bool Right = State.range(0) != 0;
	const auto Check = QmChatScrollbarRail(Bounds, 100.0f, 160.0f, Right);
	if(Check.w <= 0.0f || Check.x < Bounds.x || Check.x + Check.w > Bounds.x + Bounds.w)
	{
		State.SkipWithError("Chat scrollbar is outside bounds");
		return;
	}
	for(auto _ : State)
	{
		benchmark::DoNotOptimize(Bounds);
		const auto Rail = QmChatScrollbarRail(Bounds, 100.0f, 160.0f, Right);
		const auto Handle = QmChatScrollbarHandle(Rail, QmChatScrollbarHandleHeight(Rail.h, 5, 50), 0.5f);
		benchmark::DoNotOptimize(Handle);
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_ChatScrollbarGeometry)->Arg(0)->Arg(1);
