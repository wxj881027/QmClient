// 请抬头享受阳光｜日子很好 我很我---------致咩子
/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QmIconSwap.h"

#include "QmAnimResolve.h"
#include "QmPasswordIcon.h"

#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/qm_icon.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace
{
	struct SPairGlyphs
	{
		const char *m_pGlyphA;
		const char *m_pGlyphB;
	};

	SPairGlyphs ResolvePairGlyphs(EQmIconSwapPair Pair)
	{
		switch(Pair)
		{
		case EQmIconSwapPair::CHEVRON:
			return {FontIcons::FONT_ICON_CHEVRON_DOWN, FontIcons::FONT_ICON_CHEVRON_UP};
		case EQmIconSwapPair::PLAY_PAUSE:
			return {FontIcons::FONT_ICON_PLAY, FontIcons::FONT_ICON_PAUSE};
		case EQmIconSwapPair::PLAY_STOP:
			return {FontIcons::FONT_ICON_PLAY, FontIcons::FONT_ICON_STOP};
		case EQmIconSwapPair::SQUARE_PLUS_MINUS:
			return {FontIcons::FONT_ICON_SQUARE_PLUS, FontIcons::FONT_ICON_SQUARE_MINUS};
		case EQmIconSwapPair::STAR_FAVORITE:
			// 两端同一码位：A 端描边（强制 ICON_FONT），B 端由 DrawQmIcon 按
			// qm_ui_favorite_icon_filled 自动切 ICON_FONT_FILL。
			return {FontIcons::FONT_ICON_STAR, FontIcons::FONT_ICON_STAR};
		case EQmIconSwapPair::EYE:
		default:
			return {FontIcons::FONT_ICON_EYE, FontIcons::FONT_ICON_EYE_SLASH};
		}
	}

	// 取色用的代表图标：只影响语义色/信号族的判定，chevron 与 eye 都不属于这两类。
	EQmIcon IconForPair(EQmIconSwapPair Pair)
	{
		switch(Pair)
		{
		case EQmIconSwapPair::CHEVRON: return EQmIcon::CHEVRON_DOWN;
		case EQmIconSwapPair::PLAY_PAUSE:
		case EQmIconSwapPair::PLAY_STOP: return EQmIcon::PLAY;
		case EQmIconSwapPair::SQUARE_PLUS_MINUS: return EQmIcon::SQUARE_PLUS;
		case EQmIconSwapPair::STAR_FAVORITE: return EQmIcon::STAR;
		case EQmIconSwapPair::EYE:
		default: return EQmIcon::EYE;
		}
	}

	// 统一用 DoLabel 画图标：字号由调用方给定，取色直接用调用方颜色——
	// 与 DrawQmIcon 的全局图标色管线隔离，保证帧条与字形回退逐像素同源。
	void DrawIconGlyph(const IUiContext &Ctx, const CUIRect &Rect, const char *pGlyph, float FontSize, const ColorRGBA &Color)
	{
		if(Ctx.m_pUi == nullptr || pGlyph == nullptr)
			return;
		ITextRender *pTextRender = Ctx.m_pUi->TextRender();
		const ColorRGBA PreviousColor = pTextRender->GetTextColor();
		const ColorRGBA PreviousOutlineColor = pTextRender->GetTextOutlineColor();
		const ColorRGBA PreviousSelectionColor = pTextRender->GetTextSelectionColor();
		const unsigned PreviousFlags = pTextRender->GetRenderFlags();
		const EFontPreset PreviousPreset = pTextRender->GetFontPreset();
		pTextRender->TextColor(Color);
		pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
		pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		Ctx.m_pUi->DoLabel(&Rect, pGlyph, FontSize, TEXTALIGN_MC);
		pTextRender->SetRenderFlags(PreviousFlags);
		pTextRender->SetFontPreset(PreviousPreset);
		pTextRender->TextOutlineColor(PreviousOutlineColor);
		pTextRender->TextSelectionColor(PreviousSelectionColor);
		pTextRender->TextColor(PreviousColor);
	}

	// 帧条纹理按「交换对」各存一份：设置页里折叠箭头与密码框会同时出现，
	// 共用单槽会在两者之间反复 Unload/Load。
	//
	// 纹理在运行时按「目标物理像素边长」重采样，而不是按 UI 单位。原因有两层：
	//   1. 绘制单位是 UI 虚拟单位（高 600/qm_ui_scale），到物理像素还有一个倍率
	//      （本机 1266 高的窗口约 2.1 倍）；纹理若按 UI 单位取边长，就会被 GPU
	//      放大 2 倍，双线性把笔画摊平——无论源资产多清晰都一定糊。
	//   2. 到了物理像素后仍需面积平均（box）而不是 GPU 三角滤波：64px 缩到 30px，
	//      box 峰值 243、GPU 缩放峰值 217（同字号字形 246）。
	// 只做 box，不加锐化/膨胀：实测锐化把墨量从 15.9 压到 11.9、膨胀把背景抬亮，
	// 两者都只是把偏差换个方向。
	class CFrameCache
	{
	public:
		bool Ensure(IGraphics *pGraphics, EQmIconSwapPair Pair, const char *pAsset, int DrawPixels)
		{
			SSlot &Slot = m_aSlots[static_cast<int>(Pair)];
			const uint32_t ResetVersion = pGraphics->GraphicsResourcesResetVersion();
			if(Slot.m_Texture.IsValid() && ResetVersion == Slot.m_ResetVersion && Slot.m_Pixels == DrawPixels && str_comp(Slot.m_aAsset, pAsset) == 0)
				return true;
			if(Slot.m_Texture.IsValid())
				pGraphics->UnloadTexture(&Slot.m_Texture);
			Slot.m_Texture = IGraphics::CTextureHandle();
			Slot.m_Pixels = DrawPixels;
			Slot.m_ResetVersion = ResetVersion;

			// 解码后的源帧条按 pair 缓存：hover 缩放会让目标尺寸变化一两次，
			// 每次都重新解 PNG（1536×128）会掉帧。
			if(Slot.m_aSource.empty() || str_comp(Slot.m_aAsset, pAsset) != 0)
			{
				CImageInfo Loaded;
				if(!pGraphics->LoadPng(Loaded, pAsset, IStorage::TYPE_ALL))
				{
					Slot.m_aAsset[0] = '\0';
					return false;
				}
				const size_t Bytes = Loaded.PixelSize() * Loaded.m_Width * Loaded.m_Height;
				Slot.m_aSource.assign(Loaded.m_pData, Loaded.m_pData + Bytes);
				Slot.m_SourceWidth = static_cast<int>(Loaded.m_Width);
				Slot.m_SourceHeight = static_cast<int>(Loaded.m_Height);
				Slot.m_SourceFormat = Loaded.m_Format;
				Loaded.Free();
				str_copy(Slot.m_aAsset, pAsset);
			}

			CImageInfo Source;
			Source.m_Width = static_cast<size_t>(Slot.m_SourceWidth);
			Source.m_Height = static_cast<size_t>(Slot.m_SourceHeight);
			Source.m_Format = Slot.m_SourceFormat;
			Source.m_pData = Slot.m_aSource.data();
			CImageInfo Target;
			const bool Resampled = Resample(Source, DrawPixels, Target);
			if(!Resampled)
				return false;
			Slot.m_Texture = pGraphics->LoadTextureRawMove(Target, 0, "qm_icon_swap_frames");
			if(!Slot.m_Texture.IsValid())
				return false;
			return true;
		}

		IGraphics::CTextureHandle Texture(EQmIconSwapPair Pair) const { return m_aSlots[static_cast<int>(Pair)].m_Texture; }

	private:
		// 源：FRAME_COUNT 帧 × SrcPixels，上下两半（上填充、下描边）。
		// 目标：FRAME_COUNT 帧 × DstPixels，同样两半，白色顶点由绘制时的 SetColor 着色。
		// 逐帧逐半独立处理，相邻帧不会互相串色。
		static bool Resample(const CImageInfo &Source, int DstPixels, CImageInfo &Target)
		{
			if(Source.m_Format != CImageInfo::FORMAT_RGBA || Source.m_Width == 0 || Source.m_Height == 0 || DstPixels <= 0)
				return false;
			const int SrcPixels = static_cast<int>(Source.m_Height) / 2;
			if(SrcPixels <= 0)
				return false;
			const int DstWidth = qm_password_icon::FRAME_COUNT * DstPixels;
			const int DstHeight = DstPixels * 2;
			const size_t DstStride = static_cast<size_t>(DstWidth) * 4;
			Target.m_Width = DstWidth;
			Target.m_Height = DstHeight;
			Target.m_Format = CImageInfo::FORMAT_RGBA;
			Target.m_pData = static_cast<uint8_t *>(calloc(DstStride * DstHeight, 1));
			if(Target.m_pData == nullptr)
				return false;

			const float Scale = static_cast<float>(SrcPixels) / static_cast<float>(DstPixels);
			const int SrcWidth = static_cast<int>(Source.m_Width);
			for(int Layer = 0; Layer < 2; ++Layer)
			{
				for(int Frame = 0; Frame < qm_password_icon::FRAME_COUNT; ++Frame)
				{
					for(int dy = 0; dy < DstPixels; ++dy)
					{
						const float Sy0 = dy * Scale;
						const float Sy1 = (dy + 1) * Scale;
						const int SrcY0 = Layer * SrcPixels;
						uint8_t *pDstRow = Target.m_pData + (static_cast<size_t>(Layer * DstPixels + dy) * DstWidth + Frame * DstPixels) * 4;
						for(int dx = 0; dx < DstPixels; ++dx)
						{
							const float Sx0 = dx * Scale;
							const float Sx1 = (dx + 1) * Scale;
							float Sum = 0.0f;
							float Weight = 0.0f;
							for(int sy = static_cast<int>(Sy0); sy < static_cast<int>(std::ceil(Sy1)); ++sy)
							{
								const float Wy = std::min(static_cast<float>(sy + 1), Sy1) - std::max(static_cast<float>(sy), Sy0);
								if(Wy <= 0.0f)
									continue;
								const int SrcY = SrcY0 + std::min(sy, SrcPixels - 1);
								const uint8_t *pSrcRow = Source.m_pData + static_cast<size_t>(SrcY) * SrcWidth * 4;
								for(int sx = static_cast<int>(Sx0); sx < static_cast<int>(std::ceil(Sx1)); ++sx)
								{
									const float Wx = std::min(static_cast<float>(sx + 1), Sx1) - std::max(static_cast<float>(sx), Sx0);
									if(Wx <= 0.0f)
										continue;
									const int SrcX = Frame * SrcPixels + std::min(sx, SrcPixels - 1);
									Sum += pSrcRow[SrcX * 4 + 3] * Wx * Wy;
									Weight += Wx * Wy;
								}
							}
							pDstRow[dx * 4 + 0] = 255;
							pDstRow[dx * 4 + 1] = 255;
							pDstRow[dx * 4 + 2] = 255;
							pDstRow[dx * 4 + 3] = Weight > 0.0f ? static_cast<uint8_t>(std::lround(Sum / Weight)) : 0;
						}
					}
				}
			}
			return true;
		}

		struct SSlot
		{
			IGraphics::CTextureHandle m_Texture;
			char m_aAsset[64] = "";
			int m_Pixels = 0;
			uint32_t m_ResetVersion = 0;
			// 解码后的源帧条，避免 hover 改变目标尺寸时重复解析 PNG。
			std::vector<uint8_t> m_aSource;
			int m_SourceWidth = 0;
			int m_SourceHeight = 0;
			CImageInfo::EImageFormat m_SourceFormat = CImageInfo::FORMAT_UNDEFINED;
		};
		SSlot m_aSlots[static_cast<int>(EQmIconSwapPair::COUNT)];
	};

	CFrameCache s_FrameCache;

	// 与密码图标同一条弹簧（300/32，ζ≈0.92，稳定约 0.26s）。
	constexpr SUiSpringConfig ICON_SWAP_SPRING{1.0f, 300.0f, 32.0f, 0.01f, 0.05f};
} // namespace

bool ui_widget::DrawIconSwap(const IUiContext &Ctx, const CUIRect &Rect, EQmIconSwapPair Pair, float FontSize, float Progress, const ColorRGBA &Color)
{
	if(Ctx.m_pUi == nullptr || FontSize <= 0.0f || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return false;
	const float Clamped = std::clamp(Progress, 0.0f, 1.0f);
	// 取色与静止态同源：静止态走 DoLabel/DrawQmIcon，图标字形最终由
	// ConfiguredQmUiIconColor 染色（默认 qm_ui_icon_color=1 即纯白）。帧条此前直接用
	// 调用方颜色，于是「静止白、动画灰」——同一条管线只走一次，两边才对得上。
	const ColorRGBA IconColor = ConfiguredQmUiIconColor(Color, IconForPair(Pair));

	// 有资产走烘焙帧条：整帧替换字形，过渡靠帧序列而非 alpha 叠加。
	// 纹理边长取「物理像素」，quad 仍按 UI 单位给尺寸：QmUi 画在虚拟分辨率上
	// （高 600/qm_ui_scale），字形由 FreeType 按物理像素光栅化，帧条若按 UI 单位
	// 取纹理就会被 GPU 放大同一倍率（本机约 2.1 倍），笔画立刻被摊平成「糊」。
	const float Side = qm_password_icon::FrameSide(FontSize);
	const float PhysicalScale = qm_password_icon::ResolvePhysicalScale(Ctx.m_pUi->PixelSize());
	const int Pixels = qm_password_icon::ResolveFrameTexturePixels(Side, PhysicalScale);
	const char *pAsset = IconSwapAssetName(Pair, NormalizeQmIconWeight(g_Config.m_QmUiIconWeight));
	IGraphics *pGraphics = Ctx.m_pUi->Graphics();
	if(pAsset != nullptr && pGraphics != nullptr && s_FrameCache.Ensure(pGraphics, Pair, pAsset, Pixels))
	{
		const int Frame = qm_password_icon::FrameIndex(Clamped);
		// 位置取整到物理像素网格：落在半像素上时双线性会把笔画糊开一圈。
		const float DrawX = std::round((Rect.x + (Rect.w - Side) * 0.5f) * PhysicalScale) / PhysicalScale;
		const float DrawY = std::round((Rect.y + (Rect.h - Side) * 0.5f) * PhysicalScale) / PhysicalScale;
		const CUIRect FrameRect{DrawX, DrawY, Side, Side};
		const IGraphics::CQuadItem Quad(FrameRect.x, FrameRect.y, FrameRect.w, FrameRect.h);
		const float U0 = qm_password_icon::FrameU0(Frame, Pixels);
		const float U1 = qm_password_icon::FrameU1(Frame, Pixels);
		pGraphics->WrapClamp();
		pGraphics->TextureSet(s_FrameCache.Texture(Pair));
		pGraphics->BlendNormal();
		// 与 CTextRender 同序：先描边、再填充。字体图标的每个字形都带一层
		// DefaultTextOutlineColor（黑 30%）外扩描边，帧条缺这一层就会「没有边框」，
		// 白色直接摊到背景上，观感发白、发糊。描边色取当前文本描边色，跟随调用方。
		const ColorRGBA OutlineColor = Ctx.m_pUi->TextRender()->GetTextOutlineColor();
		if(OutlineColor.a > 0.0f)
		{
			pGraphics->QuadsBegin();
			pGraphics->SetColor(OutlineColor);
			pGraphics->QuadsSetSubset(U0, qm_password_icon::FrameV0(true, Pixels), U1, qm_password_icon::FrameV1(true, Pixels));
			pGraphics->QuadsDrawTL(&Quad, 1);
			pGraphics->QuadsEnd();
		}
		pGraphics->QuadsBegin();
		pGraphics->SetColor(IconColor);
		pGraphics->QuadsSetSubset(U0, qm_password_icon::FrameV0(false, Pixels), U1, qm_password_icon::FrameV1(false, Pixels));
		pGraphics->QuadsDrawTL(&Quad, 1);
		pGraphics->QuadsEnd();
		return true;
	}

	// 回落：交叉淡化 + 反向缩放。两个字形同尺寸同位置重叠时中间态会糊成双轮廓，
	// 让淡出的那个缩到 0.90、淡入的那个从 0.90 长回原尺寸，交叉才有让位方向。
	const SPairGlyphs Glyphs = ResolvePairGlyphs(Pair);
	const float Fade = Clamped * Clamped * (3.0f - 2.0f * Clamped);
	constexpr float CROSS_SCALE = 0.10f;
	DrawIconGlyph(Ctx, Rect, Glyphs.m_pGlyphA, FontSize * (1.0f - CROSS_SCALE * Fade), IconColor.WithMultipliedAlpha(1.0f - Fade));
	DrawIconGlyph(Ctx, Rect, Glyphs.m_pGlyphB, FontSize * (1.0f - CROSS_SCALE * (1.0f - Fade)), IconColor.WithMultipliedAlpha(Fade));
	return true;
}

float ui_widget::ResolveIconSwapProgress(const IUiContext &Ctx, const uint64_t NodeKey, const EQmIconSwapPair Pair, const float Target, bool *pActiveOut, const float Seconds, const bool AutoSpring)
{
	if(pActiveOut != nullptr)
		*pActiveOut = false;
	if(Ctx.m_pUi == nullptr)
		return Target;
	CUiV2AnimationRuntime *pAnim = Ctx.m_pAnim != nullptr ? Ctx.m_pAnim : Ctx.m_pUi->QmAnimationRuntime();
	if(pAnim == nullptr)
		return Target;
	// 只有真实出图趟推进轨道；测量/纯背景趟只读当前值。卡片内容一帧内会被画进多个
	// 渲染目标，两趟各推一次会让进度跑到前一趟前面，两层的帧条就对不上了。
	const bool DrawPass = !Ctx.m_pUi->AnimationsBlocked();
	float Progress = Target;
	if(!DrawPass)
		Progress = pAnim->GetValue(NodeKey, EUiAnimProperty::SCALE, Target);
	else if(AutoSpring && IconSwapAssetName(Pair, NormalizeQmIconWeight(g_Config.m_QmUiIconWeight)) != nullptr)
		Progress = ResolveUiAnimSpringValue(*pAnim, NodeKey, EUiAnimProperty::SCALE, Target, ICON_SWAP_SPRING, 2);
	else
		Progress = ResolveUiAnimValue(*pAnim, NodeKey, EUiAnimProperty::SCALE, Target, Seconds, EEasing::EASE_IN_OUT);
	const bool Active = pAnim->HasActiveAnimation(NodeKey, EUiAnimProperty::SCALE);
	if(pActiveOut != nullptr)
		*pActiveOut = Active;
	// 轨道停下后直接给目标值：帧条的末帧与静止态字形必须严格同形，差半帧就会闪一下。
	return Active ? std::clamp(Progress, 0.0f, 1.0f) : Target;
}

ui_widget::SIconSwapBinding ui_widget::ResolveIconSwapBinding(const IUiContext &Ctx, const EQmIconSwapPair Pair, const EQmIcon IconA, const char *pGlyphA, const EQmIcon IconB, const char *pGlyphB, const float Target, const uint64_t Salt, const void *pId, const float Seconds, const bool AutoSpring)
{
	SIconSwapBinding Binding;
	Binding.m_Pair = Pair;
	Binding.m_IconA = IconA;
	Binding.m_IconB = IconB;
	Binding.m_pGlyphA = pGlyphA;
	Binding.m_pGlyphB = pGlyphB;
	Binding.m_Progress = Target;
	Binding.m_ForceOutlineA = Pair == EQmIconSwapPair::STAR_FAVORITE;
	if(pId == nullptr)
		return Binding;
	Binding.m_NodeKey = BuildUiAnimNodeKey(Ctx.m_ScopeHash ^ Salt, reinterpret_cast<uint64_t>(pId));
	Binding.m_Progress = ResolveIconSwapProgress(Ctx, Binding.m_NodeKey, Pair, Target, &Binding.m_Animate, Seconds, AutoSpring);
	return Binding;
}

void ui_widget::DrawIconSwapBinding(const IUiContext &Ctx, const CUIRect &Rect, const ColorRGBA &Color, const void *pUser)
{
	const auto *pBinding = static_cast<const SIconSwapBinding *>(pUser);
	if(Ctx.m_pUi == nullptr || pBinding == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	// 帧条按 QmIconFontSize 换算画布边长，与 DrawQmIcon 的静止态字号同源；
	// 字号取错会让过渡期的图标比静止态大一圈。
	const float FontSize = QmIconFontSize(Rect);
	if(pBinding->m_Animate && DrawIconSwap(Ctx, Rect, pBinding->m_Pair, FontSize, pBinding->m_Progress, Color))
		return;
	const bool End = pBinding->m_Progress >= 0.5f;
	if(End)
		Ctx.m_pUi->DrawQmIcon(Rect, pBinding->m_IconB, pBinding->m_pGlyphB, Color);
	else
		Ctx.m_pUi->DrawQmIcon(Rect, pBinding->m_IconA, pBinding->m_pGlyphA, Color, pBinding->m_ForceOutlineA);
}
