// 字形查表与光栅化批次的 CPU 成本；不包含字体文件加载、应用字形缓存或 GPU 上传。
// 每次重复独立初始化 FreeType，所有迭代都显式光栅化完整字符集。
#include <benchmark/benchmark.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

namespace
{
	// 使用当前源码字体，独立于调用者的工作目录。
	constexpr const char *CJK_FONT_PATH = DDNET_TEST_SOURCE_DIR "/data/fonts/NotoSansSC-VF.ttf";
	constexpr int NUM_GLYPHS = 256;
} // namespace

class FontRasterBenchmark : public benchmark::Fixture
{
public:
	void SetUp(const benchmark::State &State) override
	{
		m_pError = "FreeType initialization failed";
		if(FT_Init_FreeType(&m_Library))
			return;

		m_pError = "CJK font could not be read";
		std::ifstream File(CJK_FONT_PATH, std::ios::binary | std::ios::ate);
		const std::streamoff Size = File ? static_cast<std::streamoff>(File.tellg()) : 0;
		if(Size <= 0 || Size > std::numeric_limits<FT_Long>::max())
			return;
		m_vFontData.resize(static_cast<size_t>(Size));
		File.seekg(0);
		if(!File.read(reinterpret_cast<char *>(m_vFontData.data()), Size))
			return;

		m_pError = "CJK font face or size initialization failed";
		if(FT_New_Memory_Face(m_Library, m_vFontData.data(), static_cast<FT_Long>(Size), 0, &m_pFace) ||
			FT_Set_Pixel_Sizes(m_pFace, 0, static_cast<FT_UInt>(State.range(0))))
			return;

		m_pError = "CJK font is missing a benchmark glyph";
		for(int i = 0; i < NUM_GLYPHS; i++)
		{
			const FT_ULong Codepoint = static_cast<FT_ULong>(0x4E00 + i * 7);
			if(FT_Get_Char_Index(m_pFace, Codepoint) == 0)
				return;
			m_vCharset.push_back(Codepoint);
		}
		m_pError = nullptr;
	}

	void TearDown(const benchmark::State &) override
	{
		// Face 引用内存字体，先销毁 Face，再释放字节及 Library。
		if(m_pFace != nullptr)
			FT_Done_Face(m_pFace);
		if(m_Library != nullptr)
			FT_Done_FreeType(m_Library);
		m_pFace = nullptr;
		m_Library = nullptr;
		m_vFontData.clear();
		m_vCharset.clear();
		m_pError = "fixture not initialized";
	}

	FT_Library m_Library = nullptr;
	FT_Face m_pFace = nullptr;
	std::vector<uint8_t> m_vFontData;
	std::vector<FT_ULong> m_vCharset;
	const char *m_pError = "fixture not initialized";
};

// 批次内每个字符都执行生产 FreeType API；不是应用字形缓存命中的稳态绘制。
BENCHMARK_DEFINE_F(FontRasterBenchmark, BM_GlyphRasterizeBatch)(benchmark::State &State)
{
	if(m_pError != nullptr)
	{
		State.SkipWithError(m_pError);
		return;
	}
	State.SetLabel("lookup+raster;no-gpu;v2");
	for(auto _ : State)
	{
		for(FT_ULong Chr : m_vCharset)
		{
			const FT_UInt GlyphIndex = FT_Get_Char_Index(m_pFace, Chr);
			if(FT_Load_Glyph(m_pFace, GlyphIndex, FT_LOAD_RENDER | FT_LOAD_NO_BITMAP))
			{
				State.SkipWithError("glyph rasterization failed");
				break;
			}
			benchmark::DoNotOptimize(m_pFace->glyph->bitmap.buffer);
		}
		if(State.skipped())
			break;
	}
	State.SetItemsProcessed(State.iterations() * NUM_GLYPHS);
}
BENCHMARK_REGISTER_F(FontRasterBenchmark, BM_GlyphRasterizeBatch)->Arg(16)->Arg(24)->Arg(36);
