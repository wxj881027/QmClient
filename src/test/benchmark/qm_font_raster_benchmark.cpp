// qm_font_raster_benchmark.cpp：字形首次光栅化成本基准（Google Benchmark）
// 首开界面 spike 的 CPU 主因：每个新（字符, 字号）组合首次绘制都要 freetype
// 光栅化（text.cpp RenderGlyph，FT_LOAD_RENDER|FT_LOAD_NO_BITMAP，同款参数），
// 随后每字形一次 GPU 纹理更新（UploadGlyph→UpdateTextTexture，无预热无合批）。
// 本基准量化 CPU 侧：一次「冷启动画满一屏」= 256 个首见中文字形逐个光栅化。
// GPU 上传开销不在本基准内，游戏内 QmPerf 计数器（ConsumeQmPerfGlyphStats）可直接实测。
// 运行方式：配置时加 -DDOWNLOAD_BENCHMARK=ON，构建 run_cxx_benchmarks 目标
#include <benchmark/benchmark.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdint>
#include <cstdio>
#include <vector>

// main / IsInterrupted 桩统一在 qm_benchmark_main.cpp 提供

namespace
{
	// 简体中文主字体（fonts/index.json 的 simplified_chinese 默认面），构建目录含副本
	constexpr const char *CJK_FONT_PATH = "data/fonts/NotoSansSC-VF.ttf";
	constexpr int NUM_GLYPHS = 256; // 一屏文字的典型新字形数量

	// 256 个不重复的 CJK 码点（统一表意区按步长取样，笔画复杂度代表性一致）
	void MakeCjkCharset(std::vector<FT_ULong> &vCodepoints)
	{
		vCodepoints.clear();
		for(int i = 0; i < NUM_GLYPHS; i++)
			vCodepoints.push_back(static_cast<FT_ULong>(0x4E00 + i * 7));
	}
} // namespace

class FontRasterBenchmark : public benchmark::Fixture
{
public:
	void SetUp(const benchmark::State &State) override
	{
		const int FontSize = static_cast<int>(State.range(0));

		if(m_Library == nullptr && FT_Init_FreeType(&m_Library))
			return;
		if(m_pFace == nullptr)
		{
			FILE *pFile = std::fopen(CJK_FONT_PATH, "rb");
			if(pFile == nullptr)
				return;
			std::fseek(pFile, 0, SEEK_END);
			const long Size = std::ftell(pFile);
			std::fseek(pFile, 0, SEEK_SET);
			m_vFontData.resize(static_cast<size_t>(Size));
			const size_t Read = std::fread(m_vFontData.data(), 1, m_vFontData.size(), pFile);
			std::fclose(pFile);
			if(Read != m_vFontData.size() || FT_New_Memory_Face(m_Library, m_vFontData.data(), static_cast<FT_Long>(m_vFontData.size()), 0, &m_pFace))
				return;
		}
		if(FT_Set_Pixel_Sizes(m_pFace, 0, FontSize))
			return;

		MakeCjkCharset(m_vCharset);
		m_Ready = true;
	}

	FT_Library m_Library = nullptr;
	FT_Face m_pFace = nullptr;
	std::vector<uint8_t> m_vFontData;
	std::vector<FT_ULong> m_vCharset;
	bool m_Ready = false;
};

// 冷启动画一屏：256 个首见中文字形逐个光栅化（首开画字的 CPU 成本），
// Arg = 字号（像素）；每迭代 = 一次完整冷绘制
BENCHMARK_DEFINE_F(FontRasterBenchmark, BM_GlyphRasterizeColdPaint)(benchmark::State &State)
{
	if(!m_Ready)
	{
		State.SkipWithError("freetype/font init failed in SetUp");
		return;
	}
	for(auto _ : State)
	{
		int Ok = 0;
		for(FT_ULong Chr : m_vCharset)
		{
			const uint32_t GlyphIndex = FT_Get_Char_Index(m_pFace, Chr);
			if(GlyphIndex == 0)
				continue;
			if(!FT_Load_Glyph(m_pFace, GlyphIndex, FT_LOAD_RENDER | FT_LOAD_NO_BITMAP))
				++Ok;
		}
		benchmark::DoNotOptimize(Ok);
	}
	State.SetItemsProcessed(State.iterations() * NUM_GLYPHS);
}
BENCHMARK_REGISTER_F(FontRasterBenchmark, BM_GlyphRasterizeColdPaint)->Arg(16)->Arg(24)->Arg(36);
