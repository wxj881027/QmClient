#include <base/str.h>

#include <game/client/QmUi/QmLocalizedLabels.h>
#include <game/localization.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

// 语言数据库是进程级共享状态；测试前后都清空，避免影响其他 suite。
class CLocalizationScope
{
public:
	CLocalizationScope() { g_Localization.Load("", nullptr, nullptr); }
	~CLocalizationScope() { g_Localization.Load("", nullptr, nullptr); }
};

TEST(QmLocalizedLabelCache, SameLanguageFileKeepsOwnedCopyAfterSourceOverwrite)
{
	// Localize 的返回值位于语言文件堆；同一文件再次加载后旧堆被释放或复用。
	// 这里直接覆写取词函数返回的缓冲区，若缓存保存的是指针而非副本，读到的内容会随之改变。
	char aTranslation[32];
	str_copy(aTranslation, "旧译文");
	const auto Translate = [&](const char *) { return static_cast<const char *>(aTranslation); };

	CQmLocalizedLabelCache Cache;
	ASSERT_TRUE(Cache.RefreshWith(Translate, "simplified_chinese.txt", {"Settings"}));
	ASSERT_STREQ(Cache.Get(0), "旧译文");

	str_copy(aTranslation, "已释放");
	EXPECT_FALSE(Cache.RefreshWith(Translate, "simplified_chinese.txt", {"Settings"}));
	EXPECT_STREQ(Cache.Get(0), "旧译文");
	EXPECT_EQ(Cache.Size(), 1U);
}

TEST(QmLocalizedLabelCache, LanguageChangeRefreshesEveryLabel)
{
	const auto TranslateJapanese = [](const char *pKey) {
		return std::string(pKey) == "Settings" ? "設定" : "バインドホイール";
	};
	const auto TranslateChinese = [](const char *pKey) {
		return std::string(pKey) == "Settings" ? "设置" : "绑定轮盘";
	};

	CQmLocalizedLabelCache Cache;
	ASSERT_TRUE(Cache.RefreshWith(TranslateJapanese, "japanese.txt", {"Settings", "Bind Wheel"}));
	ASSERT_STREQ(Cache.Get(0), "設定");
	ASSERT_STREQ(Cache.Get(1), "バインドホイール");

	ASSERT_TRUE(Cache.RefreshWith(TranslateChinese, "simplified_chinese.txt", {"Settings", "Bind Wheel"}));
	EXPECT_STREQ(Cache.Get(0), "设置");
	EXPECT_STREQ(Cache.Get(1), "绑定轮盘");
	EXPECT_EQ(Cache.Size(), 2U);
}

TEST(QmLocalizedLabelCache, ForcedRefreshRekeysWithSameLanguageFile)
{
	const auto Translate = [](const char *pKey) {
		return std::string(pKey) == "Tee 0.7" ? "Tee 0.7" : "Tee";
	};

	CQmLocalizedLabelCache Cache;
	ASSERT_TRUE(Cache.RefreshWith(Translate, "english.txt", {"Tee"}));
	ASSERT_STREQ(Cache.Get(0), "Tee");

	// Sixup（0.7 协议）切换会改变取词 key，但语言文件不变，调用方需要强制刷新。
	ASSERT_TRUE(Cache.RefreshWith(Translate, "english.txt", {"Tee 0.7"}, true));
	EXPECT_STREQ(Cache.Get(0), "Tee 0.7");
}

TEST(QmLocalizedLabelCache, RefreshLocalizesKeysAndFallsBackToSourceKey)
{
	CLocalizationScope LocalizationScope;
	// AddString 只追加不排序，而查找使用二分；只登记一条译文，避免依赖 key 哈希的插入顺序。
	g_Localization.AddString("Settings", "设置", "");

	CQmLocalizedLabelCache Cache;
	ASSERT_TRUE(Cache.Refresh("simplified_chinese.txt", {"Settings", "Bind Wheel"}));
	EXPECT_STREQ(Cache.Get(0), "设置");
	EXPECT_STREQ(Cache.Get(1), "Bind Wheel");
}
