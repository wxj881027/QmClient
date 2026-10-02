// MSDF 参数 w 分量的编码契约测试。
//
// w 同时表达「描边宽度」和「字形采样模式」（普通 MSDF / Alpha 真 SDF / Duotone），
// 因此三种状态必须落在互不重叠的区间。着色器只在真机上由 GPU 编译执行，
// 无 GPU 时无法观察其行为，所以这里固定 C++ 侧的编码不变式，
// 三个后端着色器（textured_msdf.frag / vulkan/textured_msdf.frag / metal/qmclient.metal）
// 用同一阈值解码，由 QmIconShaderContract 固定其表达式。
//
// 历史缺陷：Duotone 哨兵曾取 -0.002f，落进了真 SDF 的描边编码区间
// （w = -(描边像素 + 0.001)，对任意描边宽度 >= 0.0015px 都 <= -0.0015）。
// 名牌描边/辉光 pass 的描边宽度约为 0.5~2 屏幕像素，于是被误判成 Duotone：
// 片元 alpha 变成 max(Opacity, Sample.a * 5) ≈ 1，整个字形 quad 渲染成实心块。
#include <engine/graphics.h>

#include <gtest/gtest.h>

TEST(QmMsdfParamEncoding, TrueSdfEncodingNeverDecodesAsDuotone)
{
	// 覆盖 0 ~ 4 屏幕像素描边，步长 0.001，包含缩放后出现的任意小数值。
	for(int Step = 0; Step <= 4000; ++Step)
	{
		const float OutlineWidthPx = Step * 0.001f;
		const float W = qm_msdf_param::EncodeTrueSdf(OutlineWidthPx);
		EXPECT_EQ(qm_msdf_param::Decode(W), qm_msdf_param::EMode::TRUE_SDF)
			<< "outline=" << OutlineWidthPx << " w=" << W;
	}
}

TEST(QmMsdfParamEncoding, MsdfEncodingNeverDecodesAsDuotoneOrTrueSdf)
{
	for(int Step = 0; Step <= 4000; ++Step)
	{
		const float OutlineWidthPx = Step * 0.001f;
		const float W = qm_msdf_param::EncodeMsdf(OutlineWidthPx);
		EXPECT_EQ(qm_msdf_param::Decode(W), qm_msdf_param::EMode::MSDF)
			<< "outline=" << OutlineWidthPx << " w=" << W;
	}
}

TEST(QmMsdfParamEncoding, DuotoneSentinelDecodesAsDuotone)
{
	EXPECT_EQ(qm_msdf_param::Decode(qm_msdf_param::DUOTONE_W), qm_msdf_param::EMode::DUOTONE);
	EXPECT_LT(qm_msdf_param::DUOTONE_W, 0.0f);
	EXPECT_GT(qm_msdf_param::DUOTONE_W, -qm_msdf_param::TRUESDF_OFFSET);
}

TEST(QmMsdfParamEncoding, DecodedOutlineRoundTripsBothEncodings)
{
	for(int Step = 0; Step <= 400; ++Step)
	{
		const float OutlineWidthPx = Step * 0.01f;
		EXPECT_NEAR(qm_msdf_param::DecodeOutline(qm_msdf_param::EncodeTrueSdf(OutlineWidthPx)), OutlineWidthPx, 1e-4f);
		EXPECT_NEAR(qm_msdf_param::DecodeOutline(qm_msdf_param::EncodeMsdf(OutlineWidthPx)), OutlineWidthPx, 1e-4f);
	}
	// Duotone 不携带描边，解码出的描边宽度必须是 0，否则会走描边分支画出实心外扩。
	EXPECT_FLOAT_EQ(qm_msdf_param::DecodeOutline(qm_msdf_param::DUOTONE_W), 0.0f);
}
