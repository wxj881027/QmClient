#include <gtest/gtest.h>
#include <test/support/qmclient_source_contract_test.h>

#include <array>
#include <fstream>
#include <sstream>
#include <string>

TEST(QmMediaIslandGpuSdfContract, ShapePassAvoidsPerFragmentDistanceArrayAndInactiveItemIterations)
{
	const std::array<const char *, 2> apShaderPaths = {
		"data/shader/media_island_sdf.frag",
		"data/shader/vulkan/media_island_sdf.frag",
	};
	for(const char *pShaderPath : apShaderPaths)
	{
		const std::string ShaderSource = ReadTestSourceFile(pShaderPath);
		EXPECT_NE(ShaderSource.find("gMediaIslandSdfData[45]"), std::string::npos) << pShaderPath;
		EXPECT_NE(ShaderSource.find("ITEM_STRIDE = 3"), std::string::npos) << pShaderPath;
		EXPECT_NE(ShaderSource.find("BlobSdf"), std::string::npos) << pShaderPath;
		EXPECT_NE(ShaderSource.find("BlobExponent"), std::string::npos) << pShaderPath;
		EXPECT_EQ(ShaderSource.find("RadialRipple"), std::string::npos) << pShaderPath;
		EXPECT_EQ(ShaderSource.find("StrongestRipple"), std::string::npos) << pShaderPath;
		EXPECT_EQ(ShaderSource.find("EllipseSdf"), std::string::npos) << pShaderPath;
		EXPECT_NE(ShaderSource.find("float ItemDistance = BlobSdf(Point, ItemShape.xy, ItemShape.zw);"), std::string::npos) << pShaderPath;
		EXPECT_EQ(ShaderSource.find("float ItemDistances[MAX_ITEMS]"), std::string::npos) << pShaderPath;
		EXPECT_EQ(ShaderSource.find("i < MAX_ITEMS"), std::string::npos) << pShaderPath;
		EXPECT_NE(ShaderSource.find("float ShapeDistance = MainDistance;"), std::string::npos) << pShaderPath;
		EXPECT_NE(ShaderSource.find("vec4 ItemShape = Data(ITEM_BASE + i * ITEM_STRIDE);"), std::string::npos) << pShaderPath;

		const std::string ItemLoop = "for(int i = 0; i < ItemCount; ++i)";
		const size_t ShapeLoop = ShaderSource.find(ItemLoop);
		ASSERT_NE(ShapeLoop, std::string::npos) << pShaderPath;
		const size_t RingLoop = ShaderSource.find(ItemLoop, ShapeLoop + ItemLoop.size());
		ASSERT_NE(RingLoop, std::string::npos) << pShaderPath;
		EXPECT_EQ(ShaderSource.find(ItemLoop, RingLoop + ItemLoop.size()), std::string::npos) << pShaderPath;
	}
}

TEST(QmMediaIslandGpuSdfContract, OpenGlAndVulkanUseTheSameFragmentMainPath)
{
	const std::string OpenGlSource = ReadTestSourceFile("data/shader/media_island_sdf.frag");
	const std::string VulkanSource = ReadTestSourceFile("data/shader/vulkan/media_island_sdf.frag");
	const size_t OpenGlMain = OpenGlSource.find("void main()");
	const size_t VulkanMain = VulkanSource.find("void main()");
	ASSERT_NE(OpenGlMain, std::string::npos);
	ASSERT_NE(VulkanMain, std::string::npos);

	const auto CompactWhitespace = [](std::string Source) {
		Source.erase(std::remove_if(Source.begin(), Source.end(), [](char Character) {
			return Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n';
		}),
			Source.end());
		return Source;
	};
	EXPECT_EQ(CompactWhitespace(OpenGlSource.substr(OpenGlMain)), CompactWhitespace(VulkanSource.substr(VulkanMain)));
}
