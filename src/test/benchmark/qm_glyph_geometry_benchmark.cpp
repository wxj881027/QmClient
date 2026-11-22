// 字形路线对比：CPU worker 的 FreeType 位图+描边，GPU 路线的轮廓展平/
// 环绕判定/三角化，以及离屏读回后的覆盖度降采样。真实 GPU 提交与读回
// 延迟在客户端 mode 2 perf 日志测，不能由这里的无设备基准替代。
#include <engine/client/qm_font_names.h>
#include <engine/client/qm_glyph_geometry.h>
#include <engine/client/qm_glyph_rasterizer.h>

#include <benchmark/benchmark.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

namespace
{
	constexpr const char *CJK_FONT_PATH = DDNET_TEST_SOURCE_DIR "/data/fonts/SourceHanSans.ttc";
	constexpr int NUM_GLYPHS = 256;
	constexpr int GPU_IMAGE_DIM = 1024;
	constexpr int GPU_SCALE = 2;

	class CGlyphGeometryFixture : public benchmark::Fixture
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
			m_pError = "CJK face or size initialization failed";
			if(FT_New_Memory_Face(m_Library, m_vFontData.data(), static_cast<FT_Long>(Size), 0, &m_pFace) ||
				FT_Set_Pixel_Sizes(m_pFace, 0, static_cast<FT_UInt>(State.range(0))))
				return;
			m_pError = "CJK font is missing a benchmark glyph";
			for(int i = 0; i < NUM_GLYPHS; ++i)
			{
				const FT_ULong Codepoint = static_cast<FT_ULong>(0x4E00 + i * 7);
				const FT_UInt GlyphIndex = FT_Get_Char_Index(m_pFace, Codepoint);
				if(GlyphIndex == 0)
					return;
				m_vGlyphIndices.push_back(GlyphIndex);
				SQmGlyphWorkerTask Task;
				Task.m_Key = std::make_tuple(m_pFace, static_cast<int>(Codepoint), static_cast<int>(State.range(0)), false);
				Task.m_GlyphIndex = GlyphIndex;
				Task.m_pFontData = m_vFontData.data();
				Task.m_FontDataSize = static_cast<FT_Long>(m_vFontData.size());
				Task.m_FaceIndex = 0;
				Task.m_FontSize = static_cast<int>(State.range(0));
				Task.m_GpuGeometry = false;
				m_vTasks.push_back(std::move(Task));
			}
			m_pError = nullptr;
		}

		void TearDown(const benchmark::State &) override
		{
			if(m_pFace != nullptr)
				FT_Done_Face(m_pFace);
			if(m_Library != nullptr)
				FT_Done_FreeType(m_Library);
			m_pFace = nullptr;
			m_Library = nullptr;
			m_vFontData.clear();
			m_vGlyphIndices.clear();
			m_vTasks.clear();
			m_pError = "fixture not initialized";
		}

		FT_Library m_Library = nullptr;
		FT_Face m_pFace = nullptr;
		std::vector<uint8_t> m_vFontData;
		std::vector<FT_UInt> m_vGlyphIndices;
		std::vector<SQmGlyphWorkerTask> m_vTasks;
		const char *m_pError = "fixture not initialized";
	};
} // namespace

// 与生产 worker 使用同一个 CQmGlyphRasterizer；每 32 个字形重建一次
// rasterizer，匹配 QM_GLYPH_JOB_BATCH 的 job 边界。
BENCHMARK_DEFINE_F(CGlyphGeometryFixture, BM_GlyphCpuWorkerBatch)(benchmark::State &State)
{
	if(m_pError != nullptr)
	{
		State.SkipWithError(m_pError);
		return;
	}
	State.SetLabel("cpu-worker-raster+outline;SourceHanSans-face0;no-gpu;v1");
	for(auto _ : State)
	{
		size_t Glyphs = 0;
		for(size_t BatchStart = 0; BatchStart < m_vTasks.size(); BatchStart += 32)
		{
			CQmGlyphRasterizer Rasterizer;
			const size_t BatchEnd = std::min(BatchStart + 32, m_vTasks.size());
			for(size_t i = BatchStart; i < BatchEnd; ++i)
			{
				SQmGlyphBitmap Bitmap;
				const EQmGlyphRenderResult Result = Rasterizer.Rasterize(m_vTasks[i], Bitmap);
				if(Result != EQmGlyphRenderResult::SUCCESS)
				{
					State.SkipWithError("production CPU worker rasterization failed");
					break;
				}
				benchmark::DoNotOptimize(Bitmap.m_vFill.data());
				benchmark::DoNotOptimize(Bitmap.m_vOutline.data());
				++Glyphs;
			}
			if(State.skipped())
				break;
		}
		benchmark::ClobberMemory();
		if(State.skipped())
			break;
		State.counters["glyphs"] = static_cast<double>(Glyphs);
	}
	State.SetItemsProcessed(State.iterations() * NUM_GLYPHS);
}
BENCHMARK_REGISTER_F(CGlyphGeometryFixture, BM_GlyphCpuWorkerBatch)->Arg(16)->Arg(24)->Arg(36)->Arg(48);

BENCHMARK_DEFINE_F(CGlyphGeometryFixture, BM_GlyphGeometryBatch)(benchmark::State &State)
{
	if(m_pError != nullptr)
	{
		State.SkipWithError(m_pError);
		return;
	}
	SQmGlyphGeometry Geometry;
	int64_t Triangles = 0;
	std::vector<FT_UInt> vUsable;
	vUsable.reserve(m_vGlyphIndices.size());
	int Failures = 0;
	std::string FirstFailure;
	for(int i = 0; i < NUM_GLYPHS; ++i)
	{
		const FT_Error LoadError = FT_Load_Glyph(m_pFace, m_vGlyphIndices[i], FT_LOAD_NO_BITMAP);
		if(LoadError != 0)
		{
			++Failures;
			continue;
		}
		if(!QmBuildGlyphGeometry(m_pFace->glyph->outline, m_pFace->glyph->metrics, static_cast<int>(State.range(0)), false,
			   (m_pFace->glyph->outline.flags & FT_OUTLINE_EVEN_ODD_FILL) != 0, Geometry))
		{
			++Failures;
			std::string Error = "production glyph geometry failed at glyph " + std::to_string(i) + " stage=" + std::to_string(qm_glyph_geometry_detail::g_LastFailureStage) + " contours=" + std::to_string(m_pFace->glyph->outline.n_contours) + " points=" + std::to_string(m_pFace->glyph->outline.n_points) +
					    " clean=" + std::to_string(qm_glyph_geometry_detail::g_DebugCleanedPoints) + " ear_fail=" + std::to_string(qm_glyph_geometry_detail::g_DebugEarFailPoints) + " area=" + std::to_string(qm_glyph_geometry_detail::g_DebugSignedArea);
			for(size_t PointIndex = 0; PointIndex < qm_glyph_geometry_detail::g_DebugRemainingCount; ++PointIndex)
			{
				const auto &Point = qm_glyph_geometry_detail::g_aDebugRemaining[PointIndex];
				Error += " r" + std::to_string(PointIndex) + "=(" + std::to_string(Point.m_X) + "," + std::to_string(Point.m_Y) + ")";
			}
			if(FirstFailure.empty())
				FirstFailure = std::move(Error);
			continue;
		}
		vUsable.push_back(m_vGlyphIndices[i]);
		for(const SQmGlyphContour &Contour : Geometry.m_vContours)
			Triangles += static_cast<int64_t>(Contour.m_vTriangles.size());
	}
	if(vUsable.empty() || Failures > NUM_GLYPHS / 8)
		State.SkipWithError(FirstFailure.empty() ? "no usable glyph geometry" : FirstFailure);
	if(State.skipped())
		return;
	State.SetLabel("gpu-outline-flatten+triangulate;no-device;v1");
	for(auto _ : State)
	{
		for(FT_UInt GlyphIndex : vUsable)
		{
			if(FT_Load_Glyph(m_pFace, GlyphIndex, FT_LOAD_NO_BITMAP) != 0 ||
				!QmBuildGlyphGeometry(m_pFace->glyph->outline, m_pFace->glyph->metrics, static_cast<int>(State.range(0)), false,
					(m_pFace->glyph->outline.flags & FT_OUTLINE_EVEN_ODD_FILL) != 0, Geometry))
			{
				State.SkipWithError("production glyph geometry failed");
				break;
			}
			benchmark::DoNotOptimize(Geometry.m_vContours.data());
		}
		benchmark::ClobberMemory();
		if(State.skipped())
			break;
	}
	State.SetItemsProcessed(State.iterations() * static_cast<int64_t>(vUsable.size()));
	State.counters["triangles_per_batch"] = static_cast<double>(Triangles);
	State.counters["glyphs_per_batch"] = static_cast<double>(vUsable.size());
	State.counters["geometry_failures"] = Failures;
}
BENCHMARK_REGISTER_F(CGlyphGeometryFixture, BM_GlyphGeometryBatch)->Arg(16)->Arg(24)->Arg(36)->Arg(48);

static void BM_GpuGlyphCoverageDownsample(benchmark::State &State)
{
	const int CellWidth = static_cast<int>(State.range(0));
	const int CellHeight = static_cast<int>(State.range(1));
	const int Columns = GPU_IMAGE_DIM / (CellWidth * GPU_SCALE);
	const int Rows = GPU_IMAGE_DIM / (CellHeight * GPU_SCALE);
	const int CellCount = Columns * Rows;
	if(CellCount <= 0)
	{
		State.SkipWithError("benchmark cell does not fit GPU image");
		return;
	}
	std::vector<uint8_t> vRgba(static_cast<size_t>(GPU_IMAGE_DIM) * GPU_IMAGE_DIM * 4, 255);
	// 每个 cell 画一个覆盖约一半面积的黑块，模拟字形而非全空白区域。
	for(int Row = 0; Row < Rows; ++Row)
	{
		for(int Column = 0; Column < Columns; ++Column)
		{
			const int BaseX = Column * CellWidth * GPU_SCALE;
			const int BaseY = Row * CellHeight * GPU_SCALE;
			for(int y = BaseY + (CellHeight * GPU_SCALE) / 4; y < BaseY + (CellHeight * GPU_SCALE) * 3 / 4; ++y)
				for(int x = BaseX + (CellWidth * GPU_SCALE) / 4; x < BaseX + (CellWidth * GPU_SCALE) * 3 / 4; ++x)
					for(int Channel = 0; Channel < 3; ++Channel)
						vRgba[(static_cast<size_t>(y) * GPU_IMAGE_DIM + x) * 4 + Channel] = 0;
		}
	}
	std::vector<uint8_t> vFill(static_cast<size_t>(CellWidth) * CellHeight);
	for(auto _ : State)
	{
		for(int Row = 0; Row < Rows; ++Row)
		{
			for(int Column = 0; Column < Columns; ++Column)
			{
				if(!QmDownsampleGpuGlyphCoverage(vRgba.data(), GPU_IMAGE_DIM, GPU_IMAGE_DIM,
					   Column * CellWidth * GPU_SCALE, Row * CellHeight * GPU_SCALE,
					   CellWidth, CellHeight, GPU_SCALE, EQmGlyphFillRule::EVEN_ODD, vFill.data()))
				{
					State.SkipWithError("production GPU coverage downsampling failed");
					break;
				}
				benchmark::DoNotOptimize(vFill.data());
			}
			if(State.skipped())
				break;
		}
		if(State.skipped())
			break;
	}
	State.SetItemsProcessed(State.iterations() * CellCount);
	State.SetLabel("gpu-readback-downsample;2x-ssaa;no-device;v1");
	State.counters["cells_per_batch"] = CellCount;
}
BENCHMARK(BM_GpuGlyphCoverageDownsample)->Args({32, 32})->Args({48, 48})->Args({64, 64})->Args({96, 96});
