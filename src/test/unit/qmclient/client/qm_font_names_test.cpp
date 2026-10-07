// 使用实际 SFNT 记录与 FreeType face 验证名称，不读取生产源码。
#include <engine/client/qm_font_name_match.h>
#include <engine/client/qm_font_names.h>

#include <game/client/QmUi/SettingsFontSelection.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <fstream>
#include <iterator>
#include <set>
#include <vector>

TEST(QmFontNames, DecodesUnicodeChineseAndSupplementaryNames)
{
	FT_Byte aBytes[] = {0x4E, 0x2D, 0x65, 0x87, 0xD8, 0x3D, 0xDE, 0x00};
	FT_SfntName Name{};
	Name.platform_id = 3;
	Name.encoding_id = 1;
	Name.string = aBytes;
	Name.string_len = sizeof(aBytes);
	EXPECT_EQ(QmDecodeSfntName(Name), "中文😀");
	Name.platform_id = 0;
	EXPECT_EQ(QmDecodeSfntName(Name), "中文😀");
}

TEST(QmFontNames, MalformedUtf16AndUnsafeLegacyNamesAreRejected)
{
	FT_Byte aBytes[] = {0xD8, 0x3D, 0x00, 0x41};
	FT_SfntName Name{};
	Name.platform_id = 3;
	Name.encoding_id = 1;
	Name.string = aBytes;
	Name.string_len = sizeof(aBytes);
	EXPECT_TRUE(QmDecodeSfntName(Name).empty());
	Name.string_len = 3;
	EXPECT_TRUE(QmDecodeSfntName(Name).empty());
	Name.platform_id = 1;
	Name.encoding_id = 0;
	EXPECT_TRUE(QmDecodeSfntName(Name).empty());
}

TEST(QmFontNames, MissingUnicodeRecordsUseOnlyValidUtf8Fallback)
{
	FT_Library Library = nullptr;
	ASSERT_EQ(FT_Init_FreeType(&Library), 0);
	std::ifstream File(TestSourcePath("data/fonts/DejaVuSans.ttf"), std::ios::binary);
	ASSERT_TRUE(File.good());
	const std::vector<unsigned char> Bytes{std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()};
	FT_Face Face = nullptr;
	ASSERT_EQ(FT_New_Memory_Face(Library, Bytes.data(), Bytes.size(), 0, &Face), 0);
	EXPECT_EQ(QmFontSfntName(Face, 65000, "中文字体"), "中文字体");
	const char aInvalid[] = {static_cast<char>(0xFF), 0};
	EXPECT_TRUE(QmFontSfntName(Face, 65000, aInvalid).empty());
	const SQmFontFaceNames Names = QmFontFaceNames(Face);
	EXPECT_EQ(Names.m_Family, "DejaVu Sans");
	EXPECT_EQ(Names.m_Style, "Book");
	FT_Done_Face(Face);
	FT_Done_FreeType(Library);
}

TEST(QmFontNames, EveryCollectionFaceHasStableUtf8FamilyWithoutDuplicateFiles)
{
	FT_Library Library = nullptr;
	ASSERT_EQ(FT_Init_FreeType(&Library), 0);
	std::ifstream File(TestSourcePath("data/fonts/SourceHanSans.ttc"), std::ios::binary);
	ASSERT_TRUE(File.good());
	const std::vector<unsigned char> Bytes{std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()};
	FT_Face Collection = nullptr;
	ASSERT_EQ(FT_New_Memory_Face(Library, Bytes.data(), Bytes.size(), -1, &Collection), 0);
	const FT_Long Count = Collection->num_faces;
	FT_Done_Face(Collection);
	ASSERT_GT(Count, 1);
	bool FoundChinese = false;
	std::set<std::string> Styles;
	for(FT_Long Index = 0; Index < Count; ++Index)
	{
		SCOPED_TRACE(Index);
		FT_Face Face = nullptr;
		ASSERT_EQ(FT_New_Memory_Face(Library, Bytes.data(), Bytes.size(), Index, &Face), 0);
		const SQmFontFaceNames Names = QmFontFaceNames(Face);
		EXPECT_FALSE(Names.m_Family.empty());
		EXPECT_TRUE(str_utf8_check(Names.m_Family.c_str()));
		EXPECT_TRUE(str_utf8_check(Names.m_Style.c_str()));
		EXPECT_EQ(Names.m_Family, "Source Han Sans");
		FoundChinese |= Names.m_Style == "SC Regular";
		EXPECT_TRUE(Styles.insert(Names.m_Style).second);
		EXPECT_TRUE(QmFontFaceMatchesLegacyName(Names, (Names.m_LegacyFamily + " " + Names.m_LegacyStyle).c_str()));
		FT_Done_Face(Face);
	}
	EXPECT_TRUE(FoundChinese);
	EXPECT_EQ(Styles, (std::set<std::string>{"Regular", "K Regular", "SC Regular", "TC Regular", "HC Regular", "HW Regular", "HW K Regular", "HW SC Regular", "HW TC Regular", "HW HC Regular"}));
	FT_Done_FreeType(Library);
}

TEST(QmFontNames, QuestionMarkPlaceholdersCannotHideUnicodeNames)
{
	FT_Byte aPlaceholder[] = {0, '?', 0, '?', 0, '?', 0, ' ', 0, '?'};
	FT_SfntName Name{};
	Name.platform_id = 3;
	Name.encoding_id = 1;
	Name.string = aPlaceholder;
	Name.string_len = sizeof(aPlaceholder);
	EXPECT_TRUE(QmDecodeSfntName(Name).empty());
	EXPECT_FALSE(QmFontNameUsable("??? ???"));
	EXPECT_FALSE(QmFontNameUsable("bad�name"));
	EXPECT_TRUE(QmFontNameUsable("Font? Regular"));
	EXPECT_TRUE(QmFontNameUsable("中文字体"));
	EXPECT_TRUE(QmFontSfntName(nullptr, 1, "Fallback").empty());
	EXPECT_TRUE(QmFontFaceNames(nullptr).m_Family.empty());
}

TEST(QmFontNames, LegacyCompactFamilyStyleMatchesOnlyExactNormalizedNames)
{
	EXPECT_TRUE(QmFontNamesEqual("DejaVuSans Book", "DejaVu Sans Book"));
	EXPECT_TRUE(QmFontNamesEqual("dejavu-sans-book", "DejaVu Sans Book"));
	EXPECT_FALSE(QmFontNamesEqual("DejaVuSansExtra Book", "DejaVu Sans Book"));
	EXPECT_FALSE(QmFontNamesEqual("SourceHanSansSC", "Source Han Sans TC"));
}

TEST(QmFontNames, BundledPoppinsUsesTypographicFamilyAndAllRealStyles)
{
	FT_Library Library = nullptr;
	ASSERT_EQ(FT_Init_FreeType(&Library), 0);
	for(const char *pStyle : {"Regular", "Bold", "Light", "Medium"})
	{
		SCOPED_TRACE(pStyle);
		std::ifstream File(TestSourcePath(("data/fonts/Poppins/Poppins-" + std::string(pStyle) + ".ttf").c_str()), std::ios::binary);
		ASSERT_TRUE(File.good());
		const std::vector<unsigned char> Bytes{std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()};
		FT_Face Face = nullptr;
		ASSERT_EQ(FT_New_Memory_Face(Library, Bytes.data(), Bytes.size(), 0, &Face), 0);
		const auto Names = QmFontFaceNames(Face);
		EXPECT_EQ(Names.m_Family, "Poppins");
		EXPECT_EQ(Names.m_Style, pStyle);
		std::string SelectedFamily, SelectedStyle;
		EXPECT_TRUE(QmFontSelectionNames(&Names, SelectedFamily, SelectedStyle));
		EXPECT_EQ(SelectedFamily, "Poppins");
		EXPECT_EQ(SelectedStyle, pStyle);
		EXPECT_TRUE(QmFontFaceMatchesLegacyName(Names, (Names.m_LegacyFamily + " " + Names.m_LegacyStyle).c_str()));
		FT_Done_Face(Face);
	}
	FT_Done_FreeType(Library);
}

TEST(QmFontNames, CollectionVariantsDoNotMergeUnrelatedFamiliesOrStaticFiles)
{
	EXPECT_EQ(QmOrganizeFontFaceNames("User Font SC", "Book", "", "", true).m_Family, "User Font SC");
	EXPECT_EQ(QmOrganizeFontFaceNames("Source Han Sans HW Extra", "Regular", "", "", true).m_Family, "Source Han Sans HW Extra");
	EXPECT_EQ(QmOrganizeFontFaceNames("Source Han Sans SC", "Regular", "", "", false).m_Family, "Source Han Sans SC");
	const auto Names = QmOrganizeFontFaceNames("Source Han Sans HW SC", "Bold", "", "", true);
	EXPECT_EQ(Names.m_Family, "Source Han Sans");
	EXPECT_EQ(Names.m_Style, "HW SC Bold");
	EXPECT_TRUE(QmFontFaceMatchesLegacyName(Names, "SourceHanSansHWSC Bold"));
	EXPECT_TRUE(QmFontFaceMatchesLegacyName(Names, "Source Han Sans HW SC"));
	EXPECT_FALSE(QmFontFaceMatchesLegacyName(Names, "Source Han Sans TC"));
}

TEST(QmFontNames, TypographicNamesDoNotGuessMissingWeightsFromFamilySuffix)
{
	const auto Legacy = QmOrganizeFontFaceNames("Example Light", "Regular", "", "", false);
	EXPECT_EQ(Legacy.m_Family, "Example Light");
	EXPECT_EQ(Legacy.m_Style, "Regular");
	const auto Typographic = QmOrganizeFontFaceNames("Example Light", "Regular", "Example", "Light", false);
	EXPECT_EQ(Typographic.m_Family, "Example");
	EXPECT_EQ(Typographic.m_Style, "Light");
	EXPECT_TRUE(QmFontFaceMatchesLegacyName(Typographic, "Example Light Regular"));
}

TEST(QmFontNames, CachedFaceAvoidsRepeatedResolutionAndResetPicksUpNewFace)
{
	FT_FaceRec First{}, Second{};
	CQmFontFaceLookupCache Cache;
	int Calls = 0;
	FT_Face Current = &First;
	const auto Resolver = [&](const char *) { ++Calls; return Current; };
	EXPECT_EQ(Cache.Resolve("Poppins Light Regular", Resolver), &First);
	Current = &Second;
	EXPECT_EQ(Cache.Resolve("Poppins Light Regular", Resolver), &First);
	EXPECT_EQ(Calls, 1);
	Cache.Reset();
	EXPECT_EQ(Cache.Resolve("Poppins Light Regular", Resolver), &Second);
	EXPECT_EQ(Calls, 2);
}

TEST(QmFontNames, CachedMissingFaceRestoresAfterFontRegistration)
{
	FT_FaceRec Installed{};
	CQmFontFaceLookupCache Cache;
	int Calls = 0;
	FT_Face Current = nullptr;
	const auto Resolver = [&](const char *) { ++Calls; return Current; };
	EXPECT_EQ(Cache.Resolve("Downloaded Font", Resolver), nullptr);
	EXPECT_EQ(Cache.Resolve("Downloaded Font", Resolver), nullptr);
	EXPECT_EQ(Calls, 1);
	Current = &Installed;
	Cache.Reset();
	EXPECT_EQ(Cache.Resolve("Downloaded Font", Resolver), &Installed);
	EXPECT_EQ(Calls, 2);
}

TEST(QmFontNames, EmptyOrMissingSelectionClearsPreviouslyResolvedNames)
{
	std::string Family = "Source Han Sans", Style = "SC Regular";
	EXPECT_FALSE(QmFontSelectionNames(nullptr, Family, Style));
	EXPECT_TRUE(Family.empty());
	EXPECT_TRUE(Style.empty());
	CQmFontFaceLookupCache Cache;
	int Calls = 0;
	const auto Resolver = [&](const char *) -> FT_Face { ++Calls; return nullptr; };
	EXPECT_EQ(Cache.Resolve("", Resolver), nullptr);
	EXPECT_EQ(Cache.Resolve(nullptr, Resolver), nullptr);
	EXPECT_EQ(Calls, 0);
}

TEST(QmFontNames, CollectionPreservesRegionWhenTypographicFamilyIsAlreadyUnified)
{
	const auto Names = QmOrganizeFontFaceNames("Source Han Sans HW SC", "Regular", "Source Han Sans", "Regular", true);
	EXPECT_EQ(Names.m_Family, "Source Han Sans");
	EXPECT_EQ(Names.m_Style, "HW SC Regular");
	const auto Tagged = QmOrganizeFontFaceNames("Source Han Sans HW SC", "Regular", "Source Han Sans", "HW SC Regular", true);
	EXPECT_EQ(Tagged.m_Style, "HW SC Regular");
	std::string Family, Style;
	EXPECT_TRUE(QmFontSelectionNames(&Names, Family, Style));
	EXPECT_EQ(Family, "Source Han Sans");
	EXPECT_EQ(Style, "HW SC Regular");
}

namespace
{
	class CQmFontNameResolution : public testing::Test
	{
	protected:
		FT_Library m_Library = nullptr;
		FT_Face m_Light = nullptr, m_Regular = nullptr;
		std::vector<std::vector<unsigned char>> m_Bytes;
		std::unordered_map<FT_Face, SQmFontFaceNames> m_Names;

		void SetUp() override
		{
			ASSERT_EQ(FT_Init_FreeType(&m_Library), 0);
			m_Bytes.resize(2);
			FT_Face *apFaces[] = {&m_Light, &m_Regular};
			const char *apStyles[] = {"Light", "Regular"};
			for(int Index = 0; Index < 2; ++Index)
			{
				std::ifstream File(TestSourcePath((std::string("data/fonts/Poppins/Poppins-") + apStyles[Index] + ".ttf").c_str()), std::ios::binary);
				ASSERT_TRUE(File.good());
				m_Bytes[Index] = {std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>()};
				ASSERT_EQ(FT_New_Memory_Face(m_Library, m_Bytes[Index].data(), m_Bytes[Index].size(), 0, apFaces[Index]), 0);
				m_Names.emplace(*apFaces[Index], QmFontFaceNames(*apFaces[Index]));
			}
		}
		void TearDown() override
		{
			if(m_Light != nullptr)
				FT_Done_Face(m_Light);
			if(m_Regular != nullptr)
				FT_Done_Face(m_Regular);
			if(m_Library != nullptr)
				FT_Done_FreeType(m_Library);
		}
	};
}

TEST_F(CQmFontNameResolution, CanonicalFullNameBeatsEarlierLegacyAliasRegardlessOfLoadOrder)
{
	// 第二个真实 face 的名称输入模拟用户安装的独立旧四样式族；解析算法来自生产入口。
	m_Names[m_Regular] = QmOrganizeFontFaceNames("Poppins Light", "Regular", "", "", false);
	for(const auto &Faces : {std::vector<FT_Face>{m_Light, m_Regular}, std::vector<FT_Face>{m_Regular, m_Light}})
		EXPECT_EQ(QmResolveFontFaceName("Poppins Light Regular", Faces, m_Names), m_Regular);
}

TEST_F(CQmFontNameResolution, CanonicalFamilyBeatsEarlierLegacyFullName)
{
	m_Names[m_Regular] = QmOrganizeFontFaceNames("Poppins Light Regular", "Bold", "", "", false);
	EXPECT_EQ(QmResolveFontFaceName("Poppins Light Regular", {m_Light, m_Regular}, m_Names), m_Regular);
}

TEST_F(CQmFontNameResolution, StaticFamilyDefaultsToRealRegularRegardlessOfLoadOrder)
{
	for(const auto &Faces : {std::vector<FT_Face>{m_Light, m_Regular}, std::vector<FT_Face>{m_Regular, m_Light}})
		EXPECT_EQ(QmResolveFontFaceName("Poppins", Faces, m_Names), m_Regular);
}

TEST_F(CQmFontNameResolution, LegacyAliasRemainsUsableWhenNoCanonicalNameConflicts)
{
	const std::vector<FT_Face> Faces{m_Light, m_Regular};
	EXPECT_EQ(QmResolveFontFaceName("PoppinsLight Regular", Faces, m_Names), m_Light);
	EXPECT_EQ(QmResolveFontFaceName("Poppins Unknown", Faces, m_Names), nullptr);
	EXPECT_EQ(QmResolveFontFaceName("", Faces, m_Names), nullptr);
	EXPECT_EQ(QmResolveFontFaceName(nullptr, Faces, m_Names), nullptr);
}

TEST_F(CQmFontNameResolution, FamilySelectionDisambiguatesARealFamilyFromAnotherFacesFullName)
{
	m_Names[m_Regular] = QmOrganizeFontFaceNames("Poppins Light", "Regular", "", "", false);
	const std::vector<FT_Face> Faces{m_Light, m_Regular};
	EXPECT_EQ(QmResolveFontFaceName("Poppins Light", Faces, m_Names), m_Light);
	FT_Face FamilyFace = QmResolveFontFamilyName("Poppins Light", Faces, m_Names);
	ASSERT_EQ(FamilyFace, m_Regular);
	std::string Config;
	ASSERT_TRUE(QmFontFamilySelectionConfig(FamilyFace, m_Names, [&](const char *pName) { return QmResolveFontFaceName(pName, Faces, m_Names); }, Config));
	EXPECT_EQ(Config, "Poppins Light Regular");
	FT_Face Restored = QmResolveFontFaceName(Config.c_str(), Faces, m_Names);
	ASSERT_EQ(Restored, m_Regular);
	std::string SelectedFamily, SelectedStyle;
	ASSERT_TRUE(QmFontSelectionNames(&m_Names.at(Restored), SelectedFamily, SelectedStyle));
	EXPECT_EQ(SelectedFamily, "Poppins Light");
	EXPECT_EQ(SelectedStyle, "Regular");
	// 新的族选择不会改掉旧版完整 face 配置的含义。
	EXPECT_EQ(QmResolveFontFaceName("Poppins Light", Faces, m_Names), m_Light);
}

TEST_F(CQmFontNameResolution, UnambiguousFamilyDefaultConfigurationPreservesFamilyOnlyWeightSemantics)
{
	const std::vector<FT_Face> Faces{m_Light, m_Regular};
	FT_Face FamilyFace = QmResolveFontFamilyName("Poppins", Faces, m_Names);
	ASSERT_EQ(FamilyFace, m_Regular);
	std::string Config;
	ASSERT_TRUE(QmFontFamilySelectionConfig(FamilyFace, m_Names, [&](const char *pName) { return QmResolveFontFaceName(pName, Faces, m_Names); }, Config));
	EXPECT_EQ(Config, "Poppins");
}

TEST_F(CQmFontNameResolution, ExplicitFamilyQueryDoesNotAcceptFullStyleOrLegacyAliases)
{
	const std::vector<FT_Face> Faces{m_Light, m_Regular};
	EXPECT_EQ(QmResolveFontFamilyName("Poppins Light", Faces, m_Names), nullptr);
	EXPECT_EQ(QmResolveFontFamilyName("Poppins Light Regular", Faces, m_Names), nullptr);
	EXPECT_EQ(QmResolveFontFaceName("Poppins Light Regular", Faces, m_Names), m_Light);
	std::string Config = "stale";
	EXPECT_FALSE(QmFontFamilySelectionConfig(nullptr, m_Names, [&](const char *pName) { return QmResolveFontFaceName(pName, Faces, m_Names); }, Config));
	EXPECT_TRUE(Config.empty());
}

TEST_F(CQmFontNameResolution, VariableWeightFinalizationKeepsDisambiguatedFamilyAndRepeatedFinalizationIsStable)
{
	m_Names[m_Regular] = QmOrganizeFontFaceNames("Poppins Light", "Regular", "", "", false);
	const std::vector<FT_Face> Faces{m_Light, m_Regular};
	const auto FamilyConfig = [&](const char *pFamily, std::string &Config) {
		return QmFontFamilySelectionConfig(QmResolveFontFamilyName(pFamily, Faces, m_Names), m_Names, [&](const char *pName) { return QmResolveFontFaceName(pName, Faces, m_Names); }, Config);
	};
	std::string Config;
	ASSERT_TRUE(QmResolveVariableFontSelection("Poppins Light", "Poppins", 256, FamilyConfig, Config));
	EXPECT_EQ(QmResolveFontFaceName(Config.c_str(), Faces, m_Names), m_Regular);
	EXPECT_EQ(Config, "Poppins Light Regular");
	EXPECT_FALSE(QmResolveVariableFontSelection("Poppins Light", Config.c_str(), 256, FamilyConfig, Config));
	EXPECT_EQ(QmResolveFontFaceName(Config.c_str(), Faces, m_Names), m_Regular);
}

TEST_F(CQmFontNameResolution, VariableWeightFinalizationPreservesConfigurationWhenFamilyMissingOrCapacityInsufficient)
{
	const std::vector<FT_Face> Faces{m_Light, m_Regular};
	const auto FamilyConfig = [&](const char *pFamily, std::string &Config) {
		return QmFontFamilySelectionConfig(QmResolveFontFamilyName(pFamily, Faces, m_Names), m_Names, [&](const char *pName) { return QmResolveFontFaceName(pName, Faces, m_Names); }, Config);
	};
	std::string Config;
	EXPECT_FALSE(QmResolveVariableFontSelection("Missing", "Poppins Light", 256, FamilyConfig, Config));
	EXPECT_EQ(Config, "Poppins Light");
	EXPECT_FALSE(QmResolveVariableFontSelection("Poppins", Config.c_str(), 7, FamilyConfig, Config));
	EXPECT_EQ(Config, "Poppins Light");
	ASSERT_TRUE(QmResolveVariableFontSelection("Poppins", Config.c_str(), 256, FamilyConfig, Config));
	EXPECT_EQ(Config, "Poppins");
	EXPECT_EQ(QmResolveFontFaceName(Config.c_str(), Faces, m_Names), m_Regular);
}
