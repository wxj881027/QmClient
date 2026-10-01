// qm_text_benchmark.cpp：文本渲染 CPU 前端每字符路径基准（Google Benchmark）
// CGlyphMap（face 池/字形图集）依赖 IGraphics 无法独立构造，这里覆盖
// 字形解析前每字符必经的纯逻辑路径：
//  1. qm_font_category 码点分类（分类字体的字体槽路由查表）
//  2. str_utf8_decode UTF-8 解码（聊天/输入框/文本布局逐字符入口）
//  3. str_utf8_forward/rewind 光标导航（CLineInput 每键击的左右移动路径）
// 运行方式：配置时加 -DDOWNLOAD_BENCHMARK=ON，构建 run_cxx_benchmarks 目标
#include <base/str.h>
#include <base/system.h>

#include <engine/client/glyph_outline.h>
#include <engine/client/qm_font_category.h>
#include <engine/client/text_sweep.h>

#include <game/client/components/qmclient/nameplate_density.h>

#include <benchmark/benchmark.h>

// main / IsInterrupted 桩统一在 qm_benchmark_main.cpp 提供

// 混合码点分类：模拟中文 UI 文本（拉丁 / CJK / 图标符号约各占 1/3）
static void BM_FontCategoryClassify(benchmark::State &State)
{
	int aMixedCodepoints[] = {
		0x41,
		0x4E2D,
		0x2600,
		0x62,
		0x6C38,
		0x2603,
		0x43,
		0x6D53,
		0x2713,
		0x64,
		0x3042,
		0x2665,
		0x65,
		0xAC00,
		0x25B2,
		0x66,
		0xFF01,
		0x266A,
		0x67,
		0x4E00,
		0xE000,
		0x68,
		0x30A2,
		0x2728,
	};
	const int NumCodepoints = static_cast<int>(sizeof(aMixedCodepoints) / sizeof(aMixedCodepoints[0]));
	int *pCodepoints = aMixedCodepoints;
	benchmark::DoNotOptimize(pCodepoints);
	for(auto _ : State)
	{
		// 输入逃逸并加内存屏障，避免内联分类被折叠为常量计数。
		benchmark::ClobberMemory();
		int NumCjk = 0;
		int NumIcons = 0;
		for(int i = 0; i < NumCodepoints; i++)
		{
			const int Chr = pCodepoints[i];
			NumCjk += QmIsCjkCodepoint(Chr) ? 1 : 0;
			NumIcons += QmIsIconSymbolCodepoint(Chr) ? 1 : 0;
		}
		benchmark::DoNotOptimize(NumCjk);
		benchmark::DoNotOptimize(NumIcons);
	}
	State.SetItemsProcessed(State.iterations() * NumCodepoints);
}
BENCHMARK(BM_FontCategoryClassify);

// UTF-8 解码吞吐：中英混合文本逐字符解码（文本布局/输入处理第一层）
static void BM_StrUtf8DecodeMixed(benchmark::State &State)
{
	const char *pText = "QmClient 中文界面渲染 Benchmark ★ UTF-8 解码路径测试 42";
	const char *pEnd = pText + str_length(pText);
	for(auto _ : State)
	{
		int NumCodepoints = 0;
		const char *pCursor = pText;
		while(pCursor < pEnd)
		{
			const int Code = str_utf8_decode(&pCursor);
			NumCodepoints += Code != 0;
		}
		benchmark::DoNotOptimize(NumCodepoints);
	}
	State.SetItemsProcessed(State.iterations());
	State.SetBytesProcessed(State.iterations() * (pEnd - pText));
}
BENCHMARK(BM_StrUtf8DecodeMixed);

// 光标导航：混合宽度文本上前进到尾再回退到头（CLineInput 每键击左右移动路径）。
// 字符串含 1-4 字节字符（含 4 字节 emoji），覆盖 rewind 的多字节扫描。
static void BM_StrUtf8CursorNavigate(benchmark::State &State)
{
	const char *pText = "QmClient 输入框 ★ 光标导航 😀 测试 path 42";
	const int Len = str_length(pText);
	int NumChars = 0;
	for(const char *pCursor = pText; pCursor < pText + Len;)
	{
		str_utf8_decode(&pCursor);
		NumChars++;
	}
	for(auto _ : State)
	{
		int Cursor = 0;
		while(Cursor < Len)
			Cursor = str_utf8_forward(pText, Cursor); // 右方向键 ×N
		while(Cursor > 0)
			Cursor = str_utf8_rewind(pText, Cursor); // 左方向键 ×N
		benchmark::DoNotOptimize(Cursor);
	}
	State.SetItemsProcessed(State.iterations() * NumChars * 2); // 前进+回退各一遍
}
BENCHMARK(BM_StrUtf8CursorNavigate);

// UTF-8 整串校验：str_utf8_check 逐字节扫描（gamecore 字符串处理与
// fs.cpp 每目录项的真实路径，外来文本进入渲染前的守门步骤）
static void BM_StrUtf8Check(benchmark::State &State)
{
	const char *pText = "QmClient 中文界面渲染 Benchmark ★ UTF-8 校验路径测试 42 😀";
	const int Len = str_length(pText);
	for(auto _ : State)
	{
		int Valid = str_utf8_check(pText);
		benchmark::DoNotOptimize(Valid);
	}
	State.SetItemsProcessed(State.iterations()); // 完整字符串数量
	State.SetBytesProcessed(State.iterations() * Len);
}
BENCHMARK(BM_StrUtf8Check);

// 逐行扫光直接测量生产裁剪接口：窄/长聊天行及命中/未命中字形均计入。
static void BM_TextSweepLineClip(benchmark::State &State)
{
	using TQuad = std::array<STextSweepVertex, 4>;
	std::vector<TQuad> vGlyphs;
	for(int i = 0; i < State.range(0); ++i)
	{
		const float X = i * 12.0f;
		vGlyphs.push_back({{{vec2(X, 10.0f), vec2(0.0f, 1.0f), 1.0f},
			{vec2(X + 10.0f, 10.0f), vec2(1.0f, 1.0f), 1.0f},
			{vec2(X + 10.0f, 0.0f), vec2(1.0f, 0.0f), 1.0f},
			{vec2(X, 0.0f), vec2(0.0f, 0.0f), 1.0f}}});
	}
	int Frame = 0;
	for(auto _ : State)
	{
		const float Progress = float(Frame++ % 101) / 100.0f;
		const STextSweepBand Band{TextSweepCenter(0.0f, State.range(0) * 12.0f, 8.0f, Progress), 8.0f, 0.25f};
		size_t Fragments = 0;
		for(const auto &Quad : vGlyphs)
			TextSweepClipQuad(Quad, Band, [&](const auto &Fragment) {
				benchmark::DoNotOptimize(Fragment);
				++Fragments;
			});
		benchmark::DoNotOptimize(Fragments);
	}
	State.SetItemsProcessed(State.iterations() * State.range(0));
}
BENCHMARK(BM_TextSweepLineClip)->Arg(8)->Arg(64)->Arg(256);

// 同一缓冲对比普通描边与名牌连续描边；输入准备不计时，内核及 mask 生成计时。
static void BM_NameplateGlyphOutline(benchmark::State &State)
{
	const int Size = State.range(0);
	std::vector<unsigned char> Input(Size * Size), Output(Size * Size);
	for(int Y = Size / 4; Y < Size * 3 / 4; ++Y)
		for(int X = Size / 4; X < Size * 3 / 4; ++X)
			Input[Y * Size + X] = (X + Y) % 3 == 0 ? 127 : 255;
	for(auto _ : State)
	{
		if(State.range(1))
			QmGrowGlyphOutlineContinuous(Input.data(), Output.data(), Size, Size, QmNameplateGlyphOutlineRadius(Size));
		else
			QmGrowGlyphOutline(Input.data(), Output.data(), Size, Size, Size < 18 ? 1 : Size > 48 ? 4 :
														2);
		benchmark::DoNotOptimize(Output.data());
		benchmark::ClobberMemory();
	}
	State.SetItemsProcessed(State.iterations());
}
BENCHMARK(BM_NameplateGlyphOutline)->Args({18, 0})->Args({18, 1})->Args({48, 0})->Args({48, 1})->Args({128, 0})->Args({128, 1});

static void BM_NameplateDensityStable(benchmark::State &State)
{
	CQmNameplateDensity Density;
	int Budget = 16;
	Density.Update(1.0f, 1.0f, 0.14384104f, false, 6, 16, Budget);
	for(auto _ : State)
	{
		Budget = 16;
		benchmark::DoNotOptimize(Density);
		benchmark::DoNotOptimize(Density.Update(1.0f, 1.0f, 0.14384104f, false, 6, 16, Budget));
		benchmark::DoNotOptimize(Density.Revision());
	}
}
BENCHMARK(BM_NameplateDensityStable);
