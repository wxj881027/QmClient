#include <engine/client/qm_font_face_registry.h>

#include <gtest/gtest.h>

#include <new>

TEST(QmFontFaceRegistry, PublishesNamesAndPreviewFlagsWithEveryFace)
{
	FT_FaceRec aFaces[2]{};
	CQmFontFaceRegistry Registry;
	Registry.AddCollection({&aFaces[0], &aFaces[1]}, true, [&](FT_Face Face) {
		SQmFontFaceNames Names;
		Names.m_Family = "Preview family";
		Names.m_Style = Face == &aFaces[0] ? "Regular" : "Bold";
		return Names;
	});
	EXPECT_EQ(Registry.Faces(), (std::vector<FT_Face>{&aFaces[0], &aFaces[1]}));
	ASSERT_EQ(Registry.Names().size(), 2u);
	EXPECT_EQ(Registry.Names().at(&aFaces[0]).m_Style, "Regular");
	EXPECT_EQ(Registry.Names().at(&aFaces[1]).m_Style, "Bold");
	EXPECT_TRUE(Registry.IsPreview(&aFaces[0]));
	EXPECT_TRUE(Registry.IsPreview(&aFaces[1]));
}

TEST(QmFontFaceRegistry, NameAllocationFailureRollsBackOnlyTheNewCollection)
{
	FT_FaceRec aFaces[3]{};
	CQmFontFaceRegistry Registry;
	Registry.AddCollection({&aFaces[0]}, true, [](FT_Face) {
		SQmFontFaceNames Names;
		Names.m_Family = "Existing family";
		return Names;
	});
	int NameCalls = 0;
	EXPECT_THROW(Registry.AddCollection({&aFaces[1], &aFaces[2]}, true, [&](FT_Face) {
		if(++NameCalls == 2)
			throw std::bad_alloc();
		return SQmFontFaceNames{};
	}),
		std::bad_alloc);
	EXPECT_EQ(NameCalls, 2);
	EXPECT_EQ(Registry.Faces(), (std::vector<FT_Face>{&aFaces[0]}));
	ASSERT_EQ(Registry.Names().size(), 1u);
	EXPECT_EQ(Registry.Names().at(&aFaces[0]).m_Family, "Existing family");
	EXPECT_TRUE(Registry.IsPreview(&aFaces[0]));
	EXPECT_FALSE(Registry.IsPreview(&aFaces[1]));
	EXPECT_FALSE(Registry.IsPreview(&aFaces[2]));
}

TEST(QmFontFaceRegistry, FailedCollectionCanBeRetriedWithoutMissingNames)
{
	FT_FaceRec aFaces[2]{};
	CQmFontFaceRegistry Registry;
	int NameCalls = 0;
	const auto Names = [&](FT_Face) {
		if(++NameCalls == 2)
			throw std::bad_alloc();
		return SQmFontFaceNames{};
	};
	EXPECT_THROW(Registry.AddCollection({&aFaces[0], &aFaces[1]}, false, Names), std::bad_alloc);
	EXPECT_NO_THROW(Registry.AddCollection({&aFaces[0], &aFaces[1]}, false, Names));
	EXPECT_EQ(NameCalls, 4);
	EXPECT_EQ(Registry.Faces(), (std::vector<FT_Face>{&aFaces[0], &aFaces[1]}));
	EXPECT_EQ(Registry.Names().size(), 2u);
	EXPECT_FALSE(Registry.IsPreview(&aFaces[0]));
}

TEST(QmFontFaceRegistry, RepeatedAndNullFacesDoNotChangeExistingClassification)
{
	FT_FaceRec Face{};
	CQmFontFaceRegistry Registry;
	int NameCalls = 0;
	const auto Names = [&](FT_Face) {
		++NameCalls;
		SQmFontFaceNames Result;
		Result.m_Family = "Installed family";
		return Result;
	};
	Registry.AddCollection({&Face}, false, Names);
	Registry.AddCollection({nullptr, &Face, &Face}, true, Names);
	EXPECT_EQ(NameCalls, 1);
	EXPECT_EQ(Registry.Faces(), (std::vector<FT_Face>{&Face}));
	EXPECT_EQ(Registry.Names().at(&Face).m_Family, "Installed family");
	EXPECT_FALSE(Registry.IsPreview(&Face));
}
