// 名牌 MSDF 图集资产测试：生成产物必须自洽，避免发布一个「能加载但画不对」的图集。
//
// 图集由 qmclient_scripts/qm_nameplate_msdf_build.py 离线生成（msdfgen + FreeType），
// 运行时不参与生成，因此这里直接校验提交进仓库的产物本身。
#include <engine/shared/json.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
	constexpr const char *kBaseManifest = "data/qmclient/nameplate_msdf/nameplate_base_msdf.json";
	constexpr const char *kCjkManifest = "data/qmclient/nameplate_msdf/nameplate_cjk_msdf.json";
	constexpr const char *kBaseImage = "data/qmclient/nameplate_msdf/nameplate_base_msdf.png";
	constexpr const char *kCjkImage = "data/qmclient/nameplate_msdf/nameplate_cjk_msdf.png";

	// PNG IHDR：宽高为偏移 16/20 的大端 32 位整数
	bool ReadPngSize(const std::string &Bytes, uint32_t &Width, uint32_t &Height)
	{
		static const unsigned char s_aSignature[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
		if(Bytes.size() < 24)
			return false;
		if(mem_comp(Bytes.data(), s_aSignature, sizeof(s_aSignature)) != 0)
			return false;
		auto ReadBigEndian = [&](size_t Offset) {
			return (uint32_t)((unsigned char)Bytes[Offset] << 24) | ((uint32_t)(unsigned char)Bytes[Offset + 1] << 16) |
			       ((uint32_t)(unsigned char)Bytes[Offset + 2] << 8) | (uint32_t)(unsigned char)Bytes[Offset + 3];
		};
		Width = ReadBigEndian(16);
		Height = ReadBigEndian(20);
		return Width > 0 && Height > 0;
	}

	struct SAtlasFacts
	{
		int m_PxRange = 0;
		int m_EmPixels = 0;
		int m_Padding = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		std::string m_Kind;
		std::string m_Image;
	};

	// 校验单页 manifest 的结构与不变量，返回该页事实（解析失败时返回 false）
	bool CheckPage(const char *pManifestPath, const char *pImagePath, SAtlasFacts &Facts, size_t &GlyphCount)
	{
		const std::string Json = ReadTestSourceFile(pManifestPath);
		if(Json.empty())
			return false;

		json_value *pRoot = JsonParse(Json.c_str(), Json.size());
		if(pRoot == nullptr)
			return false;

		bool Ok = true;
		auto IntField = [&](const char *pName) {
			const json_value *pValue = json_object_get(pRoot, pName);
			if(pValue == nullptr || pValue->type != json_integer)
			{
				Ok = false;
				return 0;
			}
			return (int)pValue->u.integer;
		};
		Facts.m_PxRange = IntField("px_range");
		Facts.m_EmPixels = IntField("em_pixels");
		Facts.m_Padding = IntField("padding");

		const json_value *pKind = json_object_get(pRoot, "kind");
		if(pKind != nullptr && pKind->type == json_string)
			Facts.m_Kind = pKind->u.string.ptr;

		const json_value *pAtlas = json_object_get(pRoot, "atlas");
		const json_value *pGlyphs = json_object_get(pRoot, "glyphs");
		if(pAtlas == nullptr || pAtlas->type != json_object || pGlyphs == nullptr || pGlyphs->type != json_object)
			return false;

		const json_value *pWidth = json_object_get(pAtlas, "width");
		const json_value *pHeight = json_object_get(pAtlas, "height");
		const json_value *pImage = json_object_get(pAtlas, "image");
		if(pWidth == nullptr || pWidth->type != json_integer || pHeight == nullptr || pHeight->type != json_integer)
			return false;
		Facts.m_Width = (uint32_t)pWidth->u.integer;
		Facts.m_Height = (uint32_t)pHeight->u.integer;
		if(pImage != nullptr && pImage->type == json_string)
			Facts.m_Image = pImage->u.string.ptr;

		// 图形上传的是 RGBA 页，PNG 尺寸必须与 manifest 声明一致
		const std::string PngBytes = ReadTestSourceFile(pImagePath);
		uint32_t PngWidth = 0;
		uint32_t PngHeight = 0;
		EXPECT_TRUE(ReadPngSize(PngBytes, PngWidth, PngHeight)) << pImagePath;
		EXPECT_EQ(PngWidth, Facts.m_Width) << pImagePath;
		EXPECT_EQ(PngHeight, Facts.m_Height) << pImagePath;

		GlyphCount = 0;
		for(unsigned i = 0; i < pGlyphs->u.object.length; ++i)
		{
			const json_value *pEntry = pGlyphs->u.object.values[i].value;
			if(pEntry == nullptr || pEntry->type != json_object)
			{
				Ok = false;
				continue;
			}
			const json_value *pOutline = json_object_get(pEntry, "outline");
			const bool HasOutline = pOutline != nullptr && pOutline->type == json_boolean && pOutline->u.boolean != 0;
			auto EntryInt = [&](const char *pName) {
				const json_value *pValue = json_object_get(pEntry, pName);
				return pValue != nullptr && pValue->type == json_integer ? (int)pValue->u.integer : 0;
			};
			if(HasOutline)
			{
				const int X = EntryInt("x");
				const int Y = EntryInt("y");
				const int W = EntryInt("w");
				const int H = EntryInt("h");
				if(X < 0 || Y < 0 || W <= 0 || H <= 0)
				{
					Ok = false;
					continue;
				}
				// 字形矩形必须完整落在图集内，否则采样会整块取错
				if((uint32_t)(X + W) > Facts.m_Width || (uint32_t)(Y + H) > Facts.m_Height)
				{
					Ok = false;
					continue;
				}
			}
			++GlyphCount;
		}
		json_value_free(pRoot);
		return Ok;
	}
}

// pxRange 之外的 padding 必须留足，否则字形边缘的双线性采样会串到邻居字形上
TEST(QmNameplateMsdfAtlas, ManifestsAreSelfConsistent)
{
	SAtlasFacts BaseFacts;
	size_t BaseGlyphs = 0;
	EXPECT_TRUE(CheckPage(kBaseManifest, kBaseImage, BaseFacts, BaseGlyphs));
	EXPECT_EQ(BaseFacts.m_Kind, "msdf-glyphs");
	EXPECT_GT(BaseFacts.m_PxRange, 0);
	EXPECT_GT(BaseFacts.m_EmPixels, 0);
	EXPECT_GE(BaseFacts.m_Padding, BaseFacts.m_PxRange + 1);
	EXPECT_GT(BaseGlyphs, 900u);

	SAtlasFacts CjkFacts;
	size_t CjkGlyphs = 0;
	EXPECT_TRUE(CheckPage(kCjkManifest, kCjkImage, CjkFacts, CjkGlyphs));
	EXPECT_EQ(CjkFacts.m_Kind, "msdf-glyphs");
	EXPECT_EQ(CjkFacts.m_PxRange, BaseFacts.m_PxRange);
	EXPECT_EQ(CjkFacts.m_EmPixels, BaseFacts.m_EmPixels);
	EXPECT_GE(CjkFacts.m_Padding, CjkFacts.m_PxRange + 1);
	// 3500 个常用汉字是资源脚本的目标规模；明显偏小说明图集被截断
	EXPECT_GE(CjkGlyphs, 3000u);
}

// 基础页必须覆盖 ASCII（含空格），否则最常见的名字会整条回退
TEST(QmNameplateMsdfAtlas, BasePageCoversAscii)
{
	const std::string Json = ReadTestSourceFile(kBaseManifest);
	ASSERT_FALSE(Json.empty());
	json_value *pRoot = JsonParse(Json.c_str(), Json.size());
	ASSERT_NE(pRoot, nullptr);
	const json_value *pGlyphs = json_object_get(pRoot, "glyphs");
	ASSERT_NE(pGlyphs, nullptr);

	for(uint32_t Codepoint = 0x20; Codepoint <= 0x7E; ++Codepoint)
	{
		char aKey[16];
		str_format(aKey, sizeof(aKey), "%u", Codepoint);
		const json_value *pEntry = json_object_get(pGlyphs, aKey);
		EXPECT_NE(pEntry, nullptr) << "missing U+" << std::hex << Codepoint;
		if(pEntry == nullptr || pEntry->type != json_object)
			continue;
		// 空格没有轮廓，但必须有推进宽度，否则排版会塌缩
		const json_value *pAdvance = json_object_get(pEntry, "adv");
		ASSERT_NE(pAdvance, nullptr) << aKey;
		EXPECT_EQ(pAdvance->type, json_double) << aKey;
		EXPECT_GT(pAdvance->u.dbl, 0.0) << aKey;
		if(Codepoint != 0x20)
		{
			const json_value *pOutline = json_object_get(pEntry, "outline");
			ASSERT_NE(pOutline, nullptr) << aKey;
			EXPECT_EQ(pOutline->u.boolean, 1) << aKey;
		}
	}
	json_value_free(pRoot);
}
