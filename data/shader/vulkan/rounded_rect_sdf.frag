#version 450
#extension GL_ARB_separate_shader_objects : enable

#ifdef TW_ROUNDED_SDF_TEXTURED
layout(set = 0, binding = 0) uniform sampler2D gTextureSampler;
#endif

// 纹理变体的管线布局有两个描述符集：set 0 放图片采样器，参数块跟着落到 set 1；
// 无纹理变体只有一个 set，参数块就在 set 0。set 号写错时参数块读到的是未绑定
// 内存（全 0），片元会把「启用纹理」当成关闭，照片整块透明——这正是照片卡只剩
// 外框的原因。
#ifdef TW_ROUNDED_SDF_TEXTURED
layout(std140, set = 1, binding = 1)
#else
layout(std140, set = 0, binding = 1)
#endif
uniform SRoundedRectSdf
{
	vec4 gRoundedRectSdfData[18];
} gSdf;

layout(location = 0) noperspective in vec2 texCoord;
layout(location = 0) out vec4 FragClr;

// 圆角形状由曲线档位 CurveType、连续曲率强度 Smoothing（0 = 圆弧）和外扩 Expand
// （整块形状按距离场向外偏移，等价于同一条曲线的等距外扩）描述。
// 阴影 / 描边 / 填充都从这一份距离场派生，曲率变化时层与层才不会错位。
//
// 曲线档位：0 = 原圆弧（旧外观），1 = 超椭圆（p 范数），2 = 五次贝塞尔，
// 3 = 圆锥贝塞尔（有理二次）。超椭圆与五次贝塞尔的端点曲率都是 0，是真正的
// 连续曲率；圆锥贝塞尔的端点曲率恒为 1/(2W^2 E)，只能把接缝跳变压小，做不到 0。
// 圆锥曲线角（有理二次 Bézier）：P0=(E,0)、P1=(E,E)、P2=(0,E)，权重 W。
// W = 0.7071 时正好是圆弧；W 越大角越平。零等值线就是这条曲线本身。
vec2 ConicCornerPoint(float T, float E, float W)
{
	float B0 = (1.0 - T) * (1.0 - T);
	float B1 = 2.0 * W * T * (1.0 - T);
	float B2 = T * T;
	float Den = B0 + B1 + B2;
	return vec2((B0 + B1) * E, (B1 + B2) * E) / Den;
}

vec2 ConicCornerDerivative(float T, float E, float W)
{
	float B0 = (1.0 - T) * (1.0 - T);
	float B1 = 2.0 * W * T * (1.0 - T);
	float B2 = T * T;
	float D0 = -2.0 * (1.0 - T);
	float D1 = 2.0 * W * (1.0 - 2.0 * T);
	float D2 = 2.0 * T;
	float Den = B0 + B1 + B2;
	float DDen = D0 + D1 + D2;
	vec2 Num = vec2((B0 + B1) * E, (B1 + B2) * E);
	vec2 DNum = vec2((D0 + D1) * E, (D1 + D2) * E);
	return (DNum * Den - Num * DDen) / (Den * Den);
}

// 到圆锥曲线角的近似带符号距离：负值在形状内部（离角点更近的一侧）。
float ConicCornerDistance(vec2 P, float E, float W)
{
	float Best = 1e9;
	float BestT = 0.5;
	for(int i = 0; i <= 4; i++)
	{
		float T = float(i) / 4.0;
		float Dist = length(ConicCornerPoint(T, E, W) - P);
		if(Dist < Best)
		{
			Best = Dist;
			BestT = T;
		}
	}
	float T = BestT;
	for(int i = 0; i < 4; i++)
	{
		vec2 Q = ConicCornerPoint(T, E, W);
		vec2 D = ConicCornerDerivative(T, E, W);
		T = clamp(T - dot(Q - P, D) / max(dot(D, D), 0.000001), 0.0, 1.0);
	}
	vec2 Q = ConicCornerPoint(T, E, W);
	float Dist = length(Q - P);
	// Corner 坐标离角点越近越靠内：比曲线更近角点 = 形状内部 = 负值。
	return dot(P - Q, P) > 0.0 ? Dist : -Dist;
}

// 连续曲率的代价是曲率峰值升高：90° 转弯、端点曲率归零、圆角长度不变时，峰值
// 必然高于圆弧（超椭圆满强度能到 2.5 倍）。所以圆角沿边拉长来补偿，拉长量受有效
// 半径上限约束，曲线强度按「实际拿到多少生长」反解——扩不动时（胶囊、短边很近的
// 小控件）自动退回圆弧，不会被强行压弯。
// x = 实际生效的圆角半径（含沿边拉长），y = 实际生效的曲线强度。
vec2 RoundedCornerShape(vec2 HalfSize, float Radius, float Smoothing, float CurveType, float SeamAmount)
{
	// 原圆弧档就是旧外观：不扩展、也不换曲线。
	float Amount = CurveType > 0.5 ? clamp(Smoothing, 0.0, 1.0) : 0.0;
	float MinHalf = max(min(HalfSize.x, HalfSize.y), 0.0001);
	float ClampedRadius = clamp(Radius, 0.0, MinHalf);
	// 苹果胶囊即使全局曲线档是「原圆弧」也要生效，所以这里给接缝参数留一个例外。
	if(ClampedRadius <= 0.0 || (Amount <= 0.0 && SeamAmount <= 0.9))
		return vec2(ClampedRadius, 0.0);
	// 只有「真的有一段直边」的胶囊才谈得上接缝：正圆（滑块圆点、单选按钮）四个角
	// 本来就是连续的，不该被压成方圆，所以再要求长边明显长于短边。
	// 判据与 CPU 侧 ResolveRoundedCornerShape 必须逐字一致，否则两边生长上限不同。
	bool Capsule = ClampedRadius >= MinHalf * (9.0 / 10.0) && max(HalfSize.x, HalfSize.y) > MinHalf * 1.2;
	// 生长上限：普通控件封在短边的 25%，已经是胶囊的元素放宽到短边的 40%；
	// 这里用半边长表示，所以系数是 0.5 / 0.8。上限只限制生长，不回收更大的半径。
	float MaxRadius = max(MinHalf * (Capsule ? 0.8 : 0.5), ClampedRadius);
	float EffectiveRadius = clamp(ClampedRadius * (1.0 + 1.3 * Amount), ClampedRadius, MaxRadius);
	float EffectiveAmount = clamp((EffectiveRadius / ClampedRadius - 1.0) / 1.3, 0.0, 1.0);
	// 苹果连续曲率自己负责沿边外扩（足迹 = r*(1 + 0.528665*s)），半径不再按 1.3 倍生长，
	// 强度直接取平滑度；否则会和曲线自身的扩边叠加，圆角被放大两次。
	if(int(CurveType + 0.5) == 4)
	{
		EffectiveRadius = ClampedRadius;
		// 足迹必须塞进短边半高，塞不下就按短边等比削这一个标量：分轴各钳一次就是把端帽
		// 拉成橄榄头，上下两个角的肩部还会互相穿过中线，端边挤出外凸的唇边。
		float MaxAmount = max(MinHalf / ClampedRadius - 1.0, 0.0) / 0.528665;
		EffectiveAmount = clamp(min(clamp(Smoothing, 0.0, 1.0), MaxAmount), 0.0, 1.0);
	}
	// 胶囊端帽由端头构造单独负责（正圆 / 丝滑肩部 / 苹果端帽，见 RoundedRectSdf），
	// 圆角曲线不参与：半径已经贴到短边一半，任何沿边外扩都会穿过中线。
	// 几何兜底因此按正圆胶囊生成，与端头构造最多差 0.021 r。
	if(Capsule)
		EffectiveAmount = 0.0;
	return vec2(EffectiveRadius, EffectiveAmount);
}

// 五次贝塞尔圆角：控制点 P0=(1,0)、P1=(1,y1)、P2=(x2,y2)、P3=(y2,x2)、
// P4=(y1,1)、P5=(0,1)。前两个控制点都落在 x=1 上，端点切向沿直边、曲率恒为 0，
// 这就是「真连续曲率」的来源。两组取值之间按强度插值：0 取曲率匹配的三次圆弧
// 升阶结果（几乎就是圆弧），1 取拟合圆锥 W=1.2 手感的那组（外形偏差 0.6%）。
// 一次返回点与切向量：牛顿迭代每轮两样都要，分开写会把伯恩斯坦求和做两遍。
vec4 QuinticCorner(float T, float E, float Amount)
{
	float Y1 = mix(0.3291504, 0.14, Amount);
	float X2 = mix(0.8645747, 1.0, Amount);
	float Y2 = mix(0.6291504, 0.80, Amount);
	float U = 1.0 - T;
	float U2 = U * U;
	float T2 = T * T;
	float B0 = U2 * U2 * U;
	float B1 = 5.0 * T * U2 * U2;
	float B2 = 10.0 * T2 * U2 * U;
	float B3 = 10.0 * T2 * T * U2;
	float B4 = 5.0 * T2 * T2 * U;
	float B5 = T2 * T2 * T;
	vec2 Point = B0 * vec2(1.0, 0.0) + B1 * vec2(1.0, Y1) + B2 * vec2(X2, Y2) + B3 * vec2(Y2, X2) + B4 * vec2(Y1, 1.0) + B5 * vec2(0.0, 1.0);
	float D0 = 5.0 * U2 * U2;
	float D1 = 20.0 * T * U2 * U;
	float D2 = 30.0 * T2 * U2;
	float D3 = 20.0 * T2 * T * U;
	float D4 = 5.0 * T2 * T2;
	vec2 Derivative = D0 * vec2(0.0, Y1) + D1 * (vec2(X2, Y2) - vec2(1.0, Y1)) + D2 * (vec2(Y2, X2) - vec2(X2, Y2)) + D3 * (vec2(Y1, 1.0) - vec2(Y2, X2)) + D4 * (vec2(0.0, 1.0) - vec2(Y1, 1.0));
	return vec4(Point, Derivative) * E;
}

float QuinticCornerDistance(vec2 P, float E, float Amount)
{
	float Best = 1e9;
	float BestT = 0.5;
	for(int i = 0; i <= 5; i++)
	{
		float T = float(i) / 5.0;
		float Dist = length(QuinticCorner(T, E, Amount).xy - P);
		if(Dist < Best)
		{
			Best = Dist;
			BestT = T;
		}
	}
	float T = BestT;
	for(int i = 0; i < 4; i++)
	{
		vec4 Node = QuinticCorner(T, E, Amount);
		T = clamp(T - dot(Node.xy - P, Node.zw) / max(dot(Node.zw, Node.zw), 0.000001), 0.0, 1.0);
	}
	vec2 Q = QuinticCorner(T, E, Amount).xy;
	float Dist = length(Q - P);
	// Corner 坐标离角点越近越靠内：比曲线更近角点 = 形状内部 = 负值。
	return dot(P - Q, P) > 0.0 ? Dist : -Dist;
}
// 苹果连续曲率圆角的控制点：P0 是起点，之后每段两个手柄加一个段末点，共 10 个。
// 前 5 个独立，P5..P9 关于对角线镜像（P[9-i] = P[i].yx）。
// [0] = 三段 30° 圆弧（手柄 k = 4/3·tan(7.5°)），[1] = UIBezierPath(roundedRect:) 实测值
// （与 CoreAnimation 逐像素一致：足迹 1.528665 r，最贴角点 0.41225 r）。
// 坐标：角点在原点、直边沿 +x/+y，曲线从 (足迹, 0) 走到 (0, 足迹)。
vec2 AppleCornerControlPoint(int Index, float S)
{
	int Mirrored = Index > 4 ? 9 - Index : Index;
	vec2 Arc;
	vec2 Continuous;
	if(Mirrored == 0)
	{
		Arc = vec2(1.000000, 0.000000);
		Continuous = vec2(1.528665, 0.000000);
	}
	else if(Mirrored == 1)
	{
		Arc = vec2(0.824462, 0.000000);
		Continuous = vec2(1.088493, 0.000000);
	}
	else if(Mirrored == 2)
	{
		Arc = vec2(0.652020, 0.046206);
		Continuous = vec2(0.868407, 0.000000);
	}
	else if(Mirrored == 3)
	{
		Arc = vec2(0.500000, 0.133975);
		Continuous = vec2(0.631494, 0.074911);
	}
	else
	{
		Arc = vec2(0.347980, 0.221744);
		Continuous = vec2(0.372824, 0.169060);
	}
	vec2 Point = mix(Arc, Continuous, S);
	return Index > 4 ? Point.yx : Point;
}

// 苹果连续曲率：三段首尾相连的三次贝塞尔（T 三等分），同时给出对 T 的导数（牛顿要用）。
// 强度只在「三段圆弧」与实测曲线之间逐点插值，两个轴共用同一个标量：分轴各算一个系数
// 会把端帽在短轴上压扁、长轴上拉长（1.53:1 的橄榄头），端头外凸就是这么来的。
// 结果转回调用方坐标（圆弧中心为原点、从 (1,0) 到 (0,1)）：x = 1 - y_apple、y = 1 - x_apple。
vec4 AppleCorner(float T, float E, float S)
{
	int Segment = T < 0.3333333 ? 0 : (T < 0.6666667 ? 1 : 2);
	float U = clamp(T * 3.0 - float(Segment), 0.0, 1.0);
	vec2 P0 = AppleCornerControlPoint(Segment * 3 + 0, S);
	vec2 P1 = AppleCornerControlPoint(Segment * 3 + 1, S);
	vec2 P2 = AppleCornerControlPoint(Segment * 3 + 2, S);
	vec2 P3 = AppleCornerControlPoint(Segment * 3 + 3, S);
	float V = 1.0 - U;
	vec2 Point = V * V * V * P0 + 3.0 * V * V * U * P1 + 3.0 * V * U * U * P2 + U * U * U * P3;
	vec2 Derivative = 3.0 * (V * V * (P1 - P0) + 2.0 * V * U * (P2 - P1) + U * U * (P3 - P2)) * 3.0;
	return vec4(1.0 - Point.y, 1.0 - Point.x, -Derivative.y, -Derivative.x) * E;
}

float AppleCornerDistance(vec2 P, float E, float S)
{
	float Best = 1e9;
	float BestT = 0.5;
	for(int i = 0; i <= 7; i++)
	{
		float T = float(i) / 7.0;
		float Dist = length(AppleCorner(T, E, S).xy - P);
		if(Dist < Best)
		{
			Best = Dist;
			BestT = T;
		}
	}
	float T = BestT;
	for(int i = 0; i < 5; i++)
	{
		vec4 Node = AppleCorner(T, E, S);
		T = clamp(T - dot(Node.xy - P, Node.zw) / max(dot(Node.zw, Node.zw), 0.000001), 0.0, 1.0);
	}
	vec2 Q = AppleCorner(T, E, S).xy;
	float Dist = length(Q - P);
	// 内外判定用「半径比较」而不是角点方向点积：这条曲线关于角点是星形的，
	// 但角盒外扩以后会包含直边内侧的点，用点积会把整块内侧误判成外部。
	return length(P) > length(Q) ? Dist : -Dist;
}

// 丝滑胶囊（苹果那种连续曲率端头）：端帽仍是精确正圆，只把直边靠近端帽的一段按回旋线
// 把曲率从 0 升到 1/R。曲率单调上升、边界单调内收，所以横条会提前微微弯向端帽，
// 接缝处没有曲率突变，也不会出现「鼓一下再退回来」的起伏。
// 肩部宽度 R*Span、端部收窄 R*Drop 都由 φ 决定；幂次 Power 由「末端曲率恰好等于 1/R」解出。
float SilkyCapsuleSdf(vec2 Point, vec2 HalfSize, float Radius)
{
	const float Drop = 0.04894348;   // 1 - cos(18°)
	const float Span = 0.32669;      // 肩部在长轴方向占多少倍半径（由 φ 与幂次决定）
	const float Power = 2.16882;     // 让末端曲率正好等于 1/R 的幂次
	bool Horizontal = HalfSize.x >= HalfSize.y;
	float LongHalf = Horizontal ? HalfSize.x : HalfSize.y;
	float Along = abs(Horizontal ? Point.x : Point.y);
	float Across = abs(Horizontal ? Point.y : Point.x);
	float R = Radius;
	// 端帽与直边的交界：正圆在 φ 处的点，之前用纯直边，之后用正圆。
	float Junction = LongHalf - R * (1.0 - 0.30901699);   // 1 - sin(18°)
	if(Along >= Junction)
		return length(vec2(Along - (LongHalf - R), Across)) - R;
	float Start = Junction - R * Span;
	if(Along <= Start)
		return Across - R;
	float U = (Along - Start) / (R * Span);
	float Edge = R * (1.0 - Drop * pow(U, Power));
	float Slope = Drop * Power * pow(U, Power - 1.0) / Span;
	return (Across - Edge) / sqrt(1.0 + Slope * Slope);
}

// 苹果连续胶囊：端帽永远是半径 = 短边一半的正圆（半径不缩、端帽不拉长），多出来的
// 长度全部体现在「直边提前起弯」的肩部。肩部从直边距切点 λ = 0.4154 R 处起弯，到端帽
// 34.24° 处接上，两端 G1；三次贝塞尔手柄 0.2591 R（直边侧）/ 0.3102 R（端帽侧）。
// 这组常数是对原生 iOS 胶囊做亚像素拟合的结果（残差 < 0.5 px），相对正圆胶囊的最大
// 内收 0.021 R，向外越界 ≈ 0。直边不够长时 λ 与 φ 一起等比收窄，方盒退化成正圆。
float AppleCapsuleSdf(vec2 Point, vec2 HalfSize, float Radius, float Strength)
{
	const float Shoulder = 0.4154;
	const float Phi0 = 0.5976;
	const float HandleEdge = 0.2591;
	const float HandleCap = 0.3102;
	bool Horizontal = HalfSize.x >= HalfSize.y;
	float LongHalf = Horizontal ? HalfSize.x : HalfSize.y;
	float Along = abs(Horizontal ? Point.x : Point.y);
	float Across = abs(Horizontal ? Point.y : Point.x);
	float R = Radius;
	float Center = LongHalf - R;
	// 强度按可用直边夹住：肩部再长也不能让两端的曲线连起来。
	float A = clamp(Strength, 0.0, 1.0);
	A = min(A, max(Center, 0.0) / max(Shoulder * R, 0.0001));
	if(A <= 0.001)
		return length(vec2(max(Along - Center, 0.0), Across)) - R;
	float Lambda = Shoulder * A * R;
	float Phi = Phi0 * A;
	float CapX = Center + R * sin(Phi);
	float CapY = R * cos(Phi);
	vec2 P0 = vec2(Center - Lambda, R);
	vec2 P1 = vec2(P0.x + HandleEdge * A * R, R);
	vec2 P3 = vec2(CapX, CapY);
	vec2 P2 = P3 - vec2(cos(Phi), -sin(Phi)) * (HandleCap * A * R);
	if(Along >= CapX)
		return length(vec2(Along - Center, Across)) - R;
	if(Along <= P0.x)
		return Across - R;
	// 高度场：曲线沿长轴单调，先反解参数，再按切线斜率把竖向差换成垂直距离。
	float U = clamp((Along - P0.x) / max(P3.x - P0.x, 0.0001), 0.0, 1.0);
	for(int i = 0; i < 3; i++)
	{
		float V = 1.0 - U;
		float X = V * V * V * P0.x + 3.0 * V * V * U * P1.x + 3.0 * V * U * U * P2.x + U * U * U * P3.x;
		float DX = 3.0 * (V * V * (P1.x - P0.x) + 2.0 * V * U * (P2.x - P1.x) + U * U * (P3.x - P2.x));
		U = clamp(U - (X - Along) / (abs(DX) > 0.0001 ? DX : 0.0001), 0.0, 1.0);
	}
	float V = 1.0 - U;
	vec2 Q = V * V * V * P0 + 3.0 * V * V * U * P1 + 3.0 * V * U * U * P2 + U * U * U * P3;
	vec2 D = 3.0 * (V * V * (P1 - P0) + 2.0 * V * U * (P2 - P1) + U * U * (P3 - P2));
	float Slope = D.y / (abs(D.x) > 0.0001 ? D.x : 0.0001);
	return (Across - Q.y) / sqrt(1.0 + Slope * Slope);
}

float RoundedRectSdf(vec2 Point, vec2 HalfSize, float Radius, float Smoothing, float CurveType, float SeamAmount)
{
	vec2 Shape = RoundedCornerShape(HalfSize, Radius, Smoothing, CurveType, SeamAmount);
	float EffectiveRadius = Shape.x;
	float Amount = Shape.y;
	vec2 DistanceToInner = abs(Point) - HalfSize + EffectiveRadius;
	vec2 Corner = max(DistanceToInner, vec2(0.0));
	float Flat = min(max(DistanceToInner.x, DistanceToInner.y), 0.0);
	// 胶囊端头分两套，都要求「真的有一段直边」，正圆（滑块圆点、单选按钮）不参与：
	// 丝滑档只改直边（端帽仍是正圆 + 回旋线肩部），苹果档整块端头单独构造。
	float MinHalf = min(HalfSize.x, HalfSize.y);
	bool SeamCapsule = SeamAmount > 0.001 && EffectiveRadius >= MinHalf * 0.9 && max(HalfSize.x, HalfSize.y) > MinHalf * 1.2;
	if(SeamCapsule)
	{
		if(SeamAmount > 0.9)
			return AppleCapsuleSdf(Point, HalfSize, EffectiveRadius, clamp(SeamAmount - 1.0, 0.0, 1.0));
		return SilkyCapsuleSdf(Point, HalfSize, EffectiveRadius);
	}
	// 曲线档位：1 = 超椭圆（p 范数），2 = 五次贝塞尔，3 = 圆锥贝塞尔，4 = 苹果三段式。
	int Type = int(CurveType + 0.5);
	// 苹果连续圆角的曲线会沿两条直边各外扩 Ext，所以角区判定也要跟着外扩，
	// 否则那一段会被当成直边，曲线在直边上被直接切断。
	// 注意必须同时要求「至少有一个分量在角盒内」：两个分量都为负说明在直边内侧，
	// 那里最近的边界是直边本身，交给下面的早退分支；否则会拿角点做符号参照，
	// 把一块矩形内部误判成外部，四角出现方块。
	// 外扩量已由 RoundedCornerShape 按短边夹住（足迹 ≤ 短边半高），所以不会撞到中线。
	float AppleExt = Type == 4 ? EffectiveRadius * 0.528665 * Amount : 0.0;
	if(Type == 4 && Amount > 0.001 && DistanceToInner.x > -AppleExt && DistanceToInner.y > -AppleExt &&
		(DistanceToInner.x > 0.0 || DistanceToInner.y > 0.0))
		return AppleCornerDistance(DistanceToInner, EffectiveRadius, Amount);
	// 只有两个分量都为正才是圆角区；边上和内部直接用圆的距离，
	// 否则 p 范数在两个分量都为 0 时会被归一化除成极大值，整块内部被误判成外部。
	if(Amount <= 0.001 || Corner.x <= 0.0 || Corner.y <= 0.0)
		return length(Corner) + Flat - EffectiveRadius;
	if(Type == 2)
		return QuinticCornerDistance(Corner, EffectiveRadius, Amount);
	if(Type == 3)
		return ConicCornerDistance(Corner, EffectiveRadius, mix(0.70710678, 1.2, Amount));
	float Exponent = mix(2.0, 4.0, Amount);
	float PowX = pow(Corner.x, Exponent);
	float PowY = pow(Corner.y, Exponent);
	float Sum = max(PowX + PowY, 0.0000001);
	// 角区里 Flat 恒为 0，形状值就是 p 范数减半径：曲率在直边处为 0，向角顶连续升高。
	float Norm = pow(Sum, 1.0 / Exponent);
	float ShapeValue = Norm - EffectiveRadius;
	// p 范数的梯度不是单位长度，要把「整份形状值」除以梯度模长才换成近似距离，
	// 零等值线因此完全不动。只缩放范数、把半径留在缩放外面，角顶有效半径会被拉小，
	// 画出来就是凹进去的缺口。
	float Scale = pow(Sum, 1.0 / Exponent - 1.0);
	float GradientX = PowX / max(Corner.x, 0.0000001) * Scale;
	float GradientY = PowY / max(Corner.y, 0.0000001) * Scale;
	float GradientLength = max(length(vec2(GradientX, GradientY)), 0.0001);
	return ShapeValue / GradientLength;
}
float CornerRadius(vec2 Point, vec4 CornerRadii)
{
	if(Point.y < 0.0)
		return Point.x < 0.0 ? CornerRadii.x : CornerRadii.y;
	return Point.x < 0.0 ? CornerRadii.w : CornerRadii.z;
}

float SdfFeather(float DistanceValue, float PixelSize)
{
	return max(PixelSize, length(vec2(dFdx(DistanceValue), dFdy(DistanceValue))));
}

float Coverage(float DistanceValue, float PixelSize)
{
	float Feather = SdfFeather(DistanceValue, PixelSize);
	return 1.0 - smoothstep(-Feather * 0.5, Feather * 0.5, DistanceValue);
}

// 渐变语义与 CPU 端 SQmColorGradient::Position/Sample 保持一致。
float GradientAmount(vec2 UV, vec4 Params0, vec4 Params1)
{
	vec2 Offset = UV - Params0.yz;
	float Along = dot(Offset, Params1.xy);
	float Across = dot(Offset, vec2(-Params1.y, Params1.x));
	float Extent = abs(Params1.x) + abs(Params1.y);
	float Range = max(Params0.w, 0.0001);
	float Amount = 0.0;
	int Type = int(Params0.x + 0.5);
	if(Type == 0)
		Amount = 0.5 + Along / max(Range * Extent, 0.0001);
	else if(Type == 1)
		Amount = length(Offset) * 2.0 / Range;
	else if(Type == 2)
	{
		if(length(Offset) > 0.00001)
		{
			Amount = atan(Across, Along) / (2.0 * 3.14159265359);
			Amount -= floor(Amount);
			Amount /= Range;
		}
	}
	else if(Type == 3)
		Amount = abs(Along) * 2.0 / max(Range * Extent, 0.0001);
	else
		Amount = (abs(Along) + abs(Across)) * 2.0 / Range;
	Amount = clamp(Amount, 0.0, 1.0);
	return Params1.z > 0.5 ? 1.0 - Amount : Amount;
}

vec4 GradientColor(vec2 UV, vec4 SolidColor)
{
	vec4 Params0 = gSdf.gRoundedRectSdfData[12];
	vec4 Params1 = gSdf.gRoundedRectSdfData[13];
	vec4 Params2 = gSdf.gRoundedRectSdfData[14];
	if(Params2.x < 0.5)
		return SolidColor;
	int Count = clamp(int(Params1.w + 0.5), 1, 7);
	if(Count <= 1)
		return gSdf.gRoundedRectSdfData[5];
	float Amount = GradientAmount(UV, Params0, Params1);
	float Scaled = Amount * float(Count - 1);
	int Index = min(int(floor(Scaled)), Count - 2);
	float T = Scaled - float(Index);
	vec4 From = gSdf.gRoundedRectSdfData[5 + Index];
	vec4 To = gSdf.gRoundedRectSdfData[5 + Index + 1];
	return mix(From, To, T);
}

void main()
{
	vec4 Rect = gSdf.gRoundedRectSdfData[0];
	vec4 BorderColor = gSdf.gRoundedRectSdfData[2];
	vec4 CornerRadii = gSdf.gRoundedRectSdfData[3];
	vec4 Params = gSdf.gRoundedRectSdfData[4];
	float CornerSmoothing = clamp(gSdf.gRoundedRectSdfData[14].w, 0.0, 1.0);
	// 圆角曲线档位：0 = 原圆弧，1 = 超椭圆，2 = 五次贝塞尔，3 = 圆锥贝塞尔。
	// 档位在 [17].x、胶囊接缝软化在 [17].y；Params.w 仍然是「边框取渐变」开关。
	float CornerCurveType = gSdf.gRoundedRectSdfData[17].x;
	// 1.0 以上表示苹果胶囊，小数部分是它自己的强度（不与上方圆角曲线共用）。
	float CornerSeam = clamp(gSdf.gRoundedRectSdfData[17].y, 0.0, 2.0);
	// 形状外扩：四角形状按基础矩形算完再整体外扩，叠层之间因此保持等距。
	float ShapeExpand = clamp(gSdf.gRoundedRectSdfData[16].w, 0.0, 64.0);
	vec2 HalfSize = Rect.zw * 0.5;
	vec2 Point = (texCoord - vec2(0.5)) * (Rect.zw + vec2(Params.z * 2.0));
	float BorderWidth = clamp(Params.x, 0.0, min(HalfSize.x, HalfSize.y));
	// 渐变坐标必须和几何回退一致：都以边框内缩后的矩形为 0..1 范围。
	vec2 InnerHalfSize = HalfSize - vec2(BorderWidth);
	vec2 GradientExtent = max(InnerHalfSize, vec2(0.0));
	vec2 GradientUV = clamp((Point + GradientExtent) / max(GradientExtent * 2.0, vec2(0.0001)), vec2(0.0), vec2(1.0));
	vec4 FillColor = GradientColor(GradientUV, gSdf.gRoundedRectSdfData[1]);
	float GrainStrength = gSdf.gRoundedRectSdfData[17].z;
	if(GrainStrength > 0.0)
	{
		uvec2 Pixel = uvec2(max(floor((Point + HalfSize) / max(Params.y, 0.0001)), vec2(0.0)));
		uint Seed = Pixel.x * 1597334677u ^ Pixel.y * 3812015801u;
		Seed = (Seed ^ (Seed >> 16u)) * 2246822519u;
		float Grain = float((Seed ^ (Seed >> 13u)) & 255u) / 255.0 - 0.5;
		FillColor.rgb = clamp(FillColor.rgb + vec3(Grain * GrainStrength), vec3(0.0), vec3(1.0));
	}
	vec2 BaseHalfSize = max(HalfSize - vec2(ShapeExpand), vec2(0.5));
	float Radius = clamp(CornerRadius(Point, CornerRadii), 0.0, min(BaseHalfSize.x, BaseHalfSize.y));
	float OuterDistance = RoundedRectSdf(Point, BaseHalfSize, Radius, CornerSmoothing, CornerCurveType, CornerSeam) - ShapeExpand;
	float OuterCoverage = Coverage(OuterDistance, Params.y);
	// 内缘是同一形状向内偏移一个边框宽度：换成连续曲率后「半径减边框」不再是等距内缩，
	// 会让环带宽度不均、和外层错位，所以直接从同一份距离场加偏移。
	float InnerDistance = OuterDistance + BorderWidth;
	float InnerCoverage = BorderWidth > 0.0 && min(InnerHalfSize.x, InnerHalfSize.y) > 0.0 ? Coverage(InnerDistance, Params.y) : 0.0;
	// 填充覆盖率：有边框时以环带内缘为界，无边框时整块形状都是填充。
	float FillCoverage = BorderWidth > 0.0 ? InnerCoverage : OuterCoverage;
#ifdef TW_ROUNDED_SDF_TEXTURED
	// 圆角容器放图：纹理取代纯色填充，并共用同一个圆角覆盖度，四角因此被真实
	// 切掉。放大/平移后图片铺满矩形也不再露出方角，因为方角本身已经不画了。
	// UV 是「逻辑点 → 图片 UV」的仿射，缩放与平移由调用方折算进 Params[15]/[16]；
	// Params[16].y 是整张照片的透明度（卡片淡出时照片和边框一起消失）。
	vec4 TexParams0 = gSdf.gRoundedRectSdfData[15];
	vec4 TexParams1 = gSdf.gRoundedRectSdfData[16];
	if(TexParams0.x > 0.5)
	{
		// 图片矩形之外保持透明：图片没铺满容器时，边缘和普通 quad 一样是硬边。
		vec2 TexUV = Point * TexParams0.yz + vec2(TexParams0.w, TexParams1.x);
		// UV 边界两侧的片元会走不同分支，在判断范围前计算采样梯度。
		vec2 TexDx = dFdx(TexUV);
		vec2 TexDy = dFdy(TexUV);
		vec4 TexColor = vec4(0.0);
		if(all(greaterThanEqual(TexUV, vec2(0.0))) && all(lessThanEqual(TexUV, vec2(1.0))))
			TexColor = textureGrad(gTextureSampler, TexUV, TexDx, TexDy);
		if(TexParams0.x > 1.5)
		{
			// 距离场只存轮廓，颜色由调用方提供；按屏幕像素恢复覆盖率，缩放不重建纹理。
			vec2 UVWidth = max(fwidth(TexUV), vec2(0.000001));
			float ScreenRange = max(0.5 * dot(vec2(TexParams1.z), vec2(1.0) / UVWidth), 1.0);
			float IconCoverage = clamp((TexColor.r - 0.5) * ScreenRange + 0.5, 0.0, 1.0);
			TexColor = vec4(FillColor.rgb, FillColor.a * IconCoverage);
		}
		TexColor.a *= clamp(TexParams1.y, 0.0, 1.0);
		FillColor = TexColor;
		FillCoverage = OuterCoverage;
	}
#endif
	if(BorderWidth <= 0.0)
	{
		FragClr = vec4(FillColor.rgb, FillColor.a * FillCoverage);
		return;
	}
	float BorderCoverage = max(OuterCoverage - InnerCoverage, 0.0);
	// GradientParams2.z > 0 时按带宽做指数羽化：边框从内缘到外缘逐渐透明，
	// 才有多层辉光叠加的"晕"感。等于 0 时保持原有的实心边框。
	float BorderFalloff = gSdf.gRoundedRectSdfData[14].z;
	if(BorderFalloff > 0.0 && BorderWidth > 0.0)
	{
		// 环带内缘 InnerDistance 约 0，向外缘增至 +BorderWidth；
		// BandT 因此从内缘 0 到外缘 1，按幂次衰减成向外淡出的光晕。
		float BandT = clamp(InnerDistance / max(BorderWidth, 0.0001), 0.0, 1.0);
		BorderCoverage *= pow(max(1.0 - BandT, 0.0), BorderFalloff);
	}
	// GradientParams2.y 是"只描边"开关：渐变只服务于边框，填充整体丢弃。
	// 彩虹光环这类描边效果必须走这条路，否则渐变会把整张卡片染成彩色。
	bool BorderOnly = gSdf.gRoundedRectSdfData[14].y > 0.5;
	if(BorderOnly)
		InnerCoverage = 0.0;
	float FillAlphaMask = BorderOnly ? 0.0 : 1.0;
	// Params.w 作为"边框取渐变"开关（默认 0 保持既有单色边框行为）：
	// 描边类效果（彩虹光环）需要边框本身沿角度取色，而单色 BorderColor 做不到。
	vec4 BorderTint = BorderColor;
	if(Params.w > 0.5)
	{
		// 渐变只提供色相，描边强弱仍由调用方给的 BorderColor.a 决定；
		// 直接沿用渐变的 alpha 会让三层光晕强度全变成 1。
		// 角度渐变直接用相对几何中心的 Point：它是等比例逻辑单位，atan 得到真实
		// 角度。用 0..1 归一化 UV 会把长条卡片的角度压扁成横向色带（4:1 时压 4 倍）。
		vec2 PolarUV = vec2(Point.x / max(Rect.z, 0.0001) + 0.5, Point.y / max(Rect.z, 0.0001) + 0.5);
		vec4 GradientBorder = GradientColor(PolarUV, BorderColor);
		GradientBorder.a = BorderColor.a;
		BorderTint = GradientBorder;
	}
	float BorderAlpha = BorderTint.a * BorderCoverage;
	float FillAlpha = FillColor.a * FillCoverage * FillAlphaMask;
	float OutputAlpha = FillAlpha + BorderAlpha;
	vec3 Premultiplied = FillColor.rgb * FillAlpha + BorderTint.rgb * BorderAlpha;
	FragClr = OutputAlpha > 0.0 ? vec4(Premultiplied / OutputAlpha, OutputAlpha) : vec4(0.0);
}
