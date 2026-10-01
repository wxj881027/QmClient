#include <generated/client_data.h>

#include <game/client/components/qmclient/qm_chat_avatar.h>
#include <game/client/components/qmclient/qm_chat_export_metadata.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace
{

	// 合成一份可控几何的精灵表：整张图就是一个 1x1 格子的精灵。
	struct SSyntheticImage
	{
		CImageInfo m_Info{};
		CDataSpriteset m_Set{};
		CDataSprite m_Sprite{};
		std::vector<uint8_t> m_vPixels;

		SSyntheticImage(int Width, int Height, int GridX = 1, int GridY = 1) :
			m_vPixels((size_t)Width * Height * 4, 0)
		{
			m_Info.m_Width = Width;
			m_Info.m_Height = Height;
			m_Info.m_Format = CImageInfo::FORMAT_RGBA;
			m_Info.m_pData = m_vPixels.data();

			m_Set.m_pImage = nullptr;
			m_Set.m_Gridx = GridX;
			m_Set.m_Gridy = GridY;

			m_Sprite.m_pName = "synthetic";
			m_Sprite.m_pSet = &m_Set;
			m_Sprite.m_X = 0;
			m_Sprite.m_Y = 0;
			m_Sprite.m_W = GridX;
			m_Sprite.m_H = GridY;
		}

		void Fill(int X, int Y, uint8_t R, uint8_t G, uint8_t B, uint8_t A)
		{
			uint8_t *pPixel = m_vPixels.data() + ((size_t)Y * m_Info.m_Width + X) * 4;
			pPixel[0] = R;
			pPixel[1] = G;
			pPixel[2] = B;
			pPixel[3] = A;
		}

		void FillAll(uint8_t R, uint8_t G, uint8_t B, uint8_t A)
		{
			for(int y = 0; y < (int)m_Info.m_Height; ++y)
				for(int x = 0; x < (int)m_Info.m_Width; ++x)
					Fill(x, y, R, G, B, A);
		}

		const QmChatAvatar::SSprite &Pixel(const QmChatAvatar::SSprite &Sprite, int X, int Y) const { return Sprite; }
	};

	uint8_t AlphaAt(const QmChatAvatar::TImage &Image, int X, int Y)
	{
		return Image[((size_t)Y * QmChatAvatar::SIZE + X) * 4 + 3];
	}

	std::array<uint8_t, 4> PixelAt(const QmChatAvatar::TImage &Image, int X, int Y)
	{
		const size_t Offset = ((size_t)Y * QmChatAvatar::SIZE + X) * 4;
		return {Image[Offset], Image[Offset + 1], Image[Offset + 2], Image[Offset + 3]};
	}

} // namespace

TEST(QmChatAvatar, CopySpriteDownscalesSpritesLargerThanTheAvatarBudget)
{
	// 128x64 的整图精灵会按 64 的最大边缩到 64x32。
	SSyntheticImage Image(128, 64);
	Image.FillAll(200, 100, 50, 255);

	const QmChatAvatar::SSprite Sprite = QmChatAvatar::CopySprite(Image.m_Info, Image.m_Sprite);
	ASSERT_FALSE(Sprite.Empty());
	EXPECT_EQ(Sprite.m_Width, 64);
	EXPECT_EQ(Sprite.m_Height, 32);

	// 纯色图缩放后每个像素仍是同一颜色，alpha 为采样平均值。
	for(size_t Offset = 0; Offset < Sprite.m_vRgba.size(); Offset += 4)
	{
		EXPECT_EQ(Sprite.m_vRgba[Offset], 200);
		EXPECT_EQ(Sprite.m_vRgba[Offset + 1], 100);
		EXPECT_EQ(Sprite.m_vRgba[Offset + 2], 50);
		EXPECT_EQ(Sprite.m_vRgba[Offset + 3], 255);
	}
}

TEST(QmChatAvatar, CopySpriteKeepsTransparentPixelsColorless)
{
	// 左半透明、右半不透明：透明采样的颜色不能泄漏到结果里。
	SSyntheticImage Image(4, 4);
	for(int y = 0; y < 4; ++y)
	{
		Image.Fill(0, y, 255, 0, 0, 0);
		Image.Fill(1, y, 255, 0, 0, 0);
		Image.Fill(2, y, 10, 20, 30, 255);
		Image.Fill(3, y, 10, 20, 30, 255);
	}

	const QmChatAvatar::SSprite Sprite = QmChatAvatar::CopySprite(Image.m_Info, Image.m_Sprite);
	ASSERT_FALSE(Sprite.Empty());
	ASSERT_EQ(Sprite.m_Width, 4);
	ASSERT_EQ(Sprite.m_Height, 4);

	// x=0 的四个采样点全部来自透明区：alpha 归零，颜色也必须是 0（预乘后无颜色）。
	const size_t TransparentOffset = 0;
	EXPECT_EQ(Sprite.m_vRgba[TransparentOffset + 3], 0);
	EXPECT_EQ(Sprite.m_vRgba[TransparentOffset + 0], 0);
	EXPECT_EQ(Sprite.m_vRgba[TransparentOffset + 1], 0);
	EXPECT_EQ(Sprite.m_vRgba[TransparentOffset + 2], 0);

	// x=3 完全落在不透明区：颜色与 alpha 都是原值。
	const size_t OpaqueOffset = (0 * 4 + 3) * 4;
	EXPECT_EQ(Sprite.m_vRgba[OpaqueOffset + 0], 10);
	EXPECT_EQ(Sprite.m_vRgba[OpaqueOffset + 1], 20);
	EXPECT_EQ(Sprite.m_vRgba[OpaqueOffset + 2], 30);
	EXPECT_EQ(Sprite.m_vRgba[OpaqueOffset + 3], 255);
}

TEST(QmChatAvatar, CopySpriteRejectsInputsItCannotCrop)
{
	SSyntheticImage Image(4, 4);
	Image.FillAll(1, 2, 3, 255);

	// 没有像素数据。
	CImageInfo NoData{};
	NoData.m_Width = Image.m_Info.m_Width;
	NoData.m_Height = Image.m_Info.m_Height;
	NoData.m_Format = CImageInfo::FORMAT_RGBA;
	NoData.m_pData = nullptr;
	EXPECT_TRUE(QmChatAvatar::CopySprite(NoData, Image.m_Sprite).Empty());

	// 不是 RGBA。
	CImageInfo Rgb{};
	Rgb.m_Width = Image.m_Info.m_Width;
	Rgb.m_Height = Image.m_Info.m_Height;
	Rgb.m_Format = CImageInfo::FORMAT_RGB;
	Rgb.m_pData = Image.m_vPixels.data();
	EXPECT_TRUE(QmChatAvatar::CopySprite(Rgb, Image.m_Sprite).Empty());

	// 精灵矩形超出图片范围。
	CDataSprite Outside = Image.m_Sprite;
	Outside.m_X = 8;
	EXPECT_TRUE(QmChatAvatar::CopySprite(Image.m_Info, Outside).Empty());

	// 精灵表没有网格信息。
	CDataSpriteset BadSet = Image.m_Set;
	BadSet.m_Gridx = 0;
	CDataSprite ZeroGrid = Image.m_Sprite;
	ZeroGrid.m_pSet = &BadSet;
	EXPECT_TRUE(QmChatAvatar::CopySprite(Image.m_Info, ZeroGrid).Empty());
}

TEST(QmChatAvatar, CaptureReturnsNullWhenTheRenderInfoCarriesNoSkinSource)
{
	const CTeeRenderInfo Info{};
	EXPECT_EQ(QmChatAvatar::Capture(Info, 0), nullptr);
}

TEST(QmChatAvatar, CapturePrefersSixupPartsAndTakesItsColors)
{
	auto pOriginal = std::make_shared<QmChatAvatar::SSource>();
	auto pColorable = std::make_shared<QmChatAvatar::SSource>();
	CTeeRenderInfo Info{};
	Info.m_aSixup[0].m_apChatAvatarOriginal[protocol7::SKINPART_BODY] = pOriginal;
	Info.m_aSixup[0].m_apChatAvatarColorable[protocol7::SKINPART_BODY] = pColorable;
	Info.m_aSixup[0].m_aColors[protocol7::SKINPART_BODY] = ColorRGBA(0.1f, 0.2f, 0.3f, 1.0f);
	Info.m_aSixup[0].m_aColors[protocol7::SKINPART_FEET] = ColorRGBA(0.4f, 0.5f, 0.6f, 1.0f);

	const auto pSnapshot = QmChatAvatar::Capture(Info, 0);
	ASSERT_NE(pSnapshot, nullptr);
	EXPECT_TRUE(pSnapshot->m_Sixup);
	EXPECT_EQ(pSnapshot->m_apParts[protocol7::SKINPART_BODY], pOriginal);
	EXPECT_FLOAT_EQ(pSnapshot->m_aColors[protocol7::SKINPART_BODY].r, 0.1f);
	EXPECT_FLOAT_EQ(pSnapshot->m_aColors[protocol7::SKINPART_FEET].b, 0.6f);
	// 没有单独指定配色的部件保持白色，不继承其它部件。
	EXPECT_FLOAT_EQ(pSnapshot->m_aColors[protocol7::SKINPART_MARKING].r, 1.0f);

	// 该部件改用自定义配色时，必须换成可着色那一份素材。
	Info.m_aSixup[0].m_aUseCustomColors[protocol7::SKINPART_BODY] = true;
	const auto pCustomSnapshot = QmChatAvatar::Capture(Info, 0);
	ASSERT_NE(pCustomSnapshot, nullptr);
	EXPECT_EQ(pCustomSnapshot->m_apParts[protocol7::SKINPART_BODY], pColorable);
}

TEST(QmChatAvatar, CaptureFallsBackToTheSixSkinAndItsBodyFeetColors)
{
	auto pOriginal = std::make_shared<QmChatAvatar::SSource>();
	auto pColorable = std::make_shared<QmChatAvatar::SSource>();
	CTeeRenderInfo Info{};
	Info.m_OriginalRenderSkin.m_QmChatAvatar = pOriginal;
	Info.m_ColorableRenderSkin.m_QmChatAvatar = pColorable;
	Info.m_ColorBody = ColorRGBA(0.7f, 0.1f, 0.1f, 1.0f);
	Info.m_ColorFeet = ColorRGBA(0.1f, 0.7f, 0.1f, 1.0f);

	Info.m_CustomColoredSkin = false;
	const auto pOriginalSnapshot = QmChatAvatar::Capture(Info, 0);
	ASSERT_NE(pOriginalSnapshot, nullptr);
	EXPECT_FALSE(pOriginalSnapshot->m_Sixup);
	EXPECT_EQ(pOriginalSnapshot->m_apParts[protocol7::SKINPART_BODY], pOriginal);
	EXPECT_FLOAT_EQ(pOriginalSnapshot->m_aColors[protocol7::SKINPART_BODY].r, 0.7f);
	EXPECT_FLOAT_EQ(pOriginalSnapshot->m_aColors[protocol7::SKINPART_FEET].g, 0.7f);

	Info.m_CustomColoredSkin = true;
	const auto pColorableSnapshot = QmChatAvatar::Capture(Info, 0);
	ASSERT_NE(pColorableSnapshot, nullptr);
	EXPECT_EQ(pColorableSnapshot->m_apParts[protocol7::SKINPART_BODY], pColorable);
}

TEST(QmChatAvatar, RenderMasksTheAvatarIntoACircle)
{
	const QmChatAvatar::TImage Image = QmChatAvatar::Render(nullptr, "Alice");
	// 圆形之外完全透明，圆心不透明，即头像没有被画成方块。
	EXPECT_EQ(AlphaAt(Image, 0, 0), 0);
	EXPECT_EQ(AlphaAt(Image, QmChatAvatar::SIZE - 1, 0), 0);
	EXPECT_EQ(AlphaAt(Image, 0, QmChatAvatar::SIZE - 1), 0);
	EXPECT_EQ(AlphaAt(Image, QmChatAvatar::SIZE / 2, QmChatAvatar::SIZE / 2), 255);
}

TEST(QmChatAvatar, RenderIsDeterministicPerSenderName)
{
	const QmChatAvatar::TImage First = QmChatAvatar::Render(nullptr, "Alice");
	const QmChatAvatar::TImage Second = QmChatAvatar::Render(nullptr, "Alice");
	// 同一名字必须给出完全相同的图，不依赖任何运行时随机状态。
	EXPECT_EQ(First, Second);

	// 备用配色确实取自名字：不同名字应能给出多于一种底色。
	std::set<std::array<uint8_t, 4>> Backgrounds;
	for(const char *pName : {"Alice", "Bob", "Carol", "Dave", "Erin", "Frank", "Grace", "Heidi"})
	{
		const QmChatAvatar::TImage Image = QmChatAvatar::Render(nullptr, pName);
		Backgrounds.insert(PixelAt(Image, 0, 0));
	}
	EXPECT_GT(Backgrounds.size(), 1u);
}

TEST(QmChatAvatar, RenderWithoutSnapshotStillDrawsAFallbackAvatar)
{
	const QmChatAvatar::TImage Image = QmChatAvatar::Render(nullptr, "Bob");
	// 没有素材时退回几何头像：圆心附近必须有实心内容，而不是只剩底色。
	bool FoundBrightPixel = false;
	for(int y = 0; y < QmChatAvatar::SIZE && !FoundBrightPixel; ++y)
	{
		for(int x = 0; x < QmChatAvatar::SIZE; ++x)
		{
			const auto Pixel = PixelAt(Image, x, y);
			if(Pixel[0] > 240 && Pixel[1] > 240 && Pixel[2] > 240)
			{
				FoundBrightPixel = true;
				break;
			}
		}
	}
	EXPECT_TRUE(FoundBrightPixel);
}

TEST(QmChatAvatar, RenderIgnoresSnapshotWhoseBodySpriteIsEmpty)
{
	auto pEmptySource = std::make_shared<QmChatAvatar::SSource>();
	QmChatAvatar::SSnapshot Snapshot;
	Snapshot.m_apParts[protocol7::SKINPART_BODY] = pEmptySource;

	// 素材在但身体精灵是空的：与完全没有素材走同一条兜底路径。
	const QmChatAvatar::TImage WithEmptySource = QmChatAvatar::Render(&Snapshot, "Bob");
	const QmChatAvatar::TImage WithoutAnySource = QmChatAvatar::Render(nullptr, "Bob");
	EXPECT_EQ(WithEmptySource, WithoutAnySource);
}

TEST(QmChatExportMetadata, OutgoingWhisperUsesTheMessageSourceConnection)
{
	const int aLocalIds[NUM_DUMMIES] = {3, 5};

	// 非私聊：发言者就是消息里的客户端编号。
	EXPECT_EQ(QmChatExport::ResolveSenderId(7, false, 1, aLocalIds, NUM_DUMMIES, 3, false), 7);
	// 自己发出的私聊：协议里的编号是收件人，必须改用来源连接对应的本地身份。
	EXPECT_EQ(QmChatExport::ResolveSenderId(7, true, 1, aLocalIds, NUM_DUMMIES, 3, false), 5);
	EXPECT_EQ(QmChatExport::ResolveSenderId(7, true, 0, aLocalIds, NUM_DUMMIES, 3, false), 3);
	// 录像回放：使用录制时的本地身份，不看当前连接。
	EXPECT_EQ(QmChatExport::ResolveSenderId(7, true, 1, aLocalIds, NUM_DUMMIES, 4, true), 4);
	// 来源连接越界或缺失：退回快照里的本地身份。
	EXPECT_EQ(QmChatExport::ResolveSenderId(7, true, 9, aLocalIds, NUM_DUMMIES, 4, false), 4);
	EXPECT_EQ(QmChatExport::ResolveSenderId(7, true, -1, aLocalIds, NUM_DUMMIES, 4, false), 4);
}
