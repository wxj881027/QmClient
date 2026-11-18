/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_ROUNDED_RECT_GEOMETRY_H
#define ENGINE_CLIENT_ROUNDED_RECT_GEOMETRY_H

#include <base/math.h>

#include <algorithm>
#include <cmath>

struct SRoundedRectGeometry
{
	float m_X = 0.0f;
	float m_Y = 0.0f;
	float m_W = 0.0f;
	float m_H = 0.0f;
	float m_Rounding = 0.0f;
};

inline SRoundedRectGeometry ResolveRoundedRectGeometry(const float X, const float Y, const float W, const float H, const float Rounding, const float PixelSize)
{
	SRoundedRectGeometry Result{X, Y, W, H, 0.0f};
	if(W <= 0.0f || H <= 0.0f)
		return Result;

	const float ResolvedPixelSize = std::max(PixelSize, 0.0f);
	const auto AlignToPixel = [ResolvedPixelSize](const float Value) {
		return ResolvedPixelSize > 0.0f ? std::round(Value / ResolvedPixelSize) * ResolvedPixelSize : Value;
	};
	const float Right = AlignToPixel(X + W);
	const float Bottom = AlignToPixel(Y + H);
	Result.m_X = AlignToPixel(X);
	Result.m_Y = AlignToPixel(Y);
	Result.m_W = Right - Result.m_X;
	Result.m_H = Bottom - Result.m_Y;
	if(Result.m_W <= 0.0f || Result.m_H <= 0.0f)
	{
		Result.m_X = X;
		Result.m_Y = Y;
		Result.m_W = W;
		Result.m_H = H;
	}

	const float MaxRounding = std::max(0.0f, std::min(Result.m_W, Result.m_H) * 0.5f);
	Result.m_Rounding = std::clamp(AlignToPixel(Rounding), 0.0f, MaxRounding);
	return Result;
}

// 圆角曲线档位：0 = 原圆弧（旧外观，不扩展），1 = 超椭圆（p 范数），
// 2 = 五次贝塞尔（端点曲率 0），3 = 圆锥贝塞尔（有理二次，端点曲率不为 0）。
// 数值与 qm_ui_corner_curve 一致，着色器里用同一个枚举顺序。
enum
{
	ROUNDED_CORNER_CURVE_CLASSIC = 0,
	ROUNDED_CORNER_CURVE_SUPERELLIPSE = 1,
	ROUNDED_CORNER_CURVE_QUINTIC = 2,
	ROUNDED_CORNER_CURVE_CONIC = 3,
	// 4 = 苹果连续曲率：UIBezierPath 实测的三段三次贝塞尔，曲线沿直边外扩 0.528665 r。
	ROUNDED_CORNER_CURVE_APPLE = 4,
};

// 苹果连续曲率圆角相对半径多出来的单侧足迹：满强度时曲线从直边 1.528665 r 处起弯
// （= Apple 的 cornerCurveExpansionFactor），最贴角点 0.41225 r。三份着色器用的是
// 同一组数字，改这里必须同步 data/shader/rounded_rect_sdf.frag、vulkan 同名文件与
// data/shader/metal/qmclient.metal。
inline constexpr float APPLE_CORNER_FOOTPRINT = 0.528665f;

// 苹果连续胶囊（SwiftUI Capsule(style: .continuous) / 原生 iOS 胶囊实测）：
// 端帽永远是半径 = 短边一半的正圆，半径不缩、端帽不拉长，多出来的长度只体现在
// 直边提前起弯的肩部。肩部从直边距切点 0.4154 r 处起弯，到端帽 34.24° 处接上，
// 两端 G1；三次贝塞尔手柄 0.2591 r（直边侧）/ 0.3102 r（端帽侧）。
// 对 75 px 高的原生胶囊做亚像素拟合的残差 < 0.5 px，相对正圆胶囊的最大内收 0.021 r。
inline constexpr float APPLE_CAPSULE_SHOULDER = 0.4154f;
inline constexpr float APPLE_CAPSULE_PHI = 0.5976f;
inline constexpr float APPLE_CAPSULE_HANDLE_EDGE = 0.2591f;
inline constexpr float APPLE_CAPSULE_HANDLE_CAP = 0.3102f;

struct SRoundedCornerShape
{
	// 实际生效的圆角半径：含连续曲率的沿边拉长，受生长上限约束。
	float m_Radius = 0.0f;
	// 实际生效的曲线强度：按实际拿到多少生长反解，0 时就是圆弧。
	float m_Amount = 0.0f;
};

// 连续曲率的代价是曲率峰值升高（90° 转弯、端点曲率归零、圆角长度不变时，
// 峰值必然高于圆弧），所以圆角沿边拉长来补偿。拉长量受有效半径上限约束，
// 曲线强度按「实际拿到多少生长」反解：扩不动时（胶囊、短边很近的小控件）
// 自动退回圆弧，不会被强行压弯。着色器里是同一套公式，两处必须逐字对应。
inline SRoundedCornerShape ResolveRoundedCornerShape(const float Radius, const float Width, const float Height, const float Smoothing, const int Curve, const float SeamAmount)
{
	SRoundedCornerShape Shape;
	const float MinHalf = std::max(std::min(Width, Height) * 0.5f, 0.0001f);
	const float ClampedRadius = std::clamp(Radius, 0.0f, MinHalf);
	Shape.m_Radius = ClampedRadius;
	// 原圆弧档就是旧外观：不扩展、也不换曲线。
	const float Amount = Curve == ROUNDED_CORNER_CURVE_CLASSIC ? 0.0f : std::clamp(Smoothing, 0.0f, 1.0f);
	if(ClampedRadius <= 0.0f || Amount <= 0.0f)
		return Shape;
	// 生长上限：普通控件封在短边的 25%，已经是胶囊的元素（半径达到短边 9/10）
	// 放宽到短边的 40%。这里用半边长表示，所以系数是 0.5 / 0.8。
	// 上限只限制生长，不回收调用方本来就给的更大半径。
	// 只有「真的有一段直边」的胶囊才谈得上接缝：正圆（滑块圆点、单选按钮）四个角
	// 本来就是连续的，不该被压成方圆，所以再要求长边明显长于短边。
	const bool Capsule = ClampedRadius >= MinHalf * 0.9f && std::max(Width, Height) * 0.5f > MinHalf * 1.2f;
	const float CapRatio = Capsule ? 0.8f : 0.5f;
	const float MaxRadius = std::max(MinHalf * CapRatio, ClampedRadius);
	Shape.m_Radius = std::clamp(ClampedRadius * (1.0f + 1.3f * Amount), ClampedRadius, MaxRadius);
	Shape.m_Amount = std::clamp((Shape.m_Radius / ClampedRadius - 1.0f) / 1.3f, 0.0f, 1.0f);
	// 苹果连续曲率自己负责沿边外扩（足迹 = r·(1 + 0.528665·s)），半径不再按 1.3 倍生长，
	// 强度直接取平滑度；否则会和曲线自身的扩边叠加，圆角被放大两次。
	if(Curve == ROUNDED_CORNER_CURVE_APPLE)
	{
		Shape.m_Radius = ClampedRadius;
		// 足迹必须塞进短边半高（1 + 0.528665·s ≤ MinHalf / r）：塞不下时上下两个角的肩部
		// 会互相穿过中线，端边被挤出一条外凸的唇边。按短边等比削这一个标量，两个轴共用；
		// 分轴各钳一次就是把端帽拉成 1.53:1 的橄榄头，那才是端头外凸的成因。
		const float MaxAmount = std::max(MinHalf / ClampedRadius - 1.0f, 0.0f) / APPLE_CORNER_FOOTPRINT;
		Shape.m_Amount = std::clamp(std::min(Amount, MaxAmount), 0.0f, 1.0f);
	}
	// 胶囊端帽由端头构造单独负责（正圆 / 丝滑肩部 / 苹果端帽，见着色器），圆角曲线不参与：
	// 半径已经贴到短边一半，任何沿边外扩都会穿过中线。几何兜底因此按正圆胶囊生成，
	// 与端头构造最多差 0.021 r（实测苹果端帽相对正圆胶囊的最大内收）。
	if(Capsule)
		Shape.m_Amount = 0.0f;
	return Shape;
}

// 苹果连续曲率的控制点表：P0 是起点，之后每段两个手柄加一个段末点，共 10 个。
// 前 5 个独立，P5..P9 关于对角线镜像（P[9-i] = P[i].yx），所以只存一半。
// [0] = 三段 30° 圆弧（手柄 k = 4/3·tan(7.5°)），[1] = UIBezierPath(roundedRect:) 实测值
// （Liam Rosenfeld 的反解，与 CoreAnimation 逐像素一致：足迹 1.528665 r）。
// 强度在两组之间逐点线性插值，两个轴共用同一个标量。
// 坐标系：角点在原点，两条直边沿 +x / +y，曲线从 (足迹, 0) 走到 (0, 足迹)。
inline void AppleCornerControlPoint(const int Index, const float Amount, float &OutX, float &OutY)
{
	const float Blend = std::clamp(Amount, 0.0f, 1.0f);
	const int Mirrored = Index > 4 ? 9 - Index : Index;
	float ArcX = 0.0f;
	float ArcY = 0.0f;
	float ContinuousX = 0.0f;
	float ContinuousY = 0.0f;
	switch(Mirrored)
	{
	case 0:
		ArcX = 1.000000f;
		ArcY = 0.000000f;
		ContinuousX = 1.528665f;
		ContinuousY = 0.000000f;
		break;
	case 1:
		ArcX = 0.824462f;
		ArcY = 0.000000f;
		ContinuousX = 1.088493f;
		ContinuousY = 0.000000f;
		break;
	case 2:
		ArcX = 0.652020f;
		ArcY = 0.046206f;
		ContinuousX = 0.868407f;
		ContinuousY = 0.000000f;
		break;
	case 3:
		ArcX = 0.500000f;
		ArcY = 0.133975f;
		ContinuousX = 0.631494f;
		ContinuousY = 0.074911f;
		break;
	default:
		ArcX = 0.347980f;
		ArcY = 0.221744f;
		ContinuousX = 0.372824f;
		ContinuousY = 0.169060f;
		break;
	}
	const float PointX = ArcX + (ContinuousX - ArcX) * Blend;
	const float PointY = ArcY + (ContinuousY - ArcY) * Blend;
	OutX = Index > 4 ? PointY : PointX;
	OutY = Index > 4 ? PointX : PointY;
}

// 连续曲率圆角的采样器：和着色器里同一条曲线（超椭圆 / 五次贝塞尔 / 圆锥曲线角），
// 只供几何光栅化路径使用（模糊掩码、无 SDF 时的多边形圆角）。
// Amount 为 0 时三条曲线都退化成圆弧，采样点就是原来的 cos/sin 表。
inline void RoundedCornerCurveDirection(const int Curve, const float Amount, const int Index, const int Count, float &OutX, float &OutY)
{
	const float A = std::clamp(Amount, 0.0f, 1.0f);
	const float T = Count > 0 ? std::clamp((float)Index / (float)Count, 0.0f, 1.0f) : 0.0f;
	// 强度 0 时三条曲线都必须精确退化成同一个圆弧，阈值与着色器的「Amount <= 0.001
	// 直接走圆」保持一致：几何兜底和 SDF 在同一块面板上不能差出 0.1% 的弧。
	if(A <= 0.001f)
	{
		const float Angle = T * (float)pi * 0.5f;
		OutX = std::cos(Angle);
		OutY = std::sin(Angle);
		return;
	}
	if(Curve == ROUNDED_CORNER_CURVE_APPLE)
	{
		// 苹果连续曲率：三段首尾相连的三次贝塞尔（T 三等分，段内按 U 求值）。
		// 控制点在「角点在原点、直边沿 +x/+y」的坐标系里，这里转回调用方约定
		// （圆弧中心为原点、从 (1,0) 到 (0,1)）：x = 1 - y_apple、y = 1 - x_apple。
		// 满强度时首点落在 (1, -0.528665)，即沿直边外扩 0.528665 r；调用方按半径放大后
		// 正好落在直边上，所以角盒判定也要跟着外扩同样距离。
		const int Segment = T < (1.0f / 3.0f) ? 0 : (T < (2.0f / 3.0f) ? 1 : 2);
		const float U = std::clamp(T * 3.0f - (float)Segment, 0.0f, 1.0f);
		const float V = 1.0f - U;
		const float aWeights[4] = {V * V * V, 3.0f * V * V * U, 3.0f * V * U * U, U * U * U};
		float PointX = 0.0f;
		float PointY = 0.0f;
		for(int Point = 0; Point < 4; Point++)
		{
			float ControlX = 0.0f;
			float ControlY = 0.0f;
			AppleCornerControlPoint(Segment * 3 + Point, A, ControlX, ControlY);
			PointX += aWeights[Point] * ControlX;
			PointY += aWeights[Point] * ControlY;
		}
		OutX = 1.0f - PointY;
		OutY = 1.0f - PointX;
		return;
	}
	if(Curve == ROUNDED_CORNER_CURVE_CONIC)
	{
		// 圆锥曲线角（有理二次贝塞尔），权重与着色器一致。
		const float Weight = 0.70710678f + (1.2f - 0.70710678f) * A;
		const float B0 = (1.0f - T) * (1.0f - T);
		const float B1 = 2.0f * Weight * T * (1.0f - T);
		const float B2 = T * T;
		const float Den = std::max(B0 + B1 + B2, 0.0001f);
		OutX = (B0 + B1) / Den;
		OutY = (B1 + B2) / Den;
		return;
	}
	if(Curve == ROUNDED_CORNER_CURVE_QUINTIC)
	{
		// 五次贝塞尔：前两个控制点都在 x=1 上，端点曲率恒为 0。
		// 0 取曲率匹配的三次圆弧升阶结果，1 取拟合圆锥 W=1.2 手感的那组。
		const float Y1 = 0.3291504f + (0.14f - 0.3291504f) * A;
		const float X2 = 0.8645747f + (1.0f - 0.8645747f) * A;
		const float Y2 = 0.6291504f + (0.80f - 0.6291504f) * A;
		const float U = 1.0f - T;
		const float U2 = U * U;
		const float T2 = T * T;
		const float B0 = U2 * U2 * U;
		const float B1 = 5.0f * T * U2 * U2;
		const float B2 = 10.0f * T2 * U2 * U;
		const float B3 = 10.0f * T2 * T * U2;
		const float B4 = 5.0f * T2 * T2 * U;
		const float B5 = T2 * T2 * T;
		OutX = B0 * 1.0f + B1 * 1.0f + B2 * X2 + B3 * Y2 + B4 * Y1 + B5 * 0.0f;
		OutY = B0 * 0.0f + B1 * Y1 + B2 * Y2 + B3 * X2 + B4 * 1.0f + B5 * 1.0f;
		return;
	}
	// 超椭圆（p 范数）角：指数随强度从 2 升到 4，直边处曲率为 0。
	// 原圆弧档的 Amount 恒为 0，指数回到 2，就是原来的圆弧方向表。
	// 端点处 cos/sin 会落到 -1e-7 这种量级，负底数配分数指数会算出 NaN，
	// 几何兜底的最后一个采样点正好落在那里，所以先把底数夹到 0。
	const float Exponent = 2.0f + 2.0f * A;
	const float Power = 2.0f / Exponent;
	const float Angle = T * (float)pi * 0.5f;
	OutX = std::pow(std::max(std::cos(Angle), 0.0f), Power);
	OutY = std::pow(std::max(std::sin(Angle), 0.0f), Power);
}

#endif
