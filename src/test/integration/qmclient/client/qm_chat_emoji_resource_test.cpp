// 随包聊天表情经真实存储及生产 WebP 解码后，必须与迁移前 PNG 的 RGBA 完全一致。
#include <base/hash.h>

#include <engine/gfx/image_loader.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/chat_emoji.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <array>
#include <cstdlib>

namespace
{
	struct SEmojiReference
	{
		EQmChatEmoji m_Emoji;
		const char *m_pRgbaSha256;
	};
	const SEmojiReference REFERENCES[] = {
		{EQmChatEmoji::AGREE, "9a557c4ff078dcfb2507061dd5758991b05134203e34fe646ed5141a3cff6717"},
		{EQmChatEmoji::ANGRY, "adc10bbae57dda3942a2309d6912324027b4c3056ea36d73a06f5d66215f9ce9"},
		{EQmChatEmoji::AWKWARD, "7b8a83204fdcd472785d612a5aa6c8571e9fcc6e21408b8474bda0ab93f6da12"},
		{EQmChatEmoji::CUTE, "c9f4de8304adc1c5d729773282a8535748b7354600496c577f17675b9dc347bc"},
		{EQmChatEmoji::DEAD, "bed259bf3786866ff94cd0cc54729f1f17039ef25474a24d8222522eee6d00d9"},
		{EQmChatEmoji::HEHE, "c32c54b99259d57ea36f6af8eb31c4348adc3173fc2ad206b62064307db4bb20"},
		{EQmChatEmoji::INSULT, "e371360c94a01cb7ccedcfbd3f58d7718119d82c13d123e6b281d54b508b5eac"},
		{EQmChatEmoji::KNEEL, "8463b111289fd6e790ac074319f1f11a6a5f4adf063ddc106aca98b3c158f59f"},
		{EQmChatEmoji::LOVE, "3b3443943b04f7f96456ecfb550b0442215f6f2da7fcf8344a5c78b8029c9898"},
		{EQmChatEmoji::NO, "b0eb185698f3243823acec22e7635e68fd23f8e9c8ef1e6dedceebd7535b4c64"},
		{EQmChatEmoji::OPPOSE, "ae5dcfd91e3f1f6a069beee52fce1d926726e2ff987a352c3fb2881065d36b31"},
		{EQmChatEmoji::QUESTION, "cdd71305299344e5358661453dd21fd439b875b36ffef197601f71bba8e78546"},
		{EQmChatEmoji::SHOCKED, "f874085e3fb6b347f9bceb2c48576117493279842c7119b62c827e34d1a659e6"},
		{EQmChatEmoji::SMELL, "c9ee228e2f985c8d5177cca6567bc4536cb6aeb7e62b8cacae267b271dda1e1c"},
		{EQmChatEmoji::SUPPORT, "64bf59282c8b232ab6199238823eef1960240ba80d1e072f39bb18e50102dd42"},
		{EQmChatEmoji::SURRENDER, "578d6b746069b1d6e784bc658c7faff0410830865c7e915493caac8920a40cec"},
	};
	class CQmChatEmojiResource : public ::testing::TestWithParam<SEmojiReference>
	{
	};
}

TEST_P(CQmChatEmojiResource, LosslessWebPDecodesToOriginalPixelsIncludingTransparency)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	const auto &Reference = GetParam();
	const char *pPath = QmChatEmojiTexturePath(Reference.m_Emoji);
	void *pFileData = nullptr;
	unsigned FileSize = 0;
	ASSERT_TRUE(pStorage->ReadFile(pPath, IStorage::TYPE_ALL, &pFileData, &FileSize));
	CImageInfo Image;
	const bool Loaded = CImageLoader::LoadWebP(pFileData, FileSize, pPath, Image);
	free(pFileData);
	ASSERT_TRUE(Loaded);
	EXPECT_EQ(Image.m_Width, 1260u);
	EXPECT_EQ(Image.m_Height, 1244u);
	EXPECT_EQ(Image.m_Format, CImageInfo::FORMAT_RGBA);
	size_t DataSize = 0;
	if(Image.DataSize(DataSize))
	{
		char aDigest[SHA256_MAXSTRSIZE];
		sha256_str(sha256(Image.m_pData, DataSize), aDigest, sizeof(aDigest));
		EXPECT_STREQ(aDigest, Reference.m_pRgbaSha256);
	}
	else
		ADD_FAILURE() << "decoded emoji has invalid size";
	Image.Free();
}
INSTANTIATE_TEST_SUITE_P(Bundled, CQmChatEmojiResource, ::testing::ValuesIn(REFERENCES));
