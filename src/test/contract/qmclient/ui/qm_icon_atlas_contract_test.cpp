// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include <engine/shared/json.h>

#include <game/client/qm_icon_manager.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <string>

namespace
{
	std::string ReadTextFile(const char *pPath)
	{
		return ReadTestSourceFile(pPath);
	}

	int JsonInt(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		EXPECT_EQ(pValue->type, json_integer);
		return pValue->type == json_integer ? static_cast<int>(pValue->u.integer) : 0;
	}

	const char *JsonString(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		EXPECT_EQ(pValue->type, json_string);
		return pValue->type == json_string ? pValue->u.string.ptr : "";
	}

	bool JsonBool(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		EXPECT_EQ(pValue->type, json_boolean);
		return pValue->type == json_boolean && pValue->u.boolean;
	}

	const json_value *JsonObject(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		EXPECT_EQ(pValue->type, json_object);
		return pValue;
	}

	const json_value *JsonArray(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		EXPECT_EQ(pValue->type, json_array);
		return pValue;
	}

	double JsonDouble(const json_value *pObject, const char *pName)
	{
		const json_value *pValue = json_object_get(pObject, pName);
		EXPECT_NE(pValue, &json_value_none);
		if(pValue->type == json_double)
			return pValue->u.dbl;
		if(pValue->type == json_integer)
			return static_cast<double>(pValue->u.integer);
		return 0.0;
	}
}

// 资源清单是离线生成产物，须验证其与运行时图标标识及图集边界一致。
TEST(QmIconAtlas, GeneratedMsdfManifestsContainEveryRuntimeIcon)
{
	// Thin 未随包字体，不再烘焙（weight 2 复用 light 图集）。
	constexpr const char *apWeights[] = {"regular", "bold", "fill", "light"};
	for(const char *pWeight : apWeights)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "data/qmclient/icons/qm_icons_%s_msdf.json", pWeight);
		const std::string Json = ReadTextFile(aPath);
		ASSERT_FALSE(Json.empty()) << aPath;

		const std::unique_ptr<json_value, decltype(&json_value_free)> Root(JsonParse(Json.c_str(), Json.size()), json_value_free);
		json_value *pRoot = Root.get();
		ASSERT_NE(pRoot, nullptr) << aPath;

		const json_value *pAtlas = JsonObject(pRoot, "atlas");
		const json_value *pIcons = JsonObject(pRoot, "icons");
		ASSERT_EQ(pIcons->type, json_object);
		const int AtlasWidth = JsonInt(pAtlas, "width");
		const int AtlasHeight = JsonInt(pAtlas, "height");
		constexpr int ToolPadding = 8; // 官方工具的字形 pxrange 出血
		constexpr int CellSize = 72; // 48 + 2×12 网格间距（字形外轮廓可略超 em 框）
		const int IconCount = static_cast<int>(pIcons->u.object.length);
		EXPECT_GE(IconCount, static_cast<int>(EQmIcon::COUNT));
		// 新旧官方名共用码点 → manifest 条目数可大于唯一格数；网格按唯一格数布局。
		EXPECT_EQ(AtlasWidth, AtlasHeight);
		EXPECT_EQ(AtlasWidth % CellSize, 0) << AtlasWidth;
		// manifest 条目可多于唯一格数（新旧官方名共享码点格子），网格以唯一格数布局。
		EXPECT_EQ(JsonInt(pRoot, "version"), 2);
		EXPECT_STREQ(JsonString(pRoot, "kind"), "mtsdf");
		EXPECT_STREQ(JsonString(pRoot, "distance_field"), "mtsdf");
		EXPECT_TRUE(JsonBool(pRoot, "alpha_sdf"));
		EXPECT_EQ(JsonInt(pRoot, "px_range"), 6);
		EXPECT_EQ(JsonInt(pAtlas, "padding"), ToolPadding);

		for(int IconIndex = 0; IconIndex < static_cast<int>(EQmIcon::COUNT); ++IconIndex)
		{
			const EQmIcon Icon = static_cast<EQmIcon>(IconIndex);
			const char *pIconName = CQmIconManager::IconName(Icon);
			ASSERT_NE(pIconName[0], '\0');

			const json_value *pEntry = JsonObject(pIcons, pIconName);
			const int X = JsonInt(pEntry, "x");
			const int Y = JsonInt(pEntry, "y");
			const int W = JsonInt(pEntry, "w");
			const int H = JsonInt(pEntry, "h");

			// 字体烘焙的字形包围盒随图标外轮廓变化（不再是固定 48×48），
			// 但每个格子必须完整落在网格内，且不超过格子尺寸。
			EXPECT_GT(W, 1) << pIconName;
			EXPECT_GT(H, 1) << pIconName;
			EXPECT_LE(W, CellSize) << pIconName;
			EXPECT_LE(H, CellSize) << pIconName;
			EXPECT_GE(X, 0) << pIconName;
			EXPECT_GE(Y, 0) << pIconName;
			EXPECT_LE(X + W, AtlasWidth) << pIconName;
			EXPECT_LE(Y + H, AtlasHeight) << pIconName;
		}
	}
}

TEST(QmIconAtlasContract, BoldAtlasCarriesMorphKeyFrames)
{
	// 眼睛 morph 的 MSDF 关键帧只随 Bold 图集烘焙；显示框必须在图集内，
	// 且首末帧的框与两个眼睛图标一致（端点与静态图标同尺寸基准）。
	const std::string Json = ReadTextFile("data/qmclient/icons/qm_icons_bold_msdf.json");
	ASSERT_FALSE(Json.empty());
	const std::unique_ptr<json_value, decltype(&json_value_free)> Root(JsonParse(Json.c_str(), Json.size()), json_value_free);
	json_value *pRoot = Root.get();
	ASSERT_NE(pRoot, nullptr);

	const json_value *pAtlas = JsonObject(pRoot, "atlas");
	const json_value *pIcons = JsonObject(pRoot, "icons");
	const json_value *pFrames = JsonArray(pRoot, "morph_frames");
	ASSERT_EQ(pFrames->type, json_array);
	const int AtlasWidth = JsonInt(pAtlas, "width");
	const int AtlasHeight = JsonInt(pAtlas, "height");
	const unsigned int FrameCount = pFrames->u.array.length;
	ASSERT_GE(FrameCount, 2u);
	EXPECT_LE(FrameCount, static_cast<unsigned int>(CQmIconAtlas::MORPH_FRAME_CAPACITY));

	const json_value *pEye = JsonObject(pIcons, "eye");
	const json_value *pEyeSlash = JsonObject(pIcons, "eye-slash");

	double PreviousProgress = -1.0;
	for(unsigned int Index = 0; Index < FrameCount; ++Index)
	{
		const json_value *pFrame = pFrames->u.array.values[Index];
		ASSERT_NE(pFrame, nullptr);
		ASSERT_EQ(pFrame->type, json_object);
		const int X = JsonInt(pFrame, "x");
		const int Y = JsonInt(pFrame, "y");
		const int W = JsonInt(pFrame, "w");
		const int H = JsonInt(pFrame, "h");
		const double Progress = JsonDouble(pFrame, "progress");
		EXPECT_GE(X, 0);
		EXPECT_GE(Y, 0);
		EXPECT_GT(W, 0);
		EXPECT_GT(H, 0);
		EXPECT_LE(X + W, AtlasWidth);
		EXPECT_LE(Y + H, AtlasHeight);
		EXPECT_GT(Progress, PreviousProgress) << Index;
		PreviousProgress = Progress;
	}
	// 进度必须覆盖 [0,1] 两端。
	EXPECT_NEAR(JsonDouble(pFrames->u.array.values[0], "progress"), 0.0, 1e-6);
	EXPECT_NEAR(JsonDouble(pFrames->u.array.values[FrameCount - 1], "progress"), 1.0, 1e-6);

	const json_value *pFirst = pFrames->u.array.values[0];
	const json_value *pLast = pFrames->u.array.values[FrameCount - 1];
	EXPECT_NEAR(JsonInt(pFirst, "w"), JsonInt(pEye, "w"), 2);
	EXPECT_NEAR(JsonInt(pFirst, "h"), JsonInt(pEye, "h"), 2);
	EXPECT_NEAR(JsonInt(pLast, "w"), JsonInt(pEyeSlash, "w"), 4);
	EXPECT_NEAR(JsonInt(pLast, "h"), JsonInt(pEyeSlash, "h"), 4);
}
