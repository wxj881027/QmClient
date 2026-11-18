#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_POSE_CAPTURE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_POSE_CAPTURE_H

// 合影静帧的超采样尺寸计算。
//
// 静帧默认按窗口渲染尺寸出图（1x）。倍数 N 让整幅画面重新光栅化到 N 倍大小的
// 离屏 render target：世界投影只由宽高比与相机缩放决定，与像素尺寸无关，
// 所以取景不变，几何、皮肤与名字文字都按目标尺寸重新采样，输出像素是
// 「窗口渲染尺寸 × N」而不是把窗口截图放大。这里只放纯计算，方便单测。

#include <base/vmath.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace qm_pose
{
	// 命令与界面只接受 1-4 倍。再大的倍数对显存与后端纹理上限都不可控，
	// 越界值不报错而是回到 1 倍：一个坏参数不该让整条渲染路径失效。
	constexpr int MIN_CAPTURE_SCALE = 1;
	constexpr int MAX_CAPTURE_SCALE = 4;
	// 离屏目标与读回缓冲的尺寸上限（4 倍 4K 也在此之内）。
	constexpr int MAX_CAPTURE_DIMENSION = 16384;
	// 像素预算：纹理上限拦不住显存与编码耗时。4K 窗口 4 倍 = 15360×8640，
	// 颜色 + 深度 + 读回各 530 MB，PNG 编码在主线程要几十秒——实测直接触发
	// 「客户端卡死」看门狗。这里的 8K（7680×4320）是配置项出现前的固定上限，
	// 现在只作为参考值与单测基准；实际预算由 qm_pose_capture_max_megapixels 决定。
	constexpr int64_t MAX_CAPTURE_PIXELS = 7680LL * 4320LL;

	// 配置值换算成像素预算：按百万像素计，0 = 不限。
	// 抓帧（engine）与预览信息行（菜单）共用这一份，避免两处各写一遍
	// 1000*1000 之后出现「显示 2 倍、实际出 1 倍」的漂移。
	inline int64_t ConfiguredPixelBudget(int Megapixels)
	{
		return Megapixels > 0 ? static_cast<int64_t>(Megapixels) * 1000 * 1000 : 0;
	}

	inline int NormalizeCaptureScale(int Scale)
	{
		if(Scale < MIN_CAPTURE_SCALE || Scale > MAX_CAPTURE_SCALE)
			return MIN_CAPTURE_SCALE;
		return Scale;
	}

	struct SCaptureSize
	{
		// 0 表示这次不能做超采样（窗口尺寸本身非法或超出上限），调用方退回窗口截图。
		int m_Width = 0;
		int m_Height = 0;
		// 实际使用的倍数：请求值被规格化，并在超出尺寸上限时逐级下调。
		int m_Scale = MIN_CAPTURE_SCALE;
		// 实际倍数与请求不一致时为真，调用方据此提示「已降级」。
		bool m_Clamped = false;
	};

	// 抓帧在渲染后这一步的决策。只依赖四个事实，抽成纯函数是为了能单测——
	// 两条都是实测过的回归：
	//   1) 皮肤闸门直到渲染后才放行时，本帧没有离屏目标；这时直接抓窗口帧缓冲
	//      会让超采样请求静默降级成 1 倍（4 倍实测掉成 1280x720），必须延一帧；
	//   2) 离屏帧一旦开始就必须在同一帧读回收口，不能跨帧存在（Vulkan 下带着
	//      活动渲染目标 Swap 会卡死图形线程）。所以只要目标还活动，先收口。
	enum class ECaptureSubmit
	{
		// 皮肤还没就绪：本帧不拍。调用方负责把仍活动的离屏帧丢弃收口。
		WAIT_FOR_SKINS,
		// 离屏帧已经画完：读回并落盘。
		SUBMIT_OFFSCREEN,
		// 超采样请求，但本帧没开过离屏帧：延到下一帧按超采样重拍。
		DEFER_TO_NEXT_FRAME,
		// 1 倍，或后端确实开不了离屏目标：抓窗口帧缓冲。
		SUBMIT_WINDOW,
	};

	inline ECaptureSubmit DecideCaptureSubmit(int Scale, bool OffscreenAttemptedThisFrame, bool OffscreenActive, bool SkinsReady)
	{
		if(!SkinsReady)
			return ECaptureSubmit::WAIT_FOR_SKINS;
		// 目标已活动优先收口：带着它 Swap 是死锁路径，和倍数无关。
		if(OffscreenActive)
			return ECaptureSubmit::SUBMIT_OFFSCREEN;
		if(Scale > 1 && !OffscreenAttemptedThisFrame)
			return ECaptureSubmit::DEFER_TO_NEXT_FRAME;
		return ECaptureSubmit::SUBMIT_WINDOW;
	}

	// 分块渲染的取景：把整幅画面按 N×N 切开，每块仍用窗口分辨率渲染，
	// 拼起来才是 N 倍的图。Zoom 是「一屏看多少世界单位」的倒数关系，
	// 所以单块要除以 N（更近），中心按格心偏移。纯计算，便于单测。
	struct SView
	{
		vec2 m_Center = vec2(0.0f, 0.0f);
		float m_Zoom = 1.0f;
	};

	inline SView TileView(vec2 FullCenter, float FullZoom, float BaseWidth, float BaseHeight, int Scale, int TileX, int TileY)
	{
		const int N = NormalizeCaptureScale(Scale);
		if(N <= 1 || BaseWidth <= 0.0f || BaseHeight <= 0.0f || FullZoom <= 0.0f)
			return {FullCenter, FullZoom};
		const float WorldWidth = BaseWidth * FullZoom;
		const float WorldHeight = BaseHeight * FullZoom;
		const float Half = (float)N * 0.5f;
		const vec2 Offset = vec2(
			((float)TileX + 0.5f - Half) * WorldWidth / (float)N,
			((float)TileY + 0.5f - Half) * WorldHeight / (float)N);
		return {FullCenter + Offset, FullZoom / (float)N};
	}

	// 分块渲染的格子几何：整幅图按 N×N 切开，每块的大小与逻辑原点。
	// 分块的意义是把「整幅 N 倍」的离屏目标拆成 N×N 个「窗口大小」的目标，
	// 从而绕开后端纹理边长上限，显存峰值也从整幅降到一块。
	struct STile
	{
		// 这一块在整幅图里的像素原点（逻辑坐标）。
		int m_OriginX = 0;
		int m_OriginY = 0;
		// 这一块的像素尺寸。整幅宽高不能被 N 整除时，最后一行/列少几个像素。
		int m_Width = 0;
		int m_Height = 0;
	};

	// 求第 (TileX, TileY) 块。SumWidth/SumHeight 是整幅目标尺寸。
	inline STile ComputeTile(int SumWidth, int SumHeight, int Scale, int TileX, int TileY)
	{
		STile Tile;
		const int N = NormalizeCaptureScale(Scale);
		if(SumWidth <= 0 || SumHeight <= 0 || TileX < 0 || TileY < 0 || TileX >= N || TileY >= N)
			return Tile;
		// 用「按边界取整」而不是固定块宽：整幅尺寸除以 N 除不尽时，
		// 固定块宽会在最后拼出一条缝或越界，这里让每块的边界由比例直接决定。
		const int Left = (int)((int64_t)SumWidth * TileX / N);
		const int Right = (int)((int64_t)SumWidth * (TileX + 1) / N);
		const int Top = (int)((int64_t)SumHeight * TileY / N);
		const int Bottom = (int)((int64_t)SumHeight * (TileY + 1) / N);
		Tile.m_OriginX = Left;
		Tile.m_OriginY = Top;
		Tile.m_Width = Right - Left;
		Tile.m_Height = Bottom - Top;
		return Tile;
	}

	// 成片锐化（unsharp mask）：超采样成片在缩放到屏幕尺寸显示时会被平台做一次
	// 双线性缩小，边缘会略微发软。这里在编码前把边缘的高频抬回来一点。
	//
	// Amount 是 0-100 的百分比，0 表示完全不处理（默认），保证既有成片逐像素不变。
	// 只处理 alpha != 0 的像素：离屏目标初值是全透明，未绘制区域必须保持透明，
	// 否则透明像素的 RGB 会被邻域"染"出颜色，缩放后出现彩边。
	//
	// 输入是紧密排列的 RGBA，缓冲区须包含 Width × Height 个像素。
	inline void ApplyUnsharpMask(uint8_t *pPixels, int Width, int Height, int Amount)
	{
		if(pPixels == nullptr || Width <= 2 || Height <= 2 || Amount <= 0)
			return;
		constexpr size_t PIXEL_BYTES = 4;
		if(static_cast<size_t>(Width) > std::numeric_limits<size_t>::max() / PIXEL_BYTES / static_cast<size_t>(Height))
			return;
		const int Strength = minimum(Amount, 100);
		// 只在检测到边缘时锐化，避免把缓变渐变变成分层色带。
		constexpr int EDGE_THRESHOLD = 12;

		// 就地写回时只保留当前行和上一行的原始像素，下一行仍可从原图读取。
		const size_t RowBytes = static_cast<size_t>(Width) * PIXEL_BYTES;
		std::unique_ptr<uint8_t[]> pRows(new(std::nothrow) uint8_t[RowBytes * 2]);
		if(!pRows)
			return; // 保留原图供后续编码。
		uint8_t *pCurrent = pRows.get();
		uint8_t *pPrevious = pCurrent + RowBytes;

		for(int Y = 0; Y < Height; ++Y)
		{
			uint8_t *pOutput = pPixels + static_cast<size_t>(Y) * RowBytes;
			std::memcpy(pCurrent, pOutput, RowBytes);
			const uint8_t *pAbove = Y > 0 ? pPrevious : pCurrent;
			const uint8_t *pBelow = Y + 1 < Height ? pOutput + RowBytes : pCurrent;
			for(size_t Index = 0; Index < RowBytes; Index += PIXEL_BYTES)
			{
				if(pCurrent[Index + 3] == 0)
					continue;
				const size_t Left = Index == 0 ? Index : Index - PIXEL_BYTES;
				const size_t Right = Index + PIXEL_BYTES < RowBytes ? Index + PIXEL_BYTES : Index;
				int aDiff[3];
				int Response = 0;
				for(int Channel = 0; Channel < 3; ++Channel)
				{
					const int Sum = pCurrent[Left + Channel] + pCurrent[Right + Channel] +
							pAbove[Index + Channel] + pBelow[Index + Channel];
					aDiff[Channel] = 4 * pCurrent[Index + Channel] - Sum;
					Response = maximum(Response, maximum(aDiff[Channel], -aDiff[Channel]));
				}
				if(Response <= EDGE_THRESHOLD)
					continue;

				// 直接给拉普拉斯响应加权，避免强度取整后改变平坦通道的亮度。
				for(int Channel = 0; Channel < 3; ++Channel)
				{
					const int Value = (16 * pCurrent[Index + Channel] + Strength * aDiff[Channel]) / 16;
					pOutput[Index + Channel] = static_cast<uint8_t>(minimum(maximum(Value, 0), 255));
				}
			}
			std::swap(pCurrent, pPrevious);
		}
	}

	inline SCaptureSize ComputeCaptureSize(int WindowWidth, int WindowHeight, int RequestedScale, int MaxDimension = MAX_CAPTURE_DIMENSION, int64_t MaxPixels = 0)
	{
		SCaptureSize Size;
		const int Scale = NormalizeCaptureScale(RequestedScale);
		Size.m_Scale = Scale;
		Size.m_Clamped = Scale != RequestedScale;
		if(WindowWidth <= 0 || WindowHeight <= 0 || MaxDimension <= 0)
			return Size;
		// 窗口本身就超过上限时没有可用的超采样尺寸：宁可退回窗口截图，也不创建必然失败的目标。
		if(WindowWidth > MaxDimension || WindowHeight > MaxDimension)
		{
			Size.m_Clamped = true;
			return Size;
		}
		int Used = Scale;
		while(Used > MIN_CAPTURE_SCALE &&
			((int64_t)WindowWidth * Used > MaxDimension || (int64_t)WindowHeight * Used > MaxDimension ||
				(MaxPixels > 0 && (int64_t)WindowWidth * Used * WindowHeight * Used > MaxPixels)))
			Used--;
		if(Used != Scale)
			Size.m_Clamped = true;
		Size.m_Scale = Used;
		Size.m_Width = WindowWidth * Used;
		Size.m_Height = WindowHeight * Used;
		return Size;
	}
}

#endif
