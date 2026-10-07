// 字形查表与光栅化批次的 CPU 成本；不包含字体文件加载、应用字形缓存或 GPU 上传。
// 每次重复独立初始化 FreeType，所有迭代都显式光栅化完整字符集。
#include <engine/client/qm_font_names.h>

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
	constexpr const char *CJK_FONT_PATH = DDNET_TEST_SOURCE_DIR "/data/fonts/SourceHanSans.ttc";
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
	State.SetLabel("lookup+raster;SourceHanSans-face0;no-gpu;v3");
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

// 字体名称解析的应用缓存冷路径与稳态。文件读取、FreeType 初始化、SFNT 解码和
// 正确性预检都在 fixture 准备期；这里的冷指应用名称 lookup 缓存，不指磁盘或 FreeType。
// 参数是池内真实 face 数：4 个 Poppins，或再加 Source Han TTC 的 10 个区域面。
class FontNameLookupBenchmark : public benchmark::Fixture
{
protected:
	struct SQuery
	{
		const char *m_pName;
		FT_Face m_Expected;
	};
	FT_Library m_Library = nullptr;
	std::vector<std::vector<FT_Byte>> m_vFiles;
	std::vector<FT_Face> m_vFaces;
	std::unordered_map<FT_Face, SQmFontFaceNames> m_Names;
	std::vector<SQuery> m_vCanonicalQueries, m_vLegacyQueries;
	CQmFontFaceLookupCache m_ColdCache, m_WarmCache;
	const char *m_pError = "font name fixture not initialized";

	bool LoadFile(const char *pPath, bool Collection)
	{
		std::ifstream File(pPath, std::ios::binary | std::ios::ate);
		const std::streamoff Size = File ? static_cast<std::streamoff>(File.tellg()) : 0;
		if(Size <= 0 || Size > std::numeric_limits<FT_Long>::max())
			return false;
		m_vFiles.emplace_back(static_cast<size_t>(Size));
		auto &Bytes = m_vFiles.back();
		File.seekg(0);
		if(!File.read(reinterpret_cast<char *>(Bytes.data()), Size))
			return false;
		FT_Long Count = 1;
		if(Collection)
		{
			FT_Face Header = nullptr;
			if(FT_New_Memory_Face(m_Library, Bytes.data(), static_cast<FT_Long>(Size), -1, &Header))
				return false;
			Count = Header->num_faces;
			FT_Done_Face(Header);
		}
		for(FT_Long Index = 0; Index < Count; ++Index)
		{
			FT_Face Face = nullptr;
			if(FT_New_Memory_Face(m_Library, Bytes.data(), static_cast<FT_Long>(Size), Index, &Face))
				return false;
			m_vFaces.push_back(Face);
			m_Names.emplace(Face, QmFontFaceNames(Face));
		}
		return true;
	}

	FT_Face Resolve(const char *pName)
	{
		return QmResolveFontFaceName(pName, m_vFaces, m_Names);
	}
	void Measure(benchmark::State &State, bool Legacy, bool Warm)
	{
		if(m_pError != nullptr)
		{
			State.SkipWithError(m_pError);
			return;
		}
		const auto &Queries = Legacy ? m_vLegacyQueries : m_vCanonicalQueries;
		auto &Cache = Warm ? m_WarmCache : m_ColdCache;
		State.SetLabel(Warm ? "application-lookup-hit;SFNT-prepared;no-glyph-or-gpu;v1" : "application-lookup-miss;reset-included;SFNT-prepared;no-glyph-or-gpu;v1");
		for(auto _ : State)
		{
			// 冷路径每轮清空并重新解析完整查询批次，清空/释放/插入成本计时。
			if(!Warm)
				Cache.Reset();
			for(const auto &Query : Queries)
			{
				FT_Face Face = Cache.Resolve(Query.m_pName, [this](const char *pName) { return Resolve(pName); });
				benchmark::DoNotOptimize(Face);
			}
		}
		State.SetItemsProcessed(State.iterations() * static_cast<int64_t>(Queries.size()));
		State.counters["faces_in_pool"] = static_cast<double>(m_vFaces.size());
		State.counters["queries_per_iteration"] = static_cast<double>(Queries.size());
	}

public:
	void SetUp(const benchmark::State &State) override
	{
		m_pError = "FreeType initialization failed";
		if(FT_Init_FreeType(&m_Library))
			return;
		m_vFiles.reserve(5);
		m_pError = "bundled Poppins could not be loaded";
		for(const char *pStyle : {"Regular", "Light", "Medium", "Bold"})
		{
			const std::string Path = std::string(DDNET_TEST_SOURCE_DIR "/data/fonts/Poppins/Poppins-") + pStyle + ".ttf";
			if(!LoadFile(Path.c_str(), false))
				return;
		}
		m_vCanonicalQueries = {{"Poppins Light", m_vFaces[1]}, {"Poppins Medium", m_vFaces[2]}};
		m_vLegacyQueries = {{"Poppins Light Regular", m_vFaces[1]}, {"Poppins Medium Regular", m_vFaces[2]}};
		if(State.range(0) == 14)
		{
			m_pError = "bundled Source Han collection could not be loaded";
			if(!LoadFile(DDNET_TEST_SOURCE_DIR "/data/fonts/SourceHanSans.ttc", true) || m_vFaces.size() != 14)
				return;
			m_vCanonicalQueries.push_back({"Source Han Sans SC Regular", m_vFaces[6]});
			m_vCanonicalQueries.push_back({"Source Han Sans HW SC Regular", m_vFaces[11]});
			m_vLegacyQueries.push_back({"Source Han Sans SC", m_vFaces[6]});
			m_vLegacyQueries.push_back({"SourceHanSansHWSC", m_vFaces[11]});
		}
		m_pError = "production font name resolution precheck failed";
		for(const auto *pQueries : {&m_vCanonicalQueries, &m_vLegacyQueries})
		{
			for(const auto &Query : *pQueries)
			{
				if(Resolve(Query.m_pName) != Query.m_Expected || m_WarmCache.Resolve(Query.m_pName, [this](const char *pName) { return Resolve(pName); }) != Query.m_Expected)
					return;
			}
		}
		m_pError = nullptr;
	}
	void TearDown(const benchmark::State &) override
	{
		m_ColdCache.Reset();
		m_WarmCache.Reset();
		m_Names.clear();
		for(FT_Face Face : m_vFaces)
			FT_Done_Face(Face);
		m_vFaces.clear();
		m_vFiles.clear();
		m_vCanonicalQueries.clear();
		m_vLegacyQueries.clear();
		if(m_Library != nullptr)
			FT_Done_FreeType(m_Library);
		m_Library = nullptr;
		m_pError = "font name fixture not initialized";
	}
};

BENCHMARK_DEFINE_F(FontNameLookupBenchmark, BM_CanonicalLookupCold)(benchmark::State &State) { Measure(State, false, false); }
BENCHMARK_REGISTER_F(FontNameLookupBenchmark, BM_CanonicalLookupCold)->Arg(4)->Arg(14);
BENCHMARK_DEFINE_F(FontNameLookupBenchmark, BM_LegacyLookupCold)(benchmark::State &State) { Measure(State, true, false); }
BENCHMARK_REGISTER_F(FontNameLookupBenchmark, BM_LegacyLookupCold)->Arg(4)->Arg(14);
BENCHMARK_DEFINE_F(FontNameLookupBenchmark, BM_CanonicalLookupWarm)(benchmark::State &State) { Measure(State, false, true); }
BENCHMARK_REGISTER_F(FontNameLookupBenchmark, BM_CanonicalLookupWarm)->Arg(4)->Arg(14);
BENCHMARK_DEFINE_F(FontNameLookupBenchmark, BM_LegacyLookupWarm)(benchmark::State &State) { Measure(State, true, true); }
BENCHMARK_REGISTER_F(FontNameLookupBenchmark, BM_LegacyLookupWarm)->Arg(4)->Arg(14);
