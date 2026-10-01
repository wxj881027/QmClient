#include <game/client/components/skins.h>
#include <game/client/render.h>

#include <gtest/gtest.h>

TEST(SkinsPreviewLayers, FlagsAreDistinctBits)
{
	EXPECT_NE(TEE_PREVIEW_LAYER_BODY_OUTLINE, TEE_PREVIEW_LAYER_BODY);
	EXPECT_NE(TEE_PREVIEW_LAYER_BACK_FEET_OUTLINE, TEE_PREVIEW_LAYER_BACK_FEET);
	EXPECT_NE(TEE_PREVIEW_LAYER_FRONT_FEET_OUTLINE, TEE_PREVIEW_LAYER_FRONT_FEET);
	EXPECT_NE(TEE_PREVIEW_LAYER_OUTLINE, TEE_PREVIEW_LAYER_BODY);
	EXPECT_NE(TEE_PREVIEW_LAYER_BODY, TEE_PREVIEW_LAYER_FEET);
	EXPECT_NE(TEE_PREVIEW_LAYER_FEET, TEE_PREVIEW_LAYER_EYES);
}

TEST(SkinsPreviewLayers, ZeroMaskResolvesToAllLayers)
{
	EXPECT_EQ(ResolveTeePreviewLayers(0), TEE_PREVIEW_LAYER_ALL);
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_OUTLINE));
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_BODY_OUTLINE));
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_BACK_FEET_OUTLINE));
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_FRONT_FEET_OUTLINE));
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_BODY));
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_FEET));
	EXPECT_TRUE(HasTeePreviewLayer(0, TEE_PREVIEW_LAYER_EYES));
}

TEST(SkinsPreviewLayers, ExplicitMaskSelectsOnlyRequestedFillLayers)
{
	const int Mask = TEE_PREVIEW_LAYER_BODY | TEE_PREVIEW_LAYER_FEET;
	EXPECT_TRUE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_BODY));
	EXPECT_TRUE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_FEET));
	EXPECT_FALSE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_OUTLINE));
	EXPECT_FALSE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_EYES));
}

TEST(SkinsPreviewLayers, OutlineMaskDoesNotSelectFillLayers)
{
	const int Mask = TEE_PREVIEW_LAYER_BODY_OUTLINE | TEE_PREVIEW_LAYER_BACK_FEET_OUTLINE;
	EXPECT_TRUE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_BODY_OUTLINE));
	EXPECT_TRUE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_BACK_FEET_OUTLINE));
	EXPECT_FALSE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_BODY));
	EXPECT_FALSE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_BACK_FEET));
	EXPECT_FALSE(HasTeePreviewLayer(Mask, TEE_PREVIEW_LAYER_FRONT_FEET_OUTLINE));
}

TEST(SkinsContract, StaleTextureHandlesAreNotTreatedAsDrawable)
{
	// 句柄 IsValid() 为真、也不是 null 贴图，但纹理已经不在（设备重建、槽位释放、贴图被卸载）：
	// 这种句柄交给绘制会变成没有贴图的实心块，必须判为不可绘制。
	EXPECT_FALSE(CTeeRenderInfo::IsLiveDrawableTextureState(true, false, false));
	EXPECT_TRUE(CTeeRenderInfo::IsLiveDrawableTextureState(true, false, true));
	// 本来就不可绘制的句柄，是否分配过纹理都不改变结论。
	EXPECT_FALSE(CTeeRenderInfo::IsLiveDrawableTextureState(true, true, true));
	EXPECT_FALSE(CTeeRenderInfo::IsLiveDrawableTextureState(true, true, false));
	EXPECT_FALSE(CTeeRenderInfo::IsLiveDrawableTextureState(false, false, true));
	EXPECT_FALSE(CTeeRenderInfo::IsLiveDrawableTextureState(false, false, false));
	// 与旧判据的关系：旧判据只看「合法且非 null」，新判据在其上再要求纹理仍然分配着。
	for(const bool IsValid : {false, true})
	{
		for(const bool IsNull : {false, true})
		{
			for(const bool IsAllocated : {false, true})
			{
				const bool Drawable = CTeeRenderInfo::IsDrawableTextureState(IsValid, IsNull);
				EXPECT_EQ(CTeeRenderInfo::IsLiveDrawableTextureState(IsValid, IsNull, IsAllocated), Drawable && IsAllocated);
			}
		}
	}
}
