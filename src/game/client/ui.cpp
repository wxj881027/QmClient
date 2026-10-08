/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "ui.h"

#include "QmUi/QmDropdown.h"
#include "QmUi/QmUiPerf.h"
#include "QmUi/UiSurface.h"
#include "QmUi/UiSurfaceText.h"
#include "components/qmclient/perf_logging.h"
#include "qm_icon.h"
#include "qm_icon_font_render.h"
#include "qm_icon_label.h"
#include "ui_scrollregion.h"

#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>

#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace FontIcons;

void CUIElement::Init(CUi *pUI, int RequestedRectCount)
{
	dbg_assert(m_pUI == nullptr, "UI element can only be registered once.");
	m_pUI = pUI;
	pUI->AddUIElement(this);
	if(RequestedRectCount > 0 && !AreRectsInit())
		InitRects(RequestedRectCount);
	dbg_assert(RequestedRectCount <= 0 || m_vUIRects.size() == (size_t)RequestedRectCount, "UI element rect count changed.");
}

CUIElement::~CUIElement()
{
	if(m_pUI != nullptr)
		m_pUI->RemoveUIElement(this);
}

void CUIElement::InitRects(int RequestedRectCount)
{
	dbg_assert(m_vUIRects.empty(), "UI rects can only be initialized once, create another ui element instead.");
	m_vUIRects.resize(RequestedRectCount);
	for(auto &Rect : m_vUIRects)
		Rect.m_pParent = this;
}

CUIElement::SUIElementRect::SUIElementRect() { Reset(); }

CUiScopedQuadBatch::CUiScopedQuadBatch(CUi *pUi) :
	m_pUi(pUi)
{
	if(m_pUi != nullptr)
		m_pUi->BeginQuadBatch();
}

CUiScopedQuadBatch::~CUiScopedQuadBatch()
{
	if(m_pUi != nullptr)
		m_pUi->EndQuadBatch();
}

CUiScopedGaussianBlur::CUiScopedGaussianBlur(CUi *pUi, float Alpha) :
	m_pUi(pUi)
{
	if(m_pUi != nullptr)
		m_pUi->BeginGaussianBlurScope(Alpha);
}

CUiScopedGaussianBlur::~CUiScopedGaussianBlur()
{
	if(m_pUi != nullptr)
		m_pUi->EndGaussianBlurScope();
}

CUiScopedGaussianBlurSuppression::CUiScopedGaussianBlurSuppression(CUi *pUi) :
	CUiScopedGaussianBlurSuppression(pUi, true)
{
}

CUiScopedGaussianBlurSuppression::CUiScopedGaussianBlurSuppression(CUi *pUi, bool Active) :
	m_pUi(pUi),
	m_Active(Active)
{
	if(m_pUi != nullptr && m_Active)
		m_pUi->BeginGaussianBlurSuppression();
}

CUiScopedGaussianBlurSuppression::~CUiScopedGaussianBlurSuppression()
{
	if(m_pUi != nullptr && m_Active)
		m_pUi->EndGaussianBlurSuppression();
}

void CUIElement::SUIElementRect::Reset()
{
	m_UIRectQuadContainer = -1;
	m_UITextContainer.Reset();
	m_X = -1;
	m_Y = -1;
	m_Width = -1;
	m_Height = -1;
	m_Rounding = -1.0f;
	m_Corners = -1;
	m_BackgroundAlphaScale = 1.0f;
	m_Text.clear();
	m_FontSize = -1.0f;
	m_TextAlign = -1;
	m_LabelMaxWidth = -2.0f;
	m_LabelFlags = -1;
	m_LineCount = 0;
	m_BiggestCharacterHeight = 0.0f;
	m_Cursor = CTextCursor();
	m_TextColor = ColorRGBA(-1, -1, -1, -1);
	m_TextOutlineColor = ColorRGBA(-1, -1, -1, -1);
	m_QuadColor = ColorRGBA(-1, -1, -1, -1);
	m_ReadCursorGlyphCount = -1;
	m_aQmIcons.fill(EQmIcon::COUNT);
	m_NumQmIcons = 0;
	m_FontPreset = EFontPreset::DEFAULT_FONT;
}

void CUIElement::SUIElementRect::Draw(const CUIRect *pRect, ColorRGBA Color, int Corners, float Rounding)
{
	bool NeedsRecreate = false;
	if(m_UIRectQuadContainer == -1 || m_Width != pRect->w || m_Height != pRect->h || m_QuadColor != Color || m_Rounding != Rounding || m_Corners != Corners)
	{
		m_pParent->Ui()->Graphics()->DeleteQuadContainer(m_UIRectQuadContainer);
		NeedsRecreate = true;
	}
	m_X = pRect->x;
	m_Y = pRect->y;
	if(NeedsRecreate)
	{
		m_Width = pRect->w;
		m_Height = pRect->h;
		m_Rounding = Rounding;
		m_Corners = Corners;
		m_QuadColor = Color;

		m_pParent->Ui()->Graphics()->SetColor(Color);
		m_UIRectQuadContainer = m_pParent->Ui()->Graphics()->CreateRectQuadContainer(0, 0, pRect->w, pRect->h, Rounding, Corners);
		m_pParent->Ui()->Graphics()->SetColor(1, 1, 1, 1);
	}

	if(Color.a > 0.0f && Color.a < 1.0f && m_pParent->Ui()->GaussianBlurScopeActive())
		m_pParent->Ui()->RenderGaussianBlur(*pRect, m_pParent->Ui()->GaussianBlurScopeAlpha(), Corners, Rounding);
	m_pParent->Ui()->Graphics()->TextureClear();
	m_pParent->Ui()->Graphics()->RenderQuadContainerEx(m_UIRectQuadContainer,
		0, -1, m_X, m_Y, 1, 1);
}

void SLabelProperties::SetColor(const ColorRGBA &Color)
{
	m_vColorSplits.clear();
	m_vColorSplits.emplace_back(0, -1, Color);
}

/********************************************************
 UI
*********************************************************/

const CLinearScrollbarScale CUi::ms_LinearScrollbarScale;
const CLogarithmicScrollbarScale CUi::ms_LogarithmicScrollbarScale(25);
const CDarkButtonColorFunction CUi::ms_DarkButtonColorFunction;
const CLightButtonColorFunction CUi::ms_LightButtonColorFunction;
const CScrollBarColorFunction CUi::ms_ScrollBarColorFunction;
const float CUi::ms_FontmodHeight = 0.8f;

CUi *CUIElementBase::ms_pUi = nullptr;

IClient *CUIElementBase::Client() const { return ms_pUi->Client(); }
IGraphics *CUIElementBase::Graphics() const { return ms_pUi->Graphics(); }
IInput *CUIElementBase::Input() const { return ms_pUi->Input(); }
ITextRender *CUIElementBase::TextRender() const { return ms_pUi->TextRender(); }

void CUi::Init(IKernel *pKernel)
{
	m_pClient = pKernel->RequestInterface<IClient>();
	m_pGraphics = pKernel->RequestInterface<IGraphics>();
	m_pInput = pKernel->RequestInterface<IInput>();
	m_pTextRender = pKernel->RequestInterface<ITextRender>();
	CUIRect::Init(m_pGraphics, this);
	CLineInput::Init(m_pClient, m_pGraphics, m_pInput, m_pTextRender);
	CUIElementBase::Init(this);
}

CUi::CUi()
{
	m_Enabled = true;

	m_Screen.x = 0.0f;
	m_Screen.y = 0.0f;
}

CUi::~CUi()
{
	for(CUIElement *pEl : m_vpUIElements)
	{
		if(pEl != nullptr)
			pEl->m_pUI = nullptr;
	}
	m_vpUIElements.clear();

	for(CUIElement *&pEl : m_vpOwnUIElements)
	{
		delete pEl;
	}
	m_vpOwnUIElements.clear();
}

void CUi::OnShutdown()
{
	if(m_pGraphics == nullptr || m_pTextRender == nullptr)
		return;
	OnElementsReset();
	m_pClient = nullptr;
	m_pGraphics = nullptr;
	m_pInput = nullptr;
	m_pTextRender = nullptr;
}

CUIElement *CUi::GetNewUIElement(int RequestedRectCount)
{
	CUIElement *pNewEl = new CUIElement(this, RequestedRectCount);

	m_vpOwnUIElements.push_back(pNewEl);

	return pNewEl;
}

void CUi::AddUIElement(CUIElement *pElement)
{
	m_vpUIElements.push_back(pElement);
}

void CUi::RemoveUIElement(CUIElement *pElement)
{
	m_vpUIElements.erase(std::remove(m_vpUIElements.begin(), m_vpUIElements.end(), pElement), m_vpUIElements.end());
	pElement->m_pUI = nullptr;
}

void CUi::ResetUIElement(CUIElement &UIElement) const
{
	for(CUIElement::SUIElementRect &Rect : UIElement.m_vUIRects)
	{
		Graphics()->DeleteQuadContainer(Rect.m_UIRectQuadContainer);
		TextRender()->DeleteTextContainer(Rect.m_UITextContainer);
		Rect.Reset();
	}
}

void CUi::OnElementsReset()
{
	for(auto &Label : m_vQmCachedIconLabels)
		TextRender()->DeleteTextContainer(Label.m_Container);
	m_vQmCachedIconLabels.clear();
	for(CUIElement *pEl : m_vpUIElements)
	{
		ResetUIElement(*pEl);
	}

	for(SQuadBatchRectContainer &Container : m_vQuadBatchRectContainers)
	{
		Graphics()->DeleteQuadContainer(Container.m_QuadContainerIndex);
		Container.m_QuadContainerIndex = -1;
	}
	m_vQuadBatchRectContainers.clear();
	m_vQuadBatchSprites.clear();
	m_QuadBatchContainerIndex = -1;
}

void CUi::OnWindowResize()
{
	DestroyGaussianBlurTargets();
	OnElementsReset();
}

void CUi::BeginGaussianBlurScope(float Alpha)
{
	m_vGaussianBlurScopeAlphas.push_back(std::clamp(Alpha, 0.0f, 1.0f));
	const bool HasTemporaryTarget = std::any_of(m_aGaussianBlurTemporary.begin(), m_aGaussianBlurTemporary.end(), [](const auto &Target) { return Target.IsValid(); });
	if(!g_Config.m_QmGaussianBlur && (m_GaussianBlurSource.IsValid() || HasTemporaryTarget || m_GaussianBlurTarget.IsValid()))
		DestroyGaussianBlurTargets();
}

void CUi::EndGaussianBlurScope()
{
	dbg_assert(!m_vGaussianBlurScopeAlphas.empty(), "gaussian blur scope underflow");
	if(!m_vGaussianBlurScopeAlphas.empty())
		m_vGaussianBlurScopeAlphas.pop_back();
}

void CUi::EndGaussianBlurSuppression()
{
	dbg_assert(m_GaussianBlurSuppressionDepth > 0, "gaussian blur suppression scope underflow");
	if(m_GaussianBlurSuppressionDepth > 0)
		--m_GaussianBlurSuppressionDepth;
}

void CUi::DestroyGaussianBlurTargets()
{
	m_GaussianBlurPrepared = false;
	if(m_pGraphics == nullptr)
		return;
	Graphics()->DestroyRenderTarget(&m_GaussianBlurSource);
	for(auto &Target : m_aGaussianBlurTemporary)
		Graphics()->DestroyRenderTarget(&Target);
	Graphics()->DestroyRenderTarget(&m_GaussianBlurTarget);
	m_GaussianBlurWidth = 0;
	m_GaussianBlurHeight = 0;
	m_GaussianBlurMode = -1;
}

bool CUi::PrepareGaussianBlur()
{
	if(!g_Config.m_QmGaussianBlur)
	{
		m_GaussianBlurPrepared = false;
		return false;
	}
	if(!Graphics()->IsBackbufferCaptureSupported() || !Graphics()->IsRenderTargetGaussianBlurSupported())
	{
		m_GaussianBlurPrepared = false;
		const bool HasTemporaryTarget = std::any_of(m_aGaussianBlurTemporary.begin(), m_aGaussianBlurTemporary.end(), [](const auto &Target) { return Target.IsValid(); });
		if(m_GaussianBlurSource.IsValid() || HasTemporaryTarget || m_GaussianBlurTarget.IsValid())
			DestroyGaussianBlurTargets();
		return false;
	}

	const int BlurWidth = UiGaussianBlurTargetDimension(Graphics()->ScreenWidth());
	const int BlurHeight = UiGaussianBlurTargetDimension(Graphics()->ScreenHeight());
	const int BlurMode = std::clamp(g_Config.m_QmBlurMode, 0, 2);
	const bool DualKawase = BlurMode == static_cast<int>(IGraphics::EBlurMode::DUAL);
	const int TemporaryCount = DualKawase ? IGraphics::DUAL_KAWASE_PYRAMID_LEVELS : 1;
	if(BlurWidth <= 0 || BlurHeight <= 0)
	{
		m_GaussianBlurPrepared = false;
		return false;
	}

	const bool TemporaryTargetsValid = std::all_of(m_aGaussianBlurTemporary.begin(), m_aGaussianBlurTemporary.begin() + TemporaryCount, [](const auto &Target) { return Target.IsValid(); });
	if(BlurWidth != m_GaussianBlurWidth || BlurHeight != m_GaussianBlurHeight || BlurMode != m_GaussianBlurMode || !m_GaussianBlurSource.IsValid() || !TemporaryTargetsValid || !m_GaussianBlurTarget.IsValid())
	{
		DestroyGaussianBlurTargets();
		m_GaussianBlurSource = Graphics()->CreateRenderTarget(BlurWidth, BlurHeight);
		for(int Level = 0; Level < TemporaryCount; ++Level)
		{
			const int TemporaryWidth = DualKawase ? IGraphics::DualKawasePyramidDimension(BlurWidth, Level) : BlurWidth;
			const int TemporaryHeight = DualKawase ? IGraphics::DualKawasePyramidDimension(BlurHeight, Level) : BlurHeight;
			m_aGaussianBlurTemporary[Level] = Graphics()->CreateRenderTarget(TemporaryWidth, TemporaryHeight);
		}
		m_GaussianBlurTarget = Graphics()->CreateRenderTarget(BlurWidth, BlurHeight);
		const bool CreatedTemporaryTargets = std::all_of(m_aGaussianBlurTemporary.begin(), m_aGaussianBlurTemporary.begin() + TemporaryCount, [](const auto &Target) { return Target.IsValid(); });
		if(!m_GaussianBlurSource.IsValid() || !CreatedTemporaryTargets || !m_GaussianBlurTarget.IsValid())
		{
			DestroyGaussianBlurTargets();
			return false;
		}
		m_GaussianBlurWidth = BlurWidth;
		m_GaussianBlurHeight = BlurHeight;
		m_GaussianBlurMode = BlurMode;
	}

	const uint64_t PerfFrame = Client()->PerfFrame();
	if(m_GaussianBlurPrepared && m_GaussianBlurPreparedFrame == PerfFrame)
		return true;
	// 同帧失败闩：GPU 侧失败在帧内不会自行恢复，后续矩形重试只会重复
	// FlushQuadBatch + 背板捕获 + 模糊提交的开销，直接跳过。
	if(m_GaussianBlurFailedFrame == PerfFrame)
		return false;

	FlushQuadBatch();
	Graphics()->FlushVertices();
	if(!Graphics()->CaptureBackbufferToRenderTarget(m_GaussianBlurSource))
	{
		m_GaussianBlurFailedFrame = PerfFrame;
		m_GaussianBlurPrepared = false;
		return false;
	}

	IGraphics::SGaussianBlurParams BlurParams;
	BlurParams.m_Radius = 4;
	BlurParams.m_Sigma = 2.0f;
	BlurParams.m_Mode = static_cast<IGraphics::EBlurMode>(BlurMode);
	if(!Graphics()->GaussianBlurRenderTarget(m_GaussianBlurSource, m_aGaussianBlurTemporary, m_GaussianBlurTarget, BlurParams))
	{
		m_GaussianBlurFailedFrame = PerfFrame;
		m_GaussianBlurPrepared = false;
		return false;
	}
	m_GaussianBlurPreparedFrame = PerfFrame;
	m_GaussianBlurPrepared = true;
	return true;
}

void CUi::RenderGaussianBlur(const CUIRect &Rect, float Alpha, int Corners, float Rounding)
{
	// 调用方（模糊作用域与 HUD 背景）只看作用域，不看 qm_gaussian_blur，因此功能关闭时
	// 每个半透明矩形仍会走到这里。关闭是用户的正常选择、不是失败，不能打 trace——
	// 否则 qm_graphics_trace 一开，加载界面/主菜单每帧都会刷满 "prepare failed"。
	if(m_GaussianBlurSuppressionDepth > 0 || Rect.w <= 0.0f || Rect.h <= 0.0f || Alpha <= 0.0f || !PrepareGaussianBlur())
	{
		if(g_Config.m_QmGaussianBlur != 0 && g_Config.m_QmGraphicsTrace >= 1 && m_GaussianBlurSuppressionDepth <= 0 && Rect.w > 0.0f && Rect.h > 0.0f && Alpha > 0.0f)
			dbg_msg("ui/blur", "blur unavailable this frame (prepare failed)");
		return;
	}

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
	const float MappedWidth = ScreenX1 - ScreenX0;
	const float MappedHeight = ScreenY1 - ScreenY0;
	if(MappedWidth <= 0.0f || MappedHeight <= 0.0f)
		return;

	CUIRect ClippedRect = Rect;
	if(IsClipped())
	{
		const CUIRect &CurrentClip = *ClipArea();
		const float Left = std::max(ClippedRect.x, CurrentClip.x);
		const float Top = std::max(ClippedRect.y, CurrentClip.y);
		const float Right = std::min(ClippedRect.x + ClippedRect.w, CurrentClip.x + CurrentClip.w);
		const float Bottom = std::min(ClippedRect.y + ClippedRect.h, CurrentClip.y + CurrentClip.h);
		ClippedRect = {Left, Top, Right - Left, Bottom - Top};
	}
	if(ClippedRect.w <= 0.0f || ClippedRect.h <= 0.0f)
		return;

	const float PixelScaleX = Graphics()->ScreenWidth() / MappedWidth;
	const float PixelScaleY = Graphics()->ScreenHeight() / MappedHeight;
	const int ClipX0 = std::clamp((int)std::floor((ClippedRect.x - ScreenX0) * PixelScaleX), 0, Graphics()->ScreenWidth());
	const int ClipY0 = std::clamp((int)std::floor((ClippedRect.y - ScreenY0) * PixelScaleY), 0, Graphics()->ScreenHeight());
	const int ClipX1 = std::clamp((int)std::ceil((ClippedRect.x + ClippedRect.w - ScreenX0) * PixelScaleX), 0, Graphics()->ScreenWidth());
	const int ClipY1 = std::clamp((int)std::ceil((ClippedRect.y + ClippedRect.h - ScreenY0) * PixelScaleY), 0, Graphics()->ScreenHeight());
	if(ClipX1 <= ClipX0 || ClipY1 <= ClipY0)
		return;

	Graphics()->ClipEnable(ClipX0, ClipY0, ClipX1 - ClipX0, ClipY1 - ClipY0);
	Graphics()->BlendNormal();
	IGraphics::SRenderTargetDrawParams DrawParams;
	DrawParams.m_X = Rect.x;
	DrawParams.m_Y = Rect.y;
	DrawParams.m_W = Rect.w;
	DrawParams.m_H = Rect.h;
	DrawParams.m_Alpha = std::clamp(Alpha, 0.0f, 1.0f);
	DrawParams.m_Corners = Corners;
	DrawParams.m_Rounding = Rounding;
	DrawParams.m_U0 = (Rect.x - ScreenX0) / MappedWidth;
	DrawParams.m_U1 = (Rect.x + Rect.w - ScreenX0) / MappedWidth;
	DrawParams.m_V0 = 1.0f - (Rect.y - ScreenY0) / MappedHeight;
	DrawParams.m_V1 = 1.0f - (Rect.y + Rect.h - ScreenY0) / MappedHeight;
	Graphics()->DrawRenderTarget(m_GaussianBlurTarget, DrawParams);
	if(IsClipped())
		UpdateClipping();
	else
		Graphics()->ClipDisable();
}

void CUi::OnCursorMove(float X, float Y)
{
	if(!CheckMouseLock())
	{
		m_UpdatedMousePos.x = std::clamp(m_UpdatedMousePos.x + X, 0.0f, Graphics()->WindowWidth() - 1.0f);
		m_UpdatedMousePos.y = std::clamp(m_UpdatedMousePos.y + Y, 0.0f, Graphics()->WindowHeight() - 1.0f);
	}

	m_UpdatedMouseDelta += vec2(X, Y);
}

void CUi::BeginWheelOwnershipFrame()
{
	const uint64_t FrameId = Client()->PerfFrame();
	if(m_WheelOwnership.FrameStarted(FrameId))
		return;
	float RawDelta = 0.0f;
	if(ConsumeHotkey(HOTKEY_SCROLL_UP))
		RawDelta += 120.0f;
	if(ConsumeHotkey(HOTKEY_SCROLL_DOWN))
		RawDelta -= 120.0f;
	m_WheelOwnership.BeginFrame(FrameId, RawDelta, Input() != nullptr && Input()->AltIsPressed());
}

void CUi::RegisterWheelOwner(const void *pOwnerId, EUiWheelOwnerPriority Priority, const CUIRect &HotRect, bool Eligible)
{
	QmRegisterWheelOwnerCandidate(m_WheelOwnership, {pOwnerId, Priority, HotRect, Eligible}, MousePos(), Enabled());
}

bool CUi::TryConsumeWheel(const void *pOwnerId, float *pDelta)
{
	return QmTryConsumeWheel(m_WheelOwnership, pOwnerId, pDelta);
}

void CUi::Update()
{
	m_PopupSourceClock.Update(Client()->PerfFrame());
	// 孤儿阻断弹窗兜底清扫：要求来源每帧刷新的弹窗（下拉选择弹层等）若连续
	// 两帧未刷新，说明来源渲染已停止且当前没有任何 RenderPopupMenus 调用方
	// 在运行（如聊天模式退出后弹窗残留），在这里强制关闭，防止底层指针输入
	// 被永久锁死。正常刷新节奏下弹层在前一帧渲染中刚刷新，差值恰为 1，不受影响。
	const uint64_t CurFrame = PopupSourceFrame();
	for(size_t i = 0; i < m_vPopupMenus.size();)
	{
		const SPopupMenu &PopupMenu = m_vPopupMenus[i];
		const bool Stale = !PopupMenu.m_Closing && PopupMenu.m_Props.m_RequireSourceRefresh &&
				   !QmDropdownSourceAlive(CurFrame, PopupMenu.m_Props.m_SourceFrame, true);
		if(!Stale)
		{
			++i;
			continue;
		}
		// ClosePopupMenu 至少移除目标自身（并按栈序连带其上方子弹窗），索引不前进
		ClosePopupMenu(PopupMenu.m_pId, true);
	}
	BeginWheelOwnershipFrame();
	m_MenuUiFirstWheelPerf = false;
	const int UiScale = std::clamp(g_Config.m_QmUiScale, 50, 200);
	if(UiScale != m_LastUiScale)
	{
		m_LastUiScale = UiScale;
		Client()->OnWindowResize();
	}
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const CUIRect *pScreen = Screen();

	unsigned UpdatedMouseButtonsNext = 0;
	if(Enabled())
	{
		// Update mouse buttons based on mouse keys
		for(int MouseKey = KEY_MOUSE_1; MouseKey <= KEY_MOUSE_3; ++MouseKey)
		{
			if(Input()->KeyIsPressed(MouseKey))
			{
				m_UpdatedMouseButtons |= 1 << (MouseKey - KEY_MOUSE_1);
			}
		}

		// Update mouse position and buttons based on touch finger state
		UpdateTouchState(m_TouchState);
		if(m_TouchState.m_AnyPressed)
		{
			if(!CheckMouseLock())
			{
				m_UpdatedMousePos = m_TouchState.m_PrimaryPosition * WindowSize;
				m_UpdatedMousePos.x = std::clamp(m_UpdatedMousePos.x, 0.0f, WindowSize.x - 1.0f);
				m_UpdatedMousePos.y = std::clamp(m_UpdatedMousePos.y, 0.0f, WindowSize.y - 1.0f);
			}
			m_UpdatedMouseDelta += m_TouchState.m_PrimaryDelta * WindowSize;

			// Scroll currently hovered scroll region with touch scroll gesture.
			if(m_TouchState.m_ScrollAmount != vec2(0.0f, 0.0f))
			{
				if(m_pHotScrollRegion != nullptr)
				{
					m_pHotScrollRegion->ScrollRelativeDirect(-m_TouchState.m_ScrollAmount * pScreen->Size());
				}
				m_TouchState.m_ScrollAmount = vec2(0.0f, 0.0f);
			}

			// We need to delay the click until the next update or it's not possible to use UI
			// elements because click and hover would happen at the same time for touch events.
			if(m_TouchState.m_PrimaryPressed)
			{
				UpdatedMouseButtonsNext |= 1;
			}
			if(m_TouchState.m_SecondaryPressed)
			{
				UpdatedMouseButtonsNext |= 2;
			}
		}
	}

	m_MousePos = m_UpdatedMousePos * vec2(pScreen->w, pScreen->h) / WindowSize;
	m_MouseDelta = m_UpdatedMouseDelta;
	m_UpdatedMouseDelta = vec2(0.0f, 0.0f);
	m_LastMouseButtons = m_MouseButtons;
	m_MouseButtons = m_UpdatedMouseButtons;
	m_UpdatedMouseButtons = UpdatedMouseButtonsNext;

	m_pHotItem = m_pBecomingHotItem;
	if(m_pActiveItem)
		m_pHotItem = m_pActiveItem;
	m_pBecomingHotItem = nullptr;
	m_pHotScrollRegion = m_pBecomingHotScrollRegion;
	m_pBecomingHotScrollRegion = nullptr;
	m_BecomingHotScrollRegionPriority = EUiWheelOwnerPriority::PAGE;
	m_UnderlyingScrollBlocked = false;
	for(const SPopupMenu &PopupMenu : m_vPopupMenus)
	{
		// 出场动画中的弹窗已逻辑关闭，不再锁定下层页面滚动。
		if(PopupMenu.m_Closing)
			continue;
		if(PopupMenu.m_Props.m_BlockUnderlyingScroll && MouseInside(&PopupMenu.m_Rect) && (!PopupMenu.m_Props.m_ClipToViewport || MouseInside(&PopupMenu.m_Props.m_Viewport)))
		{
			m_UnderlyingScrollBlocked = true;
			break;
		}
	}

	if(Enabled())
	{
		CLineInput *pActiveInput = CLineInput::GetActiveInput();
		if(pActiveInput && m_pLastActiveItem && pActiveInput != m_pLastActiveItem)
			pActiveInput->Deactivate();
	}
	else
	{
		m_pHotItem = nullptr;
		m_pActiveItem = nullptr;
		m_pHotScrollRegion = nullptr;
	}

	m_ProgressSpinnerOffset += Client()->RenderFrameTime() * 1.5f;
	m_ProgressSpinnerOffset = std::fmod(m_ProgressSpinnerOffset, 1.0f);
}

void CUi::DebugRender(float X, float Y)
{
	MapScreen();

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "hot=%p nexthot=%p active=%p lastactive=%p", HotItem(), NextHotItem(), ActiveItem(), m_pLastActiveItem);
	TextRender()->Text(X, Y, 10.0f, aBuf);
}

bool CUi::MouseInside(const CUIRect *pRect) const
{
	return pRect->Inside(MousePos());
}

bool CUi::UnderlyingPointerInputBlocked() const
{
	if(m_PopupInputDepth > 0)
		return false;

	return std::any_of(m_vPopupMenus.begin(), m_vPopupMenus.end(), [](const SPopupMenu &PopupMenu) {
		// 出场动画中的弹窗已逻辑关闭，不再锁定下层页面指针交互。
		return !PopupMenu.m_Closing && PopupMenu.m_Props.m_BlockUnderlyingPointerInput;
	});
}

bool CUi::MouseHovered(const CUIRect *pRect) const
{
	return !RenderOnly() && !UnderlyingPointerInputBlocked() && MouseInside(pRect) && MouseInsideClip();
}

void CUi::ConvertMouseMove(float *pX, float *pY, IInput::ECursorType CursorType) const
{
	float Factor = 1.0f;
	switch(CursorType)
	{
	case IInput::CURSOR_MOUSE:
		Factor = g_Config.m_UiMousesens / 100.0f;
		break;
	case IInput::CURSOR_JOYSTICK:
		Factor = g_Config.m_UiControllerSens / 100.0f;
		break;
	default:
		dbg_assert_failed("CUi::ConvertMouseMove CursorType %d", (int)CursorType);
	}

	if(m_MouseSlow)
		Factor *= 0.05f;

	*pX *= Factor;
	*pY *= Factor;
}

void CUi::UpdateTouchState(CTouchState &State) const
{
	const std::vector<IInput::CTouchFingerState> &vTouchFingerStates = Input()->TouchFingerStates();

	// Updated touch position as long as any finger is beinged pressed.
	const bool WasAnyPressed = State.m_AnyPressed;
	State.m_AnyPressed = !vTouchFingerStates.empty();
	if(State.m_AnyPressed)
	{
		// We always use the position of first finger being pressed down. Multi-touch UI is
		// not possible and always choosing the last finger would cause the cursor to briefly
		// warp without having any effect if multiple fingers are used.
		const IInput::CTouchFingerState &PrimaryTouchFingerState = vTouchFingerStates.front();
		State.m_PrimaryPosition = PrimaryTouchFingerState.m_Position;
		State.m_PrimaryDelta = PrimaryTouchFingerState.m_Delta;
	}

	// Update primary (left click) and secondary (right click) action.
	if(State.m_SecondaryPressedNext)
	{
		// The secondary action is delayed by one frame until the primary has been released,
		// otherwise most UI elements cannot be activated by the secondary action because they
		// never become the hot-item unless all mouse buttons are released for one frame.
		State.m_SecondaryPressedNext = false;
		State.m_SecondaryPressed = true;
	}
	else if(vTouchFingerStates.size() != 1)
	{
		// Consider primary and secondary to be pressed only when exactly one finger is pressed,
		// to avoid UI elements and console text selection being activated while scrolling.
		State.m_PrimaryPressed = false;
		State.m_SecondaryPressed = false;
	}
	else if(!WasAnyPressed)
	{
		State.m_PrimaryPressed = true;
		State.m_SecondaryActivationTime = Client()->GlobalTime();
		State.m_SecondaryActivationDelta = vec2(0.0f, 0.0f);
	}
	else if(State.m_PrimaryPressed)
	{
		// Activate secondary by pressing and holding roughly on the same position for some time.
		const float SecondaryActivationDelay = 0.5f;
		const float SecondaryActivationMaxDistance = 0.001f;
		State.m_SecondaryActivationDelta += State.m_PrimaryDelta;
		if(Client()->GlobalTime() - State.m_SecondaryActivationTime >= SecondaryActivationDelay &&
			length(State.m_SecondaryActivationDelta) <= SecondaryActivationMaxDistance)
		{
			State.m_PrimaryPressed = false;
			State.m_SecondaryPressedNext = true;
		}
	}

	// Handle two fingers being moved roughly in same direction as a scrolling gesture.
	if(vTouchFingerStates.size() == 2)
	{
		const vec2 Delta0 = vTouchFingerStates[0].m_Delta;
		const vec2 Delta1 = vTouchFingerStates[1].m_Delta;
		const float Similarity = dot(normalize(Delta0), normalize(Delta1));
		const float SimilarityThreshold = 0.8f; // How parallel the deltas have to be (1.0f being completely parallel)
		if(Similarity > SimilarityThreshold)
		{
			const float DirectionThreshold = 3.0f; // How much longer the delta of one axis has to be compared to other axis

			// Vertical scrolling (y-delta must be larger than x-delta)
			if(absolute(Delta0.y) > DirectionThreshold * absolute(Delta0.x) &&
				absolute(Delta1.y) > DirectionThreshold * absolute(Delta1.x) &&
				Delta0.y * Delta1.y > 0.0f) // Same y direction required
			{
				// Accumulate average delta of the two fingers
				State.m_ScrollAmount.y += (Delta0.y + Delta1.y) / 2.0f;
			}
			else if(absolute(Delta0.x) > DirectionThreshold * absolute(Delta0.y) && // Horizontal scrolling (x-delta must be larger than y-delta)
				absolute(Delta1.x) > DirectionThreshold * absolute(Delta1.y) &&
				Delta0.x * Delta1.x > 0.0f) // Same x direction required
			{
				// Accumulate average delta of the two fingers
				State.m_ScrollAmount.x += (Delta0.x + Delta1.x) / 2.0f;
			}
		}
	}
	else
	{
		// Scrolling gesture should start from zero again if released.
		State.m_ScrollAmount = vec2(0.0f, 0.0f);
	}
}

bool CUi::ConsumeHotkey(EHotkey Hotkey)
{
	const bool Pressed = m_HotkeysPressed & Hotkey;
	m_HotkeysPressed &= ~Hotkey;
	return Pressed;
}

bool CUi::OnInput(const IInput::CEvent &Event)
{
	if(!Enabled())
		return false;

	CLineInput *pActiveInput = CLineInput::GetActiveInput();
	if(pActiveInput && pActiveInput->ProcessInput(Event))
		return true;

	if(Event.m_Flags & IInput::FLAG_PRESS)
	{
		unsigned LastHotkeysPressed = m_HotkeysPressed;
		if(Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER)
			m_HotkeysPressed |= HOTKEY_ENTER;
		else if(Event.m_Key == KEY_ESCAPE)
			m_HotkeysPressed |= HOTKEY_ESCAPE;
		else if(Event.m_Key == KEY_TAB && !Input()->AltIsPressed())
			m_HotkeysPressed |= HOTKEY_TAB;
		else if(Event.m_Key == KEY_DELETE)
			m_HotkeysPressed |= HOTKEY_DELETE;
		else if(Event.m_Key == KEY_UP)
			m_HotkeysPressed |= HOTKEY_UP;
		else if(Event.m_Key == KEY_DOWN)
			m_HotkeysPressed |= HOTKEY_DOWN;
		else if(Event.m_Key == KEY_LEFT)
			m_HotkeysPressed |= HOTKEY_LEFT;
		else if(Event.m_Key == KEY_RIGHT)
			m_HotkeysPressed |= HOTKEY_RIGHT;
		else if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
			m_HotkeysPressed |= HOTKEY_SCROLL_UP;
		else if(Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
			m_HotkeysPressed |= HOTKEY_SCROLL_DOWN;
		else if(Event.m_Key == KEY_PAGEUP)
			m_HotkeysPressed |= HOTKEY_PAGE_UP;
		else if(Event.m_Key == KEY_PAGEDOWN)
			m_HotkeysPressed |= HOTKEY_PAGE_DOWN;
		else if(Event.m_Key == KEY_HOME)
			m_HotkeysPressed |= HOTKEY_HOME;
		else if(Event.m_Key == KEY_END)
			m_HotkeysPressed |= HOTKEY_END;
		return LastHotkeysPressed != m_HotkeysPressed;
	}
	return false;
}

float CUi::ButtonColorMul(const void *pId)
{
	if(CheckActiveItem(pId))
		return ButtonColorMulActive();
	else if(HotItem() == pId)
		return ButtonColorMulHot();
	return ButtonColorMulDefault();
}

const CUIRect *CUi::Screen()
{
	m_Screen.h = QmUiVirtualScreenHeight(g_Config.m_QmUiScale);
	m_Screen.w = Graphics()->ScreenAspect() * m_Screen.h;
	return &m_Screen;
}

void CUi::MapScreen()
{
	const CUIRect *pScreen = Screen();
	Graphics()->MapScreen(pScreen->x, pScreen->y, pScreen->w, pScreen->h);
}

float CUi::PixelSize()
{
	return Screen()->w / Graphics()->ScreenWidth();
}

void CUi::BeginRenderOnly()
{
	if(m_RenderOnlyDepth++ == 0)
	{
		CUIRect RenderOnlyClip = IsClipped() ? *ClipArea() : *Screen();
		// 锚定在现有裁剪区右下角，确保后续嵌套裁剪不会产生负宽高。
		RenderOnlyClip.x += RenderOnlyClip.w;
		RenderOnlyClip.y += RenderOnlyClip.h;
		RenderOnlyClip.w = 0.0f;
		RenderOnlyClip.h = 0.0f;
		ClipEnable(&RenderOnlyClip);
	}
}

void CUi::EndRenderOnly()
{
	dbg_assert(m_RenderOnlyDepth > 0, "render-only UI scope underflow");
	if(--m_RenderOnlyDepth == 0)
		ClipDisable();
}

void CUi::ClipEnable(const CUIRect *pRect)
{
	FlushQuadBatch();
	if(IsClipped())
	{
		const CUIRect *pOldRect = ClipArea();
		m_vClips.push_back(pRect->Intersection(*pOldRect));
	}
	else
	{
		m_vClips.push_back(*pRect);
	}
	UpdateClipping();
}

void CUi::ClipDisable()
{
	FlushQuadBatch();
	dbg_assert(IsClipped(), "no clip region");
	m_vClips.pop_back();
	UpdateClipping();
}

const CUIRect *CUi::ClipArea() const
{
	dbg_assert(IsClipped(), "no clip region");
	return &m_vClips.back();
}

const CUIRect *CUi::OutermostClipArea() const
{
	dbg_assert(IsClipped(), "no clip region");
	return &m_vClips.front();
}

void CUi::UpdateClipping()
{
	if(IsClipped())
	{
		const CUIRect *pRect = ClipArea();
		const float XScale = Graphics()->ScreenWidth() / Screen()->w;
		const float YScale = Graphics()->ScreenHeight() / Screen()->h;

		const float ScaledX = pRect->x * XScale;
		const float ScaledY = pRect->y * YScale;
		const float RoundX = std::round(ScaledX);
		const float RoundY = std::round(ScaledY);
		Graphics()->ClipEnable(RoundX, RoundY, std::round(pRect->w * XScale + (ScaledX - RoundX)), std::round(pRect->h * YScale + (ScaledY - RoundY)));
	}
	else
	{
		Graphics()->ClipDisable();
	}
}

void CUi::RegisterPassiveHotItem(const void *pId, const CUIRect *pRect)
{
	if(MouseHovered(pRect))
		SetHotItem(pId);
}

int CUi::QuadBatchRectContainer(float Width, float Height, float Rounding, int Corners) const
{
	if(!UiBatchableRectHasPositiveSize(Width, Height))
		return -1;

	for(const SQuadBatchRectContainer &Container : m_vQuadBatchRectContainers)
	{
		if(Container.m_Width == Width && Container.m_Height == Height && Container.m_Rounding == Rounding && Container.m_Corners == Corners)
			return Container.m_QuadContainerIndex;
	}

	SQuadBatchRectContainer &Container = m_vQuadBatchRectContainers.emplace_back();
	Container.m_Width = Width;
	Container.m_Height = Height;
	Container.m_Rounding = Rounding;
	Container.m_Corners = Corners;
	Container.m_QuadContainerIndex = Graphics()->CreateRectQuadContainer(-Width / 2.0f, -Height / 2.0f, Width, Height, Rounding, Corners);
	return Container.m_QuadContainerIndex;
}

void CUi::RenderQuadContainerBatchable(int QuadContainerIndex, float X, float Y, const ColorRGBA &Color) const
{
	const SUiQuadBatchSubmissionPlan Plan = UiPlanQuadBatchSubmission(IsQuadBatchActive(), m_QuadBatchContainerIndex, m_QuadBatchColor, QuadContainerIndex, Color);
	if(Plan.m_LeavesBatchUntouched)
		return;

	if(Plan.m_RenderImmediately)
	{
		Graphics()->TextureClear();
		Graphics()->SetColor(Color);
		Graphics()->RenderQuadContainerEx(QuadContainerIndex, 0, -1, X, Y);
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	if(Plan.m_FlushBeforeQueue)
		FlushQuadBatch();

	if(Plan.m_QueueSprite)
	{
		m_QuadBatchContainerIndex = QuadContainerIndex;
		m_QuadBatchColor = Color;
		IGraphics::SRenderSpriteInfo &Info = m_vQuadBatchSprites.emplace_back();
		Info.m_Pos = vec2(X, Y);
		Info.m_Scale = 1.0f;
		Info.m_Rotation = 0.0f;
	}
}

void CUi::BeginQuadBatch() const
{
	++m_QuadBatchDepth;
}

void CUi::FlushQuadBatch() const
{
	if(!UiQuadBatchHasPendingSubmission(m_QuadBatchContainerIndex, m_vQuadBatchSprites.size()))
		return;

	Graphics()->TextureClear();
	Graphics()->SetColor(m_QuadBatchColor);
	for(const IGraphics::SRenderSpriteInfo &Info : m_vQuadBatchSprites)
		Graphics()->RenderQuadContainerEx(m_QuadBatchContainerIndex, 0, -1, Info.m_Pos.x, Info.m_Pos.y);
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	m_vQuadBatchSprites.clear();
	m_QuadBatchContainerIndex = -1;
}

void CUi::EndQuadBatch() const
{
	if(m_QuadBatchDepth <= 0)
		return;

	--m_QuadBatchDepth;
	if(m_QuadBatchDepth == 0)
		FlushQuadBatch();
}

void CUi::RenderBatchableRect(const CUIRect *pRect, ColorRGBA Color, int Corners, float Rounding) const
{
	if(Color.a > 0.0f && Color.a < 1.0f && GaussianBlurScopeActive())
		const_cast<CUi *>(this)->RenderGaussianBlur(*pRect, GaussianBlurScopeAlpha(), Corners, Rounding);
	const int QuadContainerIndex = QuadBatchRectContainer(pRect->w, pRect->h, Rounding, Corners);
	RenderQuadContainerBatchable(QuadContainerIndex, pRect->x + pRect->w / 2.0f, pRect->y + pRect->h / 2.0f, Color);
}

int CUi::DoButtonLogic(const void *pId, int Checked, const CUIRect *pRect, const unsigned Flags)
{
	if(RenderOnly())
		return 0;

	int ReturnValue = 0;
	const bool Inside = MouseHovered(pRect);
	const bool UseCurrentHit = (Flags & BUTTONFLAG_CURRENT_HIT) != 0;
	const bool AllowCurrentPress = UseCurrentHit || (PreLayoutInput() && !IsPopupOpen());
	const int CurrentPress = AllowCurrentPress ? QmButtonCurrentPress(QmResolvePointerButtons(m_MouseButtons, m_LastMouseButtons, UnderlyingPointerInputBlocked()), Flags, Inside) : -1;
	// 预布局及显式启用的下拉触发器按当前矩形接管新按下；释放仍由原状态机处理。
	// MouseHovered 和按钮状态共同保留裁剪与弹层输入屏蔽。
	if(CurrentPress >= 0)
	{
		// 新按下意味着旧控件不应继续占用 ActiveItem。被裁剪的卡片可能
		// 没有机会在上一帧处理释放，此处同时释放陈旧状态和文本输入焦点。
		if(m_pActiveItem != nullptr || m_pLastActiveItem != nullptr)
		{
			if(CLineInput *pActiveInput = CLineInput::GetActiveInput())
				pActiveInput->Deactivate();
			m_pLastActiveItem = nullptr;
			SetActiveItem(nullptr);
		}
		m_pHotItem = pId;
		m_pBecomingHotItem = pId;
	}

	if(CheckActiveItem(pId))
	{
		dbg_assert(m_ActiveButtonLogicButton >= 0, "m_ActiveButtonLogicButton invalid");
		if(!MouseButton(m_ActiveButtonLogicButton))
		{
			if(Inside && Checked >= 0)
				ReturnValue = 1 + m_ActiveButtonLogicButton;
			SetActiveItem(nullptr);
			m_ActiveButtonLogicButton = -1;
		}
	}

	bool NoRelevantButtonsPressed = true;
	for(int Button = 0; Button < 3; ++Button)
	{
		if((Flags & (BUTTONFLAG_LEFT << Button)) && MouseButton(Button))
		{
			NoRelevantButtonsPressed = false;
			if(UseCurrentHit ? CurrentPress == Button : HotItem() == pId)
			{
				SetActiveItem(pId);
				m_ActiveButtonLogicButton = Button;
			}
		}
	}

	if(Inside && NoRelevantButtonsPressed)
		SetHotItem(pId);

	return ReturnValue;
}

int CUi::DoDraggableButtonLogic(const void *pId, int Checked, const CUIRect *pRect, bool *pClicked, bool *pAbrupted)
{
	if(RenderOnly())
	{
		if(pClicked != nullptr)
			*pClicked = false;
		if(pAbrupted != nullptr)
			*pAbrupted = false;
		return 0;
	}

	// logic
	int ReturnValue = 0;
	const bool Inside = MouseHovered(pRect);

	if(pClicked != nullptr)
		*pClicked = false;
	if(pAbrupted != nullptr)
		*pAbrupted = false;

	if(CheckActiveItem(pId))
	{
		dbg_assert(m_ActiveDraggableButtonLogicButton >= 0, "m_ActiveDraggableButtonLogicButton invalid");
		if(m_ActiveDraggableButtonLogicButton == 0)
		{
			if(Checked >= 0)
				ReturnValue = 1 + m_ActiveDraggableButtonLogicButton;
			if(!MouseButton(m_ActiveDraggableButtonLogicButton))
			{
				if(pClicked != nullptr)
					*pClicked = true;
				SetActiveItem(nullptr);
				m_ActiveDraggableButtonLogicButton = -1;
			}
			if(MouseButton(1))
			{
				if(pAbrupted != nullptr)
					*pAbrupted = true;
				SetActiveItem(nullptr);
				m_ActiveDraggableButtonLogicButton = -1;
			}
		}
		else if(!MouseButton(m_ActiveDraggableButtonLogicButton))
		{
			if(Inside && Checked >= 0)
				ReturnValue = 1 + m_ActiveDraggableButtonLogicButton;
			if(pClicked != nullptr)
				*pClicked = true;
			SetActiveItem(nullptr);
			m_ActiveDraggableButtonLogicButton = -1;
		}
	}
	else if(HotItem() == pId)
	{
		for(int i = 0; i < 3; ++i)
		{
			if(MouseButton(i))
			{
				SetActiveItem(pId);
				m_ActiveDraggableButtonLogicButton = i;
			}
		}
	}

	if(Inside && !MouseButton(0) && !MouseButton(1) && !MouseButton(2))
		SetHotItem(pId);

	return ReturnValue;
}

bool CUi::DoDoubleClickLogic(const void *pId)
{
	if(RenderOnly())
		return false;

	if(m_DoubleClickState.m_pLastClickedId == pId &&
		Client()->GlobalTime() - m_DoubleClickState.m_LastClickTime < 0.5f &&
		distance(m_DoubleClickState.m_LastClickPos, MousePos()) <= 32.0f * Screen()->h / Graphics()->ScreenHeight())
	{
		m_DoubleClickState.m_pLastClickedId = nullptr;
		return true;
	}
	m_DoubleClickState.m_pLastClickedId = pId;
	m_DoubleClickState.m_LastClickTime = Client()->GlobalTime();
	m_DoubleClickState.m_LastClickPos = MousePos();
	return false;
}

EEditState CUi::DoPickerLogic(const void *pId, const CUIRect *pRect, float *pX, float *pY)
{
	if(RenderOnly())
		return EEditState::NONE;

	const bool Inside = MouseHovered(pRect);
	if(Inside)
		SetHotItem(pId);

	if(!CheckActiveItem(pId))
	{
		if(Inside && MouseButtonClicked(0))
		{
			SetActiveItem(pId);
		}
		else
		{
			return EEditState::NONE;
		}
	}

	EEditState Res = EEditState::EDITING;
	if(!MouseButton(0))
	{
		SetActiveItem(nullptr);
		Res = EEditState::END;
	}
	else if(MouseButtonClicked(0))
		Res = EEditState::START;

	if(Input()->ShiftIsPressed())
		m_MouseSlow = true;

	if(pX)
		*pX = std::clamp(MouseX() - pRect->x, 0.0f, pRect->w);
	if(pY)
		*pY = std::clamp(MouseY() - pRect->y, 0.0f, pRect->h);

	return Res;
}

void CUi::DoSmoothScrollLogic(float *pScrollOffset, float *pScrollOffsetChange, float ViewPortSize, float TotalSize, bool SmoothClamp, float ScrollSpeed) const
{
	// reset scrolling if it's not necessary anymore
	if(TotalSize < ViewPortSize)
	{
		*pScrollOffsetChange = -*pScrollOffset;
	}

	// instant scrolling if distance too long
	if(absolute(*pScrollOffsetChange) > 2.0f * ViewPortSize)
	{
		*pScrollOffset += *pScrollOffsetChange;
		*pScrollOffsetChange = 0.0f;
	}

	// smooth scrolling
	if(*pScrollOffsetChange)
	{
		const float Delta = *pScrollOffsetChange * std::clamp(Client()->RenderFrameTime() * ScrollSpeed, 0.0f, 1.0f);
		*pScrollOffset += Delta;
		*pScrollOffsetChange -= Delta;
	}

	// clamp to first item
	if(*pScrollOffset < 0.0f)
	{
		if(SmoothClamp && *pScrollOffset < -0.1f)
		{
			*pScrollOffsetChange = -*pScrollOffset;
		}
		else
		{
			*pScrollOffset = 0.0f;
			*pScrollOffsetChange = 0.0f;
		}
	}

	// clamp to last item
	if(TotalSize > ViewPortSize && *pScrollOffset > TotalSize - ViewPortSize)
	{
		if(SmoothClamp && *pScrollOffset - (TotalSize - ViewPortSize) > 0.1f)
		{
			*pScrollOffsetChange = (TotalSize - ViewPortSize) - *pScrollOffset;
		}
		else
		{
			*pScrollOffset = TotalSize - ViewPortSize;
			*pScrollOffsetChange = 0.0f;
		}
	}
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
struct SCursorAndBoundingBox
{
	vec2 m_TextSize;
	float m_BiggestCharacterHeight;
	int m_LineCount;
};

static SCursorAndBoundingBox CalcFontSizeCursorHeightAndBoundingBox(ITextRender *pTextRender, const char *pText, int Flags, float &Size, float MaxWidth, const SLabelProperties &LabelProps)
{
	const float MaxTextWidth = LabelProps.m_MaxWidth != -1.0f ? LabelProps.m_MaxWidth : MaxWidth;
	const int FlagsWithoutStop = Flags & ~(TEXTFLAG_STOP_AT_END | TEXTFLAG_ELLIPSIS_AT_END);
	const float MaxTextWidthWithoutStop = Flags == FlagsWithoutStop ? LabelProps.m_MaxWidth : -1.0f;

	float TextBoundingHeight = 0.0f;
	float TextHeight = 0.0f;
	int LineCount = 0;
	STextSizeProperties TextSizeProps{};
	TextSizeProps.m_pHeight = &TextHeight;
	TextSizeProps.m_pMaxCharacterHeightInLine = &TextBoundingHeight;
	TextSizeProps.m_pLineCount = &LineCount;

	float TextWidth;
	do
	{
		Size = maximum(Size, LabelProps.m_MinimumFontSize);
		// Only consider stop-at-end and ellipsis-at-end when minimum font size reached or font scaling disabled
		if((Size == LabelProps.m_MinimumFontSize || !LabelProps.m_EnableWidthCheck) && Flags != FlagsWithoutStop)
			TextWidth = pTextRender->TextWidth(Size, pText, -1, LabelProps.m_MaxWidth, Flags, TextSizeProps);
		else
			TextWidth = pTextRender->TextWidth(Size, pText, -1, MaxTextWidthWithoutStop, FlagsWithoutStop, TextSizeProps);
		if(TextWidth <= MaxTextWidth + 0.001f || !LabelProps.m_EnableWidthCheck || Size == LabelProps.m_MinimumFontSize)
			break;
		Size--;
	} while(true);

	SCursorAndBoundingBox Res{};
	Res.m_TextSize = vec2(TextWidth, TextHeight);
	Res.m_BiggestCharacterHeight = TextBoundingHeight;
	Res.m_LineCount = LineCount;
	return Res;
}

static int GetFlagsForLabelProperties(const SLabelProperties &LabelProps, const CTextCursor *pReadCursor)
{
	if(pReadCursor != nullptr)
		return pReadCursor->m_Flags & ~TEXTFLAG_RENDER;

	int Flags = 0;
	Flags |= LabelProps.m_DisallowNewline ? TEXTFLAG_DISALLOW_NEWLINE : 0;
	Flags |= LabelProps.m_StopAtEnd ? TEXTFLAG_STOP_AT_END : 0;
	Flags |= LabelProps.m_EllipsisAtEnd ? TEXTFLAG_ELLIPSIS_AT_END : 0;
	return Flags;
}

int CUi::GetLabelFlagsForProperties(const SLabelProperties &LabelProps, const CTextCursor *pReadCursor) const
{
	return GetFlagsForLabelProperties(LabelProps, pReadCursor);
}

vec2 CUi::CalcAlignedCursorPos(const CUIRect *pRect, vec2 TextSize, int Align, const float *pBiggestCharHeight)
{
	vec2 Cursor(pRect->x, pRect->y);

	const int HorizontalAlign = Align & TEXTALIGN_MASK_HORIZONTAL;
	if(HorizontalAlign == TEXTALIGN_CENTER)
	{
		Cursor.x += (pRect->w - TextSize.x) / 2.0f;
	}
	else if(HorizontalAlign == TEXTALIGN_RIGHT)
	{
		Cursor.x += pRect->w - TextSize.x;
	}

	const int VerticalAlign = Align & TEXTALIGN_MASK_VERTICAL;
	if(VerticalAlign == TEXTALIGN_MIDDLE)
	{
		Cursor.y += pBiggestCharHeight != nullptr ? ((pRect->h - *pBiggestCharHeight) / 2.0f - (TextSize.y - *pBiggestCharHeight)) : (pRect->h - TextSize.y) / 2.0f;
	}
	else if(VerticalAlign == TEXTALIGN_BOTTOM)
	{
		Cursor.y += pRect->h - TextSize.y;
	}

	return Cursor;
}

bool CUi::DrawCachedQmIconLabel(const CUIRect &Rect, const char *pText, float Size, int Align, int Count) const
{
	if(Count <= 0 || !std::isfinite(Size) || Size <= 0.0f)
		return false;
	float X0, Y0, X1, Y1;
	Graphics()->GetScreen(&X0, &Y0, &X1, &Y1);
	const float PixelScale = Y1 > Y0 ? Graphics()->ScreenHeight() / (Y1 - Y0) : 1.0f;
	const float PixelScaleX = X1 > X0 ? Graphics()->ScreenWidth() / (X1 - X0) : 1.0f;
	const int Weight = NormalizeQmIconWeight(g_Config.m_QmUiIconWeight);
	const EFontPreset Preset = TextRender()->GetFontPreset();
	const unsigned Flags = TextRender()->GetRenderFlags() & ~TEXT_RENDER_FLAG_ONE_TIME_USE;
	SQmCachedIconLabel *pLabel = nullptr;
	for(auto &Label : m_vQmCachedIconLabels)
	{
		if(Label.m_FontSize == Size && Label.m_PixelScale == PixelScale && Label.m_PixelScaleX == PixelScaleX && Label.m_Weight == Weight && Label.m_Preset == Preset && Label.m_Flags == Flags && Label.m_Text == pText)
		{
			pLabel = &Label;
			break;
		}
	}
	if(pLabel == nullptr)
	{
		// 动画连续字号不能让容器无限增长；淘汰时先归还对应 GPU 资源。
		if(m_vQmCachedIconLabels.size() >= 256)
		{
			TextRender()->DeleteTextContainer(m_vQmCachedIconLabels.front().m_Container);
			m_vQmCachedIconLabels.erase(m_vQmCachedIconLabels.begin());
		}
		m_vQmCachedIconLabels.push_back({pText, Size, PixelScale, PixelScaleX, Weight, Preset, Flags, {}, {}, 0.0f});
		pLabel = &m_vQmCachedIconLabels.back();
	}
	if(!pLabel->m_Container.Valid())
	{
		CTextCursor Cursor;
		Cursor.m_FontSize = Size;
		const ColorRGBA PreviousColor = TextRender()->GetTextColor();
		const ColorRGBA PreviousOutline = TextRender()->GetTextOutlineColor();
		const unsigned PreviousFlags = TextRender()->GetRenderFlags();
		TextRender()->TextColor(ColorRGBA(1, 1, 1, 1));
		TextRender()->TextOutlineColor(ColorRGBA(1, 1, 1, 1));
		TextRender()->SetRenderFlags(Flags);
		TextRender()->CreateTextContainer(pLabel->m_Container, &Cursor, pText);
		TextRender()->SetRenderFlags(PreviousFlags);
		TextRender()->TextColor(PreviousColor);
		TextRender()->TextOutlineColor(PreviousOutline);
		pLabel->m_Size = vec2(Cursor.m_LongestLineWidth, Cursor.Height());
		pLabel->m_BiggestCharacterHeight = Cursor.m_MaxCharacterHeight;
	}
	if(!pLabel->m_Container.Valid())
		return false;
	const vec2 Pos = CalcAlignedCursorPos(&Rect, pLabel->m_Size, Align, &pLabel->m_BiggestCharacterHeight);
	const EQmIcon Icon = Count == 1 ? CQmIconRegistry::IconFromGlyph(pText) : EQmIcon::COUNT;
	const ColorRGBA Color = ConfiguredQmUiIconColor(TextRender()->GetTextColor(), Icon);
	const CQmIconSemanticColorScope SemanticColorScope(QmUiIconHasSemanticColor(Icon));
	FlushQuadBatch();
	TextRender()->RenderTextContainer(pLabel->m_Container, Color, ConfiguredQmUiIconContrastColor(Color), Pos.x, Pos.y);
	CQmIconDrawDiagnostics::Record(Count);
	return true;
}

CLabelResult CUi::DoLabel(const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps) const
{
	const SQmIconLabelGlyphs Icons = QmIconLabelGlyphs(TextRender()->GetFontPreset(), pText);
	if(Icons.m_Count > 0 && pRect->w >= Size * Icons.m_Count && pRect->h >= Size && LabelProps.m_vColorSplits.empty() && LabelProps.m_MaxWidth < 0 && DrawCachedQmIconLabel(*pRect, pText, Size, Align, Icons.m_Count))
		return CLabelResult{};

	const int Flags = GetFlagsForLabelProperties(LabelProps, nullptr);
	const SCursorAndBoundingBox TextBounds = CalcFontSizeCursorHeightAndBoundingBox(TextRender(), pText, Flags, Size, pRect->w, LabelProps);
	const vec2 CursorPos = CalcAlignedCursorPos(pRect, TextBounds.m_TextSize, Align, TextBounds.m_LineCount == 1 ? &TextBounds.m_BiggestCharacterHeight : nullptr);

	CTextCursor Cursor;
	Cursor.SetPosition(CursorPos);
	Cursor.m_FontSize = Size;
	Cursor.m_Flags |= Flags;
	Cursor.m_vColorSplits = LabelProps.m_vColorSplits;
	Cursor.m_LineWidth = (float)LabelProps.m_MaxWidth;
	FlushQuadBatch();
	if(Icons.m_Count > 0)
	{
		const EQmIcon Icon = Icons.m_Count == 1 ? Icons.m_aIcons[0] : EQmIcon::COUNT;
		const ColorRGBA IconColor = ConfiguredQmUiIconColor(TextRender()->GetTextColor(), Icon);
		const CQmIconSemanticColorScope SemanticColorScope(QmUiIconHasSemanticColor(Icon));
		QmRenderImmediateFontIcon(*TextRender(), &Cursor, pText, -1, IconColor, ConfiguredQmUiIconContrastColor(IconColor));
		CQmIconDrawDiagnostics::Record(Icons.m_Count);
	}
	else
		TextRender()->TextEx(&Cursor, pText, -1);
	return CLabelResult{.m_Truncated = Cursor.m_Truncated};
}

void CUi::DoLabel(CUIElement::SUIElementRect &RectEl, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor) const
{
	const auto Icons = pReadCursor == nullptr ? QmIconLabelGlyphs(TextRender()->GetFontPreset(), pText, StrLen) : SQmIconLabelGlyphs{};
	RectEl.m_aQmIcons = Icons.m_aIcons;
	RectEl.m_NumQmIcons = Icons.m_Count;
	RectEl.m_FontPreset = TextRender()->GetFontPreset();
	const int Flags = GetFlagsForLabelProperties(LabelProps, pReadCursor);
	const SCursorAndBoundingBox TextBounds = CalcFontSizeCursorHeightAndBoundingBox(TextRender(), pText, Flags, Size, pRect->w, LabelProps);

	CTextCursor Cursor;
	if(pReadCursor)
	{
		Cursor = *pReadCursor;
	}
	else
	{
		const float *pBiggestCharHeight = TextBounds.m_LineCount == 1 ? &TextBounds.m_BiggestCharacterHeight : nullptr;
		Cursor.SetPosition(CalcAlignedCursorPos(pRect, TextBounds.m_TextSize, Align, pBiggestCharHeight));
		Cursor.m_FontSize = Size;
		Cursor.m_Flags |= Flags;
	}
	Cursor.m_LineWidth = LabelProps.m_MaxWidth;

	RectEl.m_FontSize = Size;
	RectEl.m_TextAlign = Align;
	RectEl.m_LabelMaxWidth = LabelProps.m_MaxWidth;
	RectEl.m_LabelFlags = Flags;
	RectEl.m_LineCount = TextBounds.m_LineCount;
	RectEl.m_BiggestCharacterHeight = TextBounds.m_BiggestCharacterHeight;
	RectEl.m_TextColor = TextRender()->GetTextColor();
	RectEl.m_TextOutlineColor = TextRender()->GetTextOutlineColor();
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	TextRender()->CreateTextContainer(RectEl.m_UITextContainer, &Cursor, pText, StrLen);
	TextRender()->TextColor(RectEl.m_TextColor);
	TextRender()->TextOutlineColor(RectEl.m_TextOutlineColor);
	RectEl.m_Cursor = Cursor;
}

void CUi::RenderLabelTextContainerAligned(const CUIElement::SUIElementRect &RectEl, const CUIRect *pRect, int Align) const
{
	if(pRect == nullptr || !RectEl.m_UITextContainer.Valid())
		return;

	const float *pBiggestCharHeight = RectEl.m_LineCount == 1 ? &RectEl.m_BiggestCharacterHeight : nullptr;
	const vec2 CursorPos = CalcAlignedCursorPos(pRect, vec2(RectEl.m_Cursor.m_LongestLineWidth, RectEl.m_Cursor.Height()), Align, pBiggestCharHeight);
	FlushQuadBatch();
	// 颜色在绘制时解析，彩虹和即时切换无需重建缓存文本容器。
	const EQmIcon Icon = RectEl.m_NumQmIcons == 1 ? RectEl.m_aQmIcons[0] : EQmIcon::COUNT;
	const ColorRGBA Color = RectEl.m_NumQmIcons > 0 ? ConfiguredQmUiIconColor(RectEl.m_TextColor, Icon) : RectEl.m_TextColor;
	const CQmIconSemanticColorScope SemanticColorScope(QmUiIconHasSemanticColor(Icon));
	const ColorRGBA OutlineColor = RectEl.m_NumQmIcons > 0 ? ConfiguredQmUiIconContrastColor(Color) : RectEl.m_TextOutlineColor;
	TextRender()->RenderTextContainer(RectEl.m_UITextContainer, Color, OutlineColor, CursorPos.x, CursorPos.y);
	CQmIconDrawDiagnostics::Record(RectEl.m_NumQmIcons);
}

void CUi::DoLabelStreamed(CUIElement::SUIElementRect &RectEl, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render, bool *pTextContainerRecreated) const
{
	const int Flags = GetFlagsForLabelProperties(LabelProps, pReadCursor);
	const int ReadCursorGlyphCount = pReadCursor == nullptr ? -1 : pReadCursor->m_GlyphCount;
	bool NeedsRecreate = false;
	bool ColorChanged = RectEl.m_TextColor != TextRender()->GetTextColor() || RectEl.m_TextOutlineColor != TextRender()->GetTextOutlineColor();
	const auto Icons = pReadCursor == nullptr ? QmIconLabelGlyphs(TextRender()->GetFontPreset(), pText, StrLen) : SQmIconLabelGlyphs{};
	bool StyleChanged = RectEl.m_FontPreset != TextRender()->GetFontPreset() || RectEl.m_NumQmIcons != Icons.m_Count || RectEl.m_aQmIcons != Icons.m_aIcons || RectEl.m_FontSize != Size || RectEl.m_TextAlign != Align || RectEl.m_LabelMaxWidth != LabelProps.m_MaxWidth || RectEl.m_LabelFlags != Flags;
	if(ColorChanged)
	{
		RectEl.m_TextColor = TextRender()->GetTextColor();
		RectEl.m_TextOutlineColor = TextRender()->GetTextOutlineColor();
	}
	if((!RectEl.m_UITextContainer.Valid() && pText[0] != '\0' && StrLen != 0) || RectEl.m_Width != pRect->w || RectEl.m_Height != pRect->h || StyleChanged || RectEl.m_ReadCursorGlyphCount != ReadCursorGlyphCount)
	{
		NeedsRecreate = true;
	}
	else
	{
		if(StrLen <= -1)
		{
			if(str_comp(RectEl.m_Text.c_str(), pText) != 0)
				NeedsRecreate = true;
		}
		else
		{
			if(StrLen != (int)RectEl.m_Text.size() || str_comp_num(RectEl.m_Text.c_str(), pText, StrLen) != 0)
				NeedsRecreate = true;
		}
	}
	if(pTextContainerRecreated != nullptr)
		*pTextContainerRecreated = NeedsRecreate;
	RectEl.m_X = pRect->x;
	RectEl.m_Y = pRect->y;
	if(NeedsRecreate)
	{
		TextRender()->DeleteTextContainer(RectEl.m_UITextContainer);

		RectEl.m_Width = pRect->w;
		RectEl.m_Height = pRect->h;

		if(StrLen > 0)
			RectEl.m_Text = std::string(pText, StrLen);
		else if(StrLen < 0)
			RectEl.m_Text = pText;
		else
			RectEl.m_Text.clear();

		RectEl.m_ReadCursorGlyphCount = ReadCursorGlyphCount;

		CUIRect TmpRect;
		TmpRect.x = 0;
		TmpRect.y = 0;
		TmpRect.w = pRect->w;
		TmpRect.h = pRect->h;

		DoLabel(RectEl, &TmpRect, pText, Size, TEXTALIGN_TL, LabelProps, StrLen, pReadCursor);
	}

	if(Render && RectEl.m_UITextContainer.Valid())
	{
		RenderLabelTextContainerAligned(RectEl, pRect, Align);
	}
}

CLabelResult CUi::DoLabel_AutoLineSize(const char *pText, float FontSize, int Align, CUIRect *pRect, float LineSize, const SLabelProperties &LabelProps) const
{
	CUIRect LabelRect;
	pRect->HSplitTop(LineSize, &LabelRect, pRect);

	return DoLabel(&LabelRect, pText, FontSize, Align);
}

bool CUi::DoEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners, const std::vector<STextColorSplit> &vColorSplits, int Align)
{
	return DoEditBox(pLineInput, pRect, FontSize, Corners, vColorSplits, Align, {});
}

bool CUi::DoEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners, const std::vector<STextColorSplit> &vColorSplits, int Align, const SEditBoxRenderOptions &RenderOptions)
{
	CUiScopedSurfaceText SurfaceText(TextRender(), ResolveConfiguredInputSurface(), RenderOptions.m_DrawBackground);
	const float VSpacing = 2.0f;
	const float EditBoxRounding = ui_token::radius::BASE;
	const CUIRect *pHitRect = RenderOptions.m_pHitRect != nullptr ? RenderOptions.m_pHitRect : pRect;
	CUIRect Textbox;
	pRect->VMargin(VSpacing, &Textbox);
	if(RenderOnly())
	{
		if(RenderOptions.m_DrawBackground)
			DrawRoundedSurface(this, *pRect, ResolveConfiguredInputSurface(), ColorRGBA(), EditBoxRounding, 0.0f, Corners);
		ClipEnable(pRect);
		Textbox.x -= pLineInput->GetScrollOffset();
		pLineInput->Render(&Textbox, FontSize, Align, false, -1.0f, 0.0f, vColorSplits);
		ClipDisable();
		return false;
	}

	const bool Inside = MouseHovered(pHitRect);
	bool Active = m_pLastActiveItem == pLineInput;
	const bool Changed = pLineInput->WasChanged();
	const bool CursorChanged = pLineInput->WasCursorChanged();
	const bool SubmitPressed = Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER) || ConsumeHotkey(HOTKEY_ENTER);
	const bool ClickedOutside = (MouseButtonClicked(0) || MouseButtonClicked(1)) && !Inside;

	bool JustGotActive = false;
	if(CheckActiveItem(pLineInput))
	{
		if(MouseButton(0))
		{
			if(pLineInput->IsActive() && (Input()->HasComposition() || Input()->GetCandidateCount()))
			{
				// Clear IME composition/candidates on mouse press
				Input()->StopTextInput();
				Input()->StartTextInput();
			}
		}
		else
		{
			SetActiveItem(nullptr);
		}
	}
	else if(QmEditBoxShouldStartActivation(Inside, MouseButtonClicked(0)))
	{
		if(!Active)
			JustGotActive = true;
		SetActiveItem(pLineInput);
	}

	if(Inside && !MouseButton(0))
		SetHotItem(pLineInput);

	Active = m_pLastActiveItem == pLineInput;
	if(Active && (SubmitPressed || ClickedOutside))
	{
		ReleaseActiveTextInput(pLineInput);
		Active = false;
	}
	if(Enabled() && Active && !JustGotActive)
		pLineInput->Activate(EInputPriority::UI);
	else
		pLineInput->Deactivate();

	float ScrollOffset = pLineInput->GetScrollOffset();
	float ScrollOffsetChange = pLineInput->GetScrollOffsetChange();

	// Update mouse selection information
	CLineInput::SMouseSelection *pMouseSelection = pLineInput->GetMouseSelection();
	if(Inside)
	{
		if(!pMouseSelection->m_Selecting && MouseButtonClicked(0))
		{
			pMouseSelection->m_Selecting = true;
			pMouseSelection->m_PressMouse = MousePos();
			pMouseSelection->m_Offset.x = ScrollOffset;
		}
	}
	if(pMouseSelection->m_Selecting)
	{
		pMouseSelection->m_ReleaseMouse = MousePos();
		if(!MouseButton(0))
		{
			pMouseSelection->m_Selecting = false;
			if(Active)
			{
				Input()->EnsureScreenKeyboardShown();
			}
		}
	}
	if(ScrollOffset != pMouseSelection->m_Offset.x)
	{
		// When the scroll offset is changed, update the position that the mouse was pressed at,
		// so the existing text selection still stays mostly the same.
		// TODO: The selection may change by one character temporarily, due to different character widths.
		//       Needs text render adjustment: keep selection start based on character.
		pMouseSelection->m_PressMouse.x -= ScrollOffset - pMouseSelection->m_Offset.x;
		pMouseSelection->m_Offset.x = ScrollOffset;
	}

	// Render
	if(RenderOptions.m_DrawBackground)
		DrawRoundedSurface(this, *pRect, ResolveConfiguredInputSurface(), ColorRGBA(), EditBoxRounding, 0.0f, Corners);
	ClipEnable(pRect);
	Textbox.x -= ScrollOffset;
	const STextBoundingBox BoundingBox = pLineInput->Render(&Textbox, FontSize, Align, Changed || CursorChanged, -1.0f, 0.0f, vColorSplits);
	ClipDisable();

	// Scroll left or right if necessary
	if(Active && !JustGotActive && (Changed || CursorChanged || Input()->HasComposition()))
	{
		const float CaretPositionX = pLineInput->GetCaretPosition().x - Textbox.x - ScrollOffset - ScrollOffsetChange;
		if(CaretPositionX > Textbox.w)
			ScrollOffsetChange += CaretPositionX - Textbox.w;
		else if(CaretPositionX < 0.0f)
			ScrollOffsetChange += CaretPositionX;
	}

	DoSmoothScrollLogic(&ScrollOffset, &ScrollOffsetChange, Textbox.w, BoundingBox.m_W, true);

	pLineInput->SetScrollOffset(ScrollOffset);
	pLineInput->SetScrollOffsetChange(ScrollOffsetChange);

	return Changed;
}

bool CUi::DoEditBoxMultiLine(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, float LineSpacing, int TextAlign, const SEditBoxRenderOptions &RenderOptions)
{
	CUiScopedSurfaceText SurfaceText(TextRender(), ResolveConfiguredInputSurface(), RenderOptions.m_DrawBackground);
	if(pLineInput == nullptr || pRect == nullptr)
		return false;

	const CUIRect *pHitRect = RenderOptions.m_pHitRect != nullptr ? RenderOptions.m_pHitRect : pRect;
	const bool Inside = MouseHovered(pHitRect);
	bool Active = ActiveItem() == pLineInput || pLineInput->IsActive();
	const bool Changed = pLineInput->WasChanged();
	const bool CursorChanged = pLineInput->WasCursorChanged();
	const bool ClickedOutside = (MouseButtonClicked(0) || MouseButtonClicked(1)) && !Inside;

	constexpr float VSpacing = 2.0f;
	CUIRect Textbox;
	pRect->VMargin(VSpacing, &Textbox);
	const float LineWidth = Textbox.w;

	bool JustGotActive = false;
	if(CheckActiveItem(pLineInput))
	{
		if(MouseButton(0))
		{
			if(pLineInput->IsActive() && (Input()->HasComposition() || Input()->GetCandidateCount()))
			{
				Input()->StopTextInput();
				Input()->StartTextInput();
			}
		}
		else
		{
			SetActiveItem(nullptr);
		}
	}
	else if(QmEditBoxShouldStartActivation(Inside, MouseButtonClicked(0)))
	{
		if(!Active)
			JustGotActive = true;
		SetActiveItem(pLineInput);
	}

	if(Inside && !MouseButton(0))
		SetHotItem(pLineInput);
	if(Active && ClickedOutside)
	{
		ReleaseActiveTextInput(pLineInput);
		Active = false;
	}

	if(Enabled() && Active && !JustGotActive)
		pLineInput->Activate(EInputPriority::UI);
	else
		pLineInput->Deactivate();

	CLineInput::SMouseSelection *pMouseSelection = pLineInput->GetMouseSelection();
	if(Inside)
	{
		if(!pMouseSelection->m_Selecting && MouseButtonClicked(0))
		{
			pMouseSelection->m_Selecting = true;
			pMouseSelection->m_PressMouse = MousePos();
			pMouseSelection->m_Offset = vec2(0.0f, 0.0f);
		}
	}
	if(pMouseSelection->m_Selecting)
	{
		pMouseSelection->m_ReleaseMouse = MousePos();
		if(!MouseButton(0))
		{
			pMouseSelection->m_Selecting = false;
			if(Active)
				Input()->EnsureScreenKeyboardShown();
		}
	}

	if(RenderOptions.m_DrawBackground)
		DrawRoundedSurface(this, *pRect, ResolveConfiguredInputSurface(), ColorRGBA(), ui_token::radius::TIGHT);
	ClipEnable(pRect);
	pLineInput->Render(&Textbox, FontSize, TextAlign, Changed || CursorChanged, LineWidth, LineSpacing);
	ClipDisable();

	pLineInput->SetScrollOffset(0.0f);
	pLineInput->SetScrollOffsetChange(0.0f);

	return Changed;
}

bool CUi::DoClearableEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners, const std::vector<STextColorSplit> &vColorSplits)
{
	return DoClearableEditBox(pLineInput, pRect, FontSize, Corners, vColorSplits, {});
}

bool CUi::DoClearableEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, int Corners, const std::vector<STextColorSplit> &vColorSplits, const SEditBoxRenderOptions &RenderOptions)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(this);
	const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();
	const ColorRGBA PreviousTextOutlineColor = TextRender()->GetTextOutlineColor();
	const ColorRGBA PreviousTextSelectionColor = TextRender()->GetTextSelectionColor();
	const unsigned PreviousRenderFlags = TextRender()->GetRenderFlags();
	const EFontPreset PreviousFontPreset = TextRender()->GetFontPreset();

	const float EditBoxRounding = ui_token::radius::BASE;
	CUIRect EditBox, ClearButton;
	pRect->VSplitRight(pRect->h, &EditBox, &ClearButton);

	bool ReturnValue = DoEditBox(pLineInput, &EditBox, FontSize, Corners & ~IGraphics::CORNER_R, vColorSplits, TEXTALIGN_ML, RenderOptions);

	DrawRoundedSurface(this, ClearButton, ScaleBackgroundAlpha(ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f * ButtonColorMul(pLineInput->GetClearButtonId()))), ColorRGBA(), EditBoxRounding, 0.0f, Corners & ~IGraphics::CORNER_L);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	DoLabel(&ClearButton, "×", ClearButton.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(PreviousRenderFlags);
	TextRender()->SetFontPreset(PreviousFontPreset);
	TextRender()->TextOutlineColor(PreviousTextOutlineColor);
	TextRender()->TextSelectionColor(PreviousTextSelectionColor);
	TextRender()->TextColor(PreviousTextColor);
	if(DoButtonLogic(pLineInput->GetClearButtonId(), 0, &ClearButton, BUTTONFLAG_LEFT))
	{
		CUiScopedGaussianBlurSuppression GaussianBlurSuppression(this);
		ClearButton.Draw(ScaleBackgroundAlpha(ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f * ButtonColorMul(pLineInput->GetClearButtonId()))), Corners & ~IGraphics::CORNER_L, EditBoxRounding);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		DoLabel(&ClearButton, "×", ClearButton.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_MC);
		TextRender()->SetRenderFlags(0);
		if(DoButtonLogic(pLineInput->GetClearButtonId(), 0, &ClearButton, BUTTONFLAG_LEFT))
		{
			pLineInput->Clear();
			SetActiveItem(pLineInput);
			ReturnValue = true;
		}
	}

	return ReturnValue;
}

bool CUi::DoEditBox_Search(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, bool HotkeyEnabled)
{
	return DoEditBox_Search(pLineInput, pRect, FontSize, HotkeyEnabled, {});
}

bool CUi::DoEditBox_Search(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, bool HotkeyEnabled, const SEditBoxRenderOptions &RenderOptions)
{
	const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();
	const ColorRGBA PreviousTextOutlineColor = TextRender()->GetTextOutlineColor();
	const ColorRGBA PreviousTextSelectionColor = TextRender()->GetTextSelectionColor();
	const unsigned PreviousRenderFlags = TextRender()->GetRenderFlags();
	const EFontPreset PreviousFontPreset = TextRender()->GetFontPreset();

	CUIRect QuickSearch = *pRect;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	DoLabel_QmIcon(&QuickSearch, EQmIcon::SEARCH, FONT_ICON_MAGNIFYING_GLASS, FontSize, TEXTALIGN_ML);
	const float SearchWidth = TextRender()->TextWidth(FontSize, FONT_ICON_MAGNIFYING_GLASS);
	TextRender()->SetRenderFlags(PreviousRenderFlags);
	TextRender()->SetFontPreset(PreviousFontPreset);
	TextRender()->TextOutlineColor(PreviousTextOutlineColor);
	TextRender()->TextSelectionColor(PreviousTextSelectionColor);
	TextRender()->TextColor(PreviousTextColor);
	QuickSearch.VSplitLeft(SearchWidth + 5.0f, nullptr, &QuickSearch);
	if(HotkeyEnabled && Input()->ModifierIsPressed() && Input()->KeyPress(KEY_F))
	{
		SetActiveItem(pLineInput);
		pLineInput->SelectAll();
	}
	pLineInput->SetEmptyText(Localize("Search"));
	return DoClearableEditBox(pLineInput, &QuickSearch, FontSize, IGraphics::CORNER_ALL, {}, RenderOptions);
}

int CUi::DoButton_Menu(CUIElement &UIElement, const CButtonContainer *pId, const std::function<const char *()> &GetTextLambda, const CUIRect *pRect, const SMenuButtonProperties &Props)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(this);
	const bool Enabled = Props.m_Enabled;
	const ColorRGBA BaseSurface = ResolveConfiguredControlSurface(Enabled);
	const bool UseRoundedRectSdf = Graphics()->HasRoundedRectSdf();
	CUIRect Text = *pRect, DropDownIcon;
	Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f, &Text);
	Text.HMargin((Text.h * Props.m_FontFactor) / 2.0f, &Text);
	const float FontSize = Props.m_FontSize > 0.0f ? Props.m_FontSize : Text.h * CUi::ms_FontmodHeight;
	if(Props.m_ShowDropDownIcon)
	{
		Text.VSplitRight(pRect->h * 0.25f, &Text, nullptr);
		Text.VSplitRight(pRect->h * 0.75f, &Text, &DropDownIcon);
	}

	if(!UIElement.AreRectsInit() || Props.m_HintRequiresStringCheck || Props.m_HintCanChangePositionOrSize || !UIElement.Rect(0)->m_UITextContainer.Valid() || (UseRoundedRectSdf ? UIElement.Rect(0)->m_UIRectQuadContainer != -1 : UIElement.Rect(0)->m_UIRectQuadContainer == -1))
	{
		bool NeedsRecalc = !UIElement.AreRectsInit() || !UIElement.Rect(0)->m_UITextContainer.Valid() || (UseRoundedRectSdf ? UIElement.Rect(0)->m_UIRectQuadContainer != -1 : UIElement.Rect(0)->m_UIRectQuadContainer == -1);
		if(UIElement.AreRectsInit())
		{
			for(int i = 0; i < 3; ++i)
			{
				if(UIElement.Rect(i)->m_QuadColor != BaseSurface)
					NeedsRecalc = true;
			}
		}
		if(Props.m_HintCanChangePositionOrSize)
		{
			if(UIElement.AreRectsInit())
			{
				if(UIElement.Rect(0)->m_X != pRect->x || UIElement.Rect(0)->m_Y != pRect->y || UIElement.Rect(0)->m_Width != pRect->w || UIElement.Rect(0)->m_Height != pRect->h || UIElement.Rect(0)->m_Rounding != Props.m_Rounding || UIElement.Rect(0)->m_Corners != Props.m_Corners || UIElement.Rect(0)->m_BackgroundAlphaScale != m_BackgroundAlphaScale || UIElement.Rect(0)->m_FontSize != FontSize)
				{
					NeedsRecalc = true;
				}
			}
		}
		const char *pText = nullptr;
		if(Props.m_HintRequiresStringCheck)
		{
			if(UIElement.AreRectsInit())
			{
				pText = GetTextLambda();
				if(str_comp(UIElement.Rect(0)->m_Text.c_str(), pText) != 0)
				{
					NeedsRecalc = true;
				}
			}
		}
		if(NeedsRecalc)
		{
			if(!UIElement.AreRectsInit())
			{
				UIElement.InitRects(3);
			}
			ResetUIElement(UIElement);

			for(int i = 0; i < 3; ++i)
			{
				const ColorRGBA Color = BaseSurface;
				CUIElement::SUIElementRect &NewRect = *UIElement.Rect(i);
				NewRect.m_QuadColor = Color;
				if(!UseRoundedRectSdf)
				{
					Graphics()->SetColor(Color);
					NewRect.m_UIRectQuadContainer = Graphics()->CreateRectQuadContainer(pRect->x, pRect->y, pRect->w, pRect->h, Props.m_Rounding, Props.m_Corners);
				}

				NewRect.m_X = pRect->x;
				NewRect.m_Y = pRect->y;
				NewRect.m_Width = pRect->w;
				NewRect.m_Height = pRect->h;
				NewRect.m_Rounding = Props.m_Rounding;
				NewRect.m_Corners = Props.m_Corners;
				NewRect.m_BackgroundAlphaScale = m_BackgroundAlphaScale;
				if(i == 0)
				{
					if(pText == nullptr)
						pText = GetTextLambda();
					NewRect.m_Text = pText;
					if(Props.m_UseIconFont)
						TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					DoLabel(NewRect, &Text, pText, FontSize, TEXTALIGN_MC);
					if(Props.m_UseIconFont)
						TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
				}
			}
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}
	// render
	size_t Index = 2;
	if(Enabled && CheckActiveItem(pId))
		Index = 0;
	else if(Enabled && HotItem() == pId)
		Index = 1;
	CUiScopedSurfaceText StateSurfaceText(TextRender(), BaseSurface);
	if(UseRoundedRectSdf)
		DrawRoundedSurface(this, *pRect, BaseSurface, ColorRGBA(), Props.m_Rounding, 0.0f, Props.m_Corners);
	else
	{
		Graphics()->TextureClear();
		Graphics()->RenderQuadContainer(UIElement.Rect(Index)->m_UIRectQuadContainer, -1);
	}
	if(Props.m_ShowDropDownIcon)
	{
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		DoLabel_QmIcon(&DropDownIcon, EQmIcon::CIRCLE_CHEVRON_DOWN, FONT_ICON_CIRCLE_CHEVRON_DOWN, DropDownIcon.h * CUi::ms_FontmodHeight, TEXTALIGN_MR);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	ColorRGBA ColorText(TextRender()->GetTextColor());
	ColorRGBA ColorTextOutline(TextRender()->GetTextOutlineColor());
	if(!Enabled)
	{
		ColorText.a *= 0.65f;
		ColorTextOutline.a *= 0.65f;
	}
	if(UIElement.Rect(0)->m_UITextContainer.Valid())
		FlushQuadBatch();
	if(UIElement.Rect(0)->m_UITextContainer.Valid())
	{
		const CUIElement::SUIElementRect &Label = *UIElement.Rect(0);
		{
			const EQmIcon Icon = Label.m_NumQmIcons == 1 ? Label.m_aQmIcons[0] : EQmIcon::COUNT;
			const ColorRGBA LabelColor = Label.m_NumQmIcons > 0 ? ConfiguredQmUiIconColor(ColorText, Icon) : ColorText;
			const CQmIconSemanticColorScope SemanticColorScope(QmUiIconHasSemanticColor(Icon));
			const ColorRGBA LabelOutlineColor = Label.m_NumQmIcons > 0 ? ConfiguredQmUiIconContrastColor(LabelColor) : ColorTextOutline;
			TextRender()->RenderTextContainer(Label.m_UITextContainer, LabelColor, LabelOutlineColor);
			CQmIconDrawDiagnostics::Record(Label.m_NumQmIcons);
		}
	}
	if(!Enabled)
		return 0;
	return DoButtonLogic(pId, Props.m_Checked, pRect, Props.m_Flags);
}

void CUi::DrawButton_FontIcon(const char *pText, const CUIRect *pRect, ColorRGBA Color, int Corners, bool Enabled)
{
	const CUIRect ButtonRect = QmUiSquareIconButtonRect(*pRect);
	pRect = &ButtonRect;
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(this);
	DrawRoundedSurface(this, *pRect, Color, ColorRGBA(), 5.0f, 0.0f, Corners);

	CUiScopedSurfaceText SurfaceText(TextRender(), Color);
	const ColorRGBA PreviousColor = TextRender()->GetTextColor();
	const ColorRGBA PreviousOutlineColor = TextRender()->GetTextOutlineColor();
	const unsigned PreviousFlags = TextRender()->GetRenderFlags();
	const EFontPreset PreviousPreset = TextRender()->GetFontPreset();
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);

	TextRender()->TextColor(ResolveUiSurfaceForeground(SurfaceText.Surface()).WithAlpha(TextRender()->GetTextColor().a));

	CUIRect Label;
	pRect->HMargin(2.0f, &Label);
	DoLabel(&Label, pText, Label.h * ms_FontmodHeight, TEXTALIGN_MC);

	if(!Enabled)
	{
		const CQmIconSemanticColorScope SemanticColorScope;
		TextRender()->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
		DoLabel_QmIcon(&Label, EQmIcon::SLASH, FONT_ICON_SLASH, Label.h * ms_FontmodHeight, TEXTALIGN_MC);
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	TextRender()->SetRenderFlags(PreviousFlags);
	TextRender()->SetFontPreset(PreviousPreset);
	TextRender()->TextOutlineColor(PreviousOutlineColor);
	TextRender()->TextColor(PreviousColor);
	(void)Enabled;
}

int CUi::DoButton_FontIcon(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, const unsigned Flags, int Corners, bool Enabled, const std::optional<ColorRGBA> ButtonColor)
{
	const CUIRect ButtonRect = QmUiSquareIconButtonRect(*pRect);
	pRect = &ButtonRect;
	const ColorRGBA Fill = ResolveConfiguredIconButtonSurface(ButtonColor, Enabled);
	DrawButton_FontIcon(pText, pRect, Fill, Corners, Enabled);
	const ColorRGBA Surface = CompositeUiSurface(Fill, CUiScopedSurfaceText::CurrentSurface());
	const ColorRGBA Feedback = ResolveUiIconButtonFeedback(Surface, Enabled, MouseHovered(pRect), CheckActiveItem(pButtonContainer) && MouseButton(0));
	DrawRoundedSurface(this, *pRect, Feedback, Feedback.WithAlpha(Feedback.a > 0.0f ? ui_token::feedback::ICON_BORDER_ALPHA : 0.0f), 5.0f, ui_token::feedback::ICON_BORDER_WIDTH, Corners);

	return Enabled ? DoButtonLogic(pButtonContainer, Checked, pRect, Flags) : 0;
}

bool CUi::DrawQmIcon(const CUIRect &Rect, EQmIcon Icon, const char *pFallbackIcon, const ColorRGBA &Color) const
{
	if(pFallbackIcon == nullptr || pFallbackIcon[0] == '\0')
		return false;

	ITextRender *pTextRender = TextRender();
	const ColorRGBA PreviousColor = pTextRender->GetTextColor();
	const unsigned PreviousFlags = pTextRender->GetRenderFlags();
	const EFontPreset PreviousPreset = pTextRender->GetFontPreset();
	pTextRender->TextColor(ConfiguredQmUiIconColor(Color, Icon));
	const CQmIconSemanticColorScope SemanticColorScope(QmUiIconHasSemanticColor(Icon));
	pTextRender->SetFontPreset(EFontPreset::ICON_FONT);
	pTextRender->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	DoLabel(&Rect, pFallbackIcon, QmIconFontSize(Rect), TEXTALIGN_MC);
	pTextRender->SetRenderFlags(PreviousFlags);
	pTextRender->SetFontPreset(PreviousPreset);
	pTextRender->TextColor(PreviousColor);
	return true;
}

CLabelResult CUi::DoLabel_QmIcon(const CUIRect *pRect, EQmIcon Icon, const char *pFallbackIcon, float Size, int Align, const SLabelProperties &LabelProps) const
{
	const CUIRect IconRect = QmIconLabelRect(*pRect, Size, Align);

	if(DrawQmIcon(IconRect, Icon, pFallbackIcon, TextRender()->GetTextColor()))
		return CLabelResult{};

	return DoLabel(pRect, pFallbackIcon, Size, Align, LabelProps);
}

int CUi::DoButton_QmIcon(CButtonContainer *pButtonContainer, EQmIcon Icon, const char *pFallbackIcon, int Checked, const CUIRect *pRect, const unsigned Flags, int Corners, bool Enabled, const std::optional<ColorRGBA> ButtonColor)
{
	const CUIRect ButtonRect = QmUiSquareIconButtonRect(*pRect);
	pRect = &ButtonRect;
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(this);
	const ColorRGBA Fill = ResolveConfiguredIconButtonSurface(ButtonColor, Enabled);
	DrawRoundedSurface(this, *pRect, Fill, ColorRGBA(), 5.0f, 0.0f, Corners);
	const ColorRGBA Feedback = ResolveUiIconButtonFeedback(CompositeUiSurface(Fill, CUiScopedSurfaceText::CurrentSurface()), Enabled, MouseHovered(pRect), CheckActiveItem(pButtonContainer) && MouseButton(0));
	DrawRoundedSurface(this, *pRect, Feedback, Feedback.WithAlpha(Feedback.a > 0.0f ? ui_token::feedback::ICON_BORDER_ALPHA : 0.0f), 5.0f, ui_token::feedback::ICON_BORDER_WIDTH, Corners);
	CUiScopedSurfaceText SurfaceText(TextRender(), Fill);
	const ColorRGBA PreviousOutlineColor = TextRender()->GetTextOutlineColor();

	CUIRect Label;
	pRect->HMargin(2.0f, &Label);
	const float IconSide = std::min(Label.w, Label.h);
	CUIRect IconRect;
	IconRect.x = Label.x + (Label.w - IconSide) * 0.5f;
	IconRect.y = Label.y + (Label.h - IconSide) * 0.5f;
	IconRect.w = IconSide;
	IconRect.h = IconSide;
	DrawQmIcon(IconRect, Icon, pFallbackIcon, ResolveUiSurfaceForeground(SurfaceText.Surface()).WithAlpha(TextRender()->GetTextColor().a));

	if(!Enabled)
	{
		// 与 DrawButton_FontIcon 保持一致：禁用时叠加红色斜杠。
		const CQmIconSemanticColorScope SemanticColorScope;
		DrawQmIcon(IconRect, EQmIcon::SLASH, FONT_ICON_SLASH, ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
	}
	TextRender()->TextOutlineColor(PreviousOutlineColor);

	return Enabled ? DoButtonLogic(pButtonContainer, Checked, pRect, Flags) : 0;
}

int CUi::DoButton_PopupMenu(CButtonContainer *pButtonContainer, const char *pText, const CUIRect *pRect, float Size, int Align, float Padding, bool TransparentInactive, bool Enabled, const std::optional<ColorRGBA> ButtonColor, float MinimumFontSize)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(this);
	const bool DrawBackground = ButtonColor.has_value() || !TransparentInactive || CheckActiveItem(pButtonContainer) || HotItem() == pButtonContainer;
	const ColorRGBA Fill = ButtonColor.value_or(ResolveConfiguredControlSurface(Enabled));
	CUiScopedSurfaceText SurfaceText(TextRender(), DrawBackground ? Fill : ColorRGBA());
	if(DrawBackground)
		DrawRoundedSurface(this, *pRect, Fill, ColorRGBA(), ui_token::radius::BASE);

	CUIRect Label;
	pRect->Margin(Padding, &Label);
	if(MinimumFontSize > 0.0f)
	{
		SLabelProperties Props;
		Props.m_MaxWidth = Label.w;
		Props.m_MinimumFontSize = MinimumFontSize;
		Props.m_EllipsisAtEnd = true;
		DoLabel(&Label, pText, Size, Align, Props);
	}
	else
		DoLabel(&Label, pText, Size, Align);

	return Enabled ? DoButtonLogic(pButtonContainer, 0, pRect, BUTTONFLAG_LEFT) : 0;
}

int64_t CUi::DoValueSelector(const void *pId, const CUIRect *pRect, const char *pLabel, int64_t Current, int64_t Min, int64_t Max, const SValueSelectorProperties &Props)
{
	return DoValueSelectorWithState(pId, pRect, pLabel, Current, Min, Max, Props).m_Value;
}

SEditResult<int64_t> CUi::DoValueSelectorWithState(const void *pId, const CUIRect *pRect, const char *pLabel, int64_t Current, int64_t Min, int64_t Max, const SValueSelectorProperties &Props)
{
	CUiScopedSurfaceText SurfaceText(TextRender(), ResolveConfiguredInputSurface());
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(this);
	// logic
	const bool Inside = MouseInside(pRect);
	const int Base = Props.m_IsHex ? 16 : 10;
	auto RenderValueSelectorDisplay = [&](bool RenderText = true) {
		CUIRect Textbox;
		pRect->VMargin(2.0f, &Textbox);
		char aBuf[128];
		if(Props.m_pfnFormatValue != nullptr)
		{
			char aValueBuf[64];
			Props.m_pfnFormatValue(Current, aValueBuf, sizeof(aValueBuf), Base, Props.m_HexPrefix);
			if(pLabel[0] != '\0')
				str_format(aBuf, sizeof(aBuf), "%s %s", pLabel, aValueBuf);
			else
				str_copy(aBuf, aValueBuf);
		}
		else if(pLabel[0] != '\0')
		{
			if(Props.m_IsHex)
				str_format(aBuf, sizeof(aBuf), "%s #%0*" PRIX64, pLabel, Props.m_HexPrefix, Current);
			else
				str_format(aBuf, sizeof(aBuf), "%s %" PRId64, pLabel, Current);
		}
		else if(Props.m_IsHex)
			str_format(aBuf, sizeof(aBuf), "#%0*" PRIX64, Props.m_HexPrefix, Current);
		else
			str_format(aBuf, sizeof(aBuf), "%" PRId64, Current);
		DrawRoundedSurface(this, *pRect, ResolveConfiguredInputSurface(), ColorRGBA(), ui_token::radius::BASE);
		SLabelProperties ValueLabelProps;
		ValueLabelProps.m_MaxWidth = Textbox.w;
		ValueLabelProps.m_DisallowNewline = true;
		ValueLabelProps.m_StopAtEnd = true;
		if(RenderText)
		{
			const char *pDisplayText = m_ActiveValueSelectorState.m_pLastTextId == pId ? m_ActiveValueSelectorState.m_NumberInput.GetDisplayedString() : aBuf;
			const float ValueFontSize = QmFitSingleLineFontSize(10.0f, 6.0f, TextRender()->TextWidth(10.0f, pDisplayText), Textbox.w);
			ValueLabelProps.m_MinimumFontSize = ValueFontSize;
			DoLabel(&Textbox, pDisplayText, ValueFontSize, Props.m_TextAlign, ValueLabelProps);
		}
	};

	if(CheckActiveItem(pId))
	{
		if(m_ActiveValueSelectorState.m_Button < 0)
		{
			DisableMouseLock();
			SetActiveItem(nullptr);
		}
		else if(!MouseButton(m_ActiveValueSelectorState.m_Button))
		{
			DisableMouseLock();
			SetActiveItem(nullptr);
			if(Inside && ((m_ActiveValueSelectorState.m_Button == 0 && !m_ActiveValueSelectorState.m_DidScroll) || m_ActiveValueSelectorState.m_Button == 1))
			{
				m_ActiveValueSelectorState.m_pLastTextId = pId;
				if(Props.m_pfnFormatValue != nullptr)
				{
					char aEditBuf[64];
					Props.m_pfnFormatValue(Current, aEditBuf, sizeof(aEditBuf), Base, Props.m_HexPrefix);
					m_ActiveValueSelectorState.m_NumberInput.Set(aEditBuf);
				}
				else
				{
					m_ActiveValueSelectorState.m_NumberInput.SetInteger64(Current, Base, Props.m_HexPrefix);
				}
				if(Props.m_SelectAllOnActivate)
				{
					m_ActiveValueSelectorState.m_NumberInput.SelectAll();
				}
				else
				{
					m_ActiveValueSelectorState.m_NumberInput.SetCursorOffset(m_ActiveValueSelectorState.m_NumberInput.GetLength());
					m_ActiveValueSelectorState.m_NumberInput.SelectNothing();
				}
			}
			m_ActiveValueSelectorState.m_Button = -1;
		}
	}

	if(m_ActiveValueSelectorState.m_pLastTextId == pId)
	{
		SetActiveItem(&m_ActiveValueSelectorState.m_NumberInput);
		m_ActiveValueSelectorState.m_NumberInput.Activate(EInputPriority::UI);
		// 编辑态同步鼠标选择状态到行输入：CLineInput::Render 只在
		// m_MouseSelection.m_Selecting 时按鼠标位置反算光标/选区，不同步的
		// 话点击文本之间无法把插入条放到对应位置（DoEditBox 同款机制）。
		// 值选择器没有水平滚动，偏移恒为 0。
		CLineInput::SMouseSelection *pMouseSelection = m_ActiveValueSelectorState.m_NumberInput.GetMouseSelection();
		if(Inside)
		{
			if(!pMouseSelection->m_Selecting && MouseButtonClicked(0))
			{
				pMouseSelection->m_Selecting = true;
				pMouseSelection->m_PressMouse = MousePos();
				pMouseSelection->m_Offset.x = 0.0f;
			}
		}
		if(pMouseSelection->m_Selecting)
		{
			pMouseSelection->m_ReleaseMouse = MousePos();
			if(!MouseButton(0))
			{
				pMouseSelection->m_Selecting = false;
				Input()->EnsureScreenKeyboardShown();
			}
		}
		RenderValueSelectorDisplay(false);
		const ColorRGBA PreviousTextColor = TextRender()->GetTextColor();
		const char *pEditText = m_ActiveValueSelectorState.m_NumberInput.GetDisplayedString();
		CUIRect Textbox;
		pRect->VMargin(2.0f, &Textbox);
		const float EditFontSize = QmFitSingleLineFontSize(10.0f, 6.0f, TextRender()->TextWidth(10.0f, pEditText), Textbox.w);
		m_ActiveValueSelectorState.m_NumberInput.Render(&Textbox, EditFontSize, Props.m_TextAlign, false, -1.0f, 0.0f, {});
		TextRender()->TextColor(PreviousTextColor);

		if(Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER) || ConsumeHotkey(HOTKEY_ENTER) || ((MouseButtonClicked(1) || MouseButtonClicked(0)) && !Inside))
		{
			int64_t ParsedValue = 0;
			if(Props.m_pfnParseValue != nullptr)
			{
				if(Props.m_pfnParseValue(m_ActiveValueSelectorState.m_NumberInput.GetString(), ParsedValue, Base))
					Current = std::clamp(ParsedValue, Min, Max);
			}
			else
				Current = std::clamp(m_ActiveValueSelectorState.m_NumberInput.GetInteger64(Base), Min, Max);
			DisableMouseLock();
			ReleaseActiveTextInput(&m_ActiveValueSelectorState.m_NumberInput);
			m_ActiveValueSelectorState.m_pLastTextId = nullptr;
		}

		if(ConsumeHotkey(HOTKEY_ESCAPE))
		{
			DisableMouseLock();
			ReleaseActiveTextInput(&m_ActiveValueSelectorState.m_NumberInput);
			m_ActiveValueSelectorState.m_pLastTextId = nullptr;
		}
	}
	else
	{
		if(CheckActiveItem(pId))
		{
			dbg_assert(m_ActiveValueSelectorState.m_Button >= 0, "m_ActiveValueSelectorState.m_Button invalid");
			if(Props.m_UseScroll && m_ActiveValueSelectorState.m_Button == 0 && MouseButton(0))
			{
				m_ActiveValueSelectorState.m_ScrollValue += MouseDeltaX() * (Input()->ShiftIsPressed() ? 0.05f : 1.0f);

				if(absolute(m_ActiveValueSelectorState.m_ScrollValue) > Props.m_Scale)
				{
					const int64_t Count = (int64_t)(m_ActiveValueSelectorState.m_ScrollValue / Props.m_Scale);
					m_ActiveValueSelectorState.m_ScrollValue = std::fmod(m_ActiveValueSelectorState.m_ScrollValue, Props.m_Scale);
					Current += Props.m_Step * Count;
					Current = std::clamp(Current, Min, Max);
					m_ActiveValueSelectorState.m_DidScroll = true;

					// Constrain to discrete steps
					if(Count > 0)
						Current = Current / Props.m_Step * Props.m_Step;
					else
						Current = std::ceil(Current / (float)Props.m_Step) * Props.m_Step;
				}
			}
		}
		else if(HotItem() == pId)
		{
			if(MouseButton(0))
			{
				m_ActiveValueSelectorState.m_Button = 0;
				m_ActiveValueSelectorState.m_DidScroll = false;
				m_ActiveValueSelectorState.m_ScrollValue = 0.0f;
				SetActiveItem(pId);
				if(Props.m_UseScroll)
					EnableMouseLock(pId);
			}
			else if(MouseButton(1))
			{
				m_ActiveValueSelectorState.m_Button = 1;
				SetActiveItem(pId);
			}
		}
	}

	if(m_ActiveValueSelectorState.m_pLastTextId != pId)
		RenderValueSelectorDisplay();

	if(Inside && !MouseButton(0) && !MouseButton(1))
		SetHotItem(pId);

	EEditState State = EEditState::NONE;
	if(m_pLastEditingItem == pId)
	{
		State = EEditState::EDITING;
	}
	if(((CheckActiveItem(pId) && CheckMouseLock()) || m_ActiveValueSelectorState.m_pLastTextId == pId) && m_pLastEditingItem != pId)
	{
		State = EEditState::START;
		m_pLastEditingItem = pId;
	}
	if(!CheckMouseLock() && m_ActiveValueSelectorState.m_pLastTextId != pId && m_pLastEditingItem == pId)
	{
		State = EEditState::END;
		m_pLastEditingItem = nullptr;
	}

	return SEditResult<int64_t>{State, Current};
}

float CUi::DoScrollbarV(const void *pId, const CUIRect *pRect, float Current)
{
	Current = std::clamp(Current, 0.0f, 1.0f);

	// layout
	CUIRect Rail;
	pRect->Margin(5.0f, &Rail);

	CUIRect Handle;
	Rail.HSplitTop(std::clamp(33.0f, Rail.w, Rail.h / 3.0f), &Handle, nullptr);
	Handle.y = Rail.y + (Rail.h - Handle.h) * Current;

	// logic
	const bool InsideRail = MouseHovered(&Rail);
	const bool InsideHandle = MouseHovered(&Handle);
	bool Grabbed = false; // whether to apply the offset

	if(CheckActiveItem(pId))
	{
		if(MouseButton(0))
		{
			Grabbed = true;
			if(Input()->ShiftIsPressed())
				m_MouseSlow = true;
		}
		else
		{
			SetActiveItem(nullptr);
		}
	}
	else if(HotItem() == pId)
	{
		if(InsideHandle)
		{
			if(MouseButton(0))
			{
				SetActiveItem(pId);
				m_ActiveScrollbarOffset = MouseY() - Handle.y;
				Grabbed = true;
			}
		}
		else if(MouseButtonClicked(0))
		{
			SetActiveItem(pId);
			m_ActiveScrollbarOffset = Handle.h / 2.0f;
			Grabbed = true;
		}
	}

	if(InsideRail && !MouseButton(0))
	{
		SetHotItem(pId);
	}

	float ReturnValue = Current;
	if(Grabbed)
	{
		const float Min = Rail.y;
		const float Max = Rail.h - Handle.h;
		const float Cur = MouseY() - m_ActiveScrollbarOffset;
		ReturnValue = std::clamp((Cur - Min) / Max, 0.0f, 1.0f);
	}

	// render
	DrawRoundedSurface(this, Rail, ScaleBackgroundAlpha(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)), ColorRGBA(), Rail.w / 2.0f);
	DrawRoundedSurface(this, Handle, ScaleBackgroundAlpha(ms_ScrollBarColorFunction.GetColor(CheckActiveItem(pId), HotItem() == pId)), ColorRGBA(), Handle.w / 2.0f);

	return ReturnValue;
}

void CUi::RenderScrollbarH(const void *pId, const CUIRect *pRect, float Current, const ColorRGBA *pColorInner)
{
	Current = std::clamp(Current, 0.0f, 1.0f);

	// layout
	CUIRect Rail = *pRect;
	const float HandleSize = std::clamp(Rail.h * 0.75f, 8.0f, 16.0f);
	CUIRect Handle;
	Rail.VSplitLeft(HandleSize, &Handle, nullptr);
	Handle.h = HandleSize;
	Handle.y = Rail.y + (Rail.h - Handle.h) * 0.5f;
	Handle.x += (Rail.w - Handle.w) * Current;

	const ColorRGBA HandleColor = ms_ScrollBarColorFunction.GetColor(CheckActiveItem(pId), HotItem() == pId);
	CUIRect VisualRail = Rail;
	VisualRail.VMargin(HandleSize * 0.5f, &VisualRail);
	VisualRail.h = std::clamp(pRect->h * 0.12f, 2.0f, 3.0f);
	VisualRail.y = pRect->y + (pRect->h - VisualRail.h) * 0.5f;
	VisualRail.Draw(ScaleBackgroundAlpha(ColorRGBA(1.0f, 1.0f, 1.0f, 0.28f)), IGraphics::CORNER_ALL, VisualRail.h * 0.5f);
	if(pColorInner)
	{
		Handle.Draw(ScaleBackgroundAlpha(ColorRGBA(0.08f, 0.08f, 0.08f, 0.75f)), IGraphics::CORNER_ALL, HandleSize * 0.5f);
		CUIRect InnerHandle;
		Handle.Margin(2.0f, &InnerHandle);
		InnerHandle.Draw(ScaleBackgroundAlpha(pColorInner->Multiply(HandleColor)), IGraphics::CORNER_ALL, InnerHandle.h * 0.5f);
	}
	else
	{
		Handle.Draw(ScaleBackgroundAlpha(HandleColor), IGraphics::CORNER_ALL, HandleSize * 0.5f);
	}
}

float CUi::DoScrollbarH(const void *pId, const CUIRect *pRect, float Current, const ColorRGBA *pColorInner)
{
	Current = std::clamp(Current, 0.0f, 1.0f);

	// layout
	CUIRect Rail = *pRect;
	const float HandleSize = std::clamp(Rail.h * 0.75f, 8.0f, 16.0f);
	CUIRect Handle;
	Rail.VSplitLeft(HandleSize, &Handle, nullptr);
	Handle.h = HandleSize;
	Handle.y = Rail.y + (Rail.h - Handle.h) * 0.5f;
	Handle.x += (Rail.w - Handle.w) * Current;

	CUIRect HandleArea = Handle;
	HandleArea.h = pRect->h * 0.9f;
	HandleArea.y = pRect->y + pRect->h * 0.05f;
	HandleArea.w += 6.0f;
	HandleArea.x -= 3.0f;

	const bool InsideRail = MouseHovered(&Rail);
	const bool InsideHandle = MouseHovered(&HandleArea);
	bool Grabbed = false;
	if(CheckActiveItem(pId))
	{
		if(MouseButton(0))
		{
			Grabbed = true;
			if(Input()->ShiftIsPressed())
				m_MouseSlow = true;
		}
		else
		{
			SetActiveItem(nullptr);
		}
	}
	else if(HotItem() == pId)
	{
		if(InsideHandle)
		{
			if(MouseButton(0))
			{
				SetActiveItem(pId);
				m_pLastActiveScrollbar = pId;
				m_ActiveScrollbarOffset = MouseX() - Handle.x;
				Grabbed = true;
			}
		}
		else if(MouseButtonClicked(0))
		{
			SetActiveItem(pId);
			m_pLastActiveScrollbar = pId;
			m_ActiveScrollbarOffset = Handle.w / 2.0f;
			Grabbed = true;
		}
	}

	if(InsideRail && !MouseButton(0))
		SetHotItem(pId);

	float ReturnValue = Current;
	if(Grabbed)
	{
		const float Max = Rail.w - Handle.w;
		const float Cur = MouseX() - m_ActiveScrollbarOffset;
		ReturnValue = std::clamp((Cur - Rail.x) / Max, 0.0f, 1.0f);
	}

	RenderScrollbarH(pId, pRect, ReturnValue, pColorInner);

	return ReturnValue;
}

bool CUi::DoScrollbarOption(const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix, const char *pMaxText)
{
	const bool Infinite = Flags & CUi::SCROLLBAR_OPTION_INFINITE;
	const bool NoClampValue = Flags & CUi::SCROLLBAR_OPTION_NOCLAMPVALUE;
	const bool MultiLine = Flags & CUi::SCROLLBAR_OPTION_MULTILINE;
	const bool DelayUpdate = Flags & CUi::SCROLLBAR_OPTION_DELAYUPDATE;

	int PrevValue = (DelayUpdate && m_pLastActiveScrollbar == pId && CheckActiveItem(pId)) ? m_ScrollbarValue : *pOption;
	int Value = PrevValue;
	if(Infinite)
	{
		Max += 1;
		if(Value == 0)
			Value = Max;
	}

	// Allow adjustment of slider options when ctrl is pressed (to avoid scrolling, or accidentally adjusting the value)
	int Increment = std::max(1, (Max - Min) / 35);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && MouseInside(pRect))
	{
		Value += Increment;
		Value = std::clamp(Value, Min, Max);
	}
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && MouseInside(pRect))
	{
		Value -= Increment;
		Value = std::clamp(Value, Min, Max);
	}

	char aBuf[256];
	if(!Infinite || Value != Max)
	{
		if(pMaxText != nullptr && Value == Max)
			str_format(aBuf, sizeof(aBuf), "%s: %s", pStr, pMaxText);
		else
			str_format(aBuf, sizeof(aBuf), "%s: %i%s", pStr, Value, pSuffix);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "%s: ∞", pStr);
	}

	if(NoClampValue)
	{
		// clamp the value internally for the scrollbar
		Value = std::clamp(Value, Min, Max);
	}

	CUIRect Label, ScrollBar;
	if(MultiLine)
		pRect->HSplitMid(&Label, &ScrollBar);
	else
		pRect->VSplitMid(&Label, &ScrollBar, minimum(10.0f, pRect->w * 0.05f));

	const float FontSize = Label.h * CUi::ms_FontmodHeight * 0.8f;
	DoLabel(&Label, aBuf, FontSize, TEXTALIGN_ML);

	Value = pScale->ToAbsolute(DoScrollbarH(pId, &ScrollBar, pScale->ToRelative(Value, Min, Max)), Min, Max);
	if(NoClampValue && ((Value == Min && PrevValue < Min) || (Value == Max && PrevValue > Max)))
	{
		Value = PrevValue; // use previous out of range value instead if the scrollbar is at the edge
	}
	else if(Infinite)
	{
		if(Value == Max)
			Value = 0;
	}

	if(DelayUpdate && m_pLastActiveScrollbar == pId && CheckActiveItem(pId))
	{
		m_ScrollbarValue = Value;
		return false;
	}

	if(*pOption != Value)
	{
		*pOption = Value;
		return true;
	}
	return false;
}

void CUi::RenderProgressBar(CUIRect ProgressBar, float Progress)
{
	const float Rounding = minimum(ui_token::radius::TIGHT, ProgressBar.h / 2.0f);
	DrawRoundedSurface(this, ProgressBar, ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), ColorRGBA(), Rounding);
	ProgressBar.w = maximum(ProgressBar.w * Progress, 2 * Rounding);
	DrawRoundedSurface(this, ProgressBar, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f), ColorRGBA(), Rounding);
}

void CCachedText::Update(ITextRender *pTextRender, const char *pText, float FontSize, float LineWidth, int CursorFlags)
{
	// 非空缓存还必须属于当前图集，不能仅凭文本和布局参数跳过重建。
	if(m_FontSize == FontSize && m_LineWidth == LineWidth && m_CursorFlags == CursorFlags && m_Text == pText &&
		(m_TextContainerIndex.Valid() || (m_Text.empty() && m_TextContainerIndex.m_Index < 0)))
		return;

	pTextRender->DeleteTextContainer(m_TextContainerIndex);
	m_pTextContainerOwner = nullptr;

	m_Text = pText;
	m_FontSize = FontSize;
	m_LineWidth = LineWidth;
	m_CursorFlags = CursorFlags;

	CTextCursor Cursor;
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineWidth = LineWidth;
	Cursor.m_Flags = CursorFlags;

	// 颜色在渲染时应用，因此不能烘焙进 quad。
	const ColorRGBA OldColor = pTextRender->GetTextColor();
	pTextRender->TextColor(pTextRender->DefaultTextColor());
	pTextRender->CreateTextContainer(m_TextContainerIndex, &Cursor, m_Text.c_str());
	pTextRender->TextColor(OldColor);

	// 空文本不会留下容器，此时不记录所有者，析构时也就无需归还。
	if(m_TextContainerIndex.Valid())
		m_pTextContainerOwner = pTextRender;

	m_BoundingBox = Cursor.BoundingBox();
	m_MaxCharacterHeight = Cursor.m_MaxCharacterHeight;
}

void CCachedText::Render(ITextRender *pTextRender, vec2 Pos, ColorRGBA Color) const
{
	if(!m_TextContainerIndex.Valid())
		return;
	// quad 用默认颜色构建，因此描边需在此处随 alpha 淡出而非继承烘焙的顶点色。
	pTextRender->RenderTextContainer(m_TextContainerIndex, Color, pTextRender->DefaultTextOutlineColor().WithMultipliedAlpha(Color.a), Pos.x, Pos.y);
}

void CCachedText::Reset(ITextRender *pTextRender)
{
	pTextRender->DeleteTextContainer(m_TextContainerIndex);
	m_pTextContainerOwner = nullptr;
	m_Text.clear();
	m_FontSize = -1.0f;
	m_LineWidth = -1.0f;
	m_CursorFlags = 0;
	m_BoundingBox = {0.0f, 0.0f, 0.0f, 0.0f};
	m_MaxCharacterHeight = 0.0f;
}

CCachedText::~CCachedText()
{
	// 由 Update/Reset 传入的渲染器归还容器；渲染器生命周期长于所有 CCachedText
	// （客户端关闭时 CUi 会先清空缓存并停止使用文本渲染器）。
	if(m_pTextContainerOwner != nullptr)
		m_pTextContainerOwner->DeleteTextContainer(m_TextContainerIndex);
}

void CUi::RenderTime(CUIRect TimeRect, float FontSize, int Seconds, bool NotFinished, int Millis, bool TrueMilliseconds, CCachedText &SecondsText, CCachedText &MillisText, ColorRGBA Color) const
{
	if(NotFinished)
		return;

	char aBuf[128];
	str_time(absolute(static_cast<int64_t>(Seconds)) * 100, TIME_HOURS, aBuf, sizeof(aBuf));
	SecondsText.Update(TextRender(), aBuf, FontSize);

	// 垂直居中
	vec2 Cursor = TimeRect.TopLeft();
	const float SecondsWidth = std::min(SecondsText.Width(), TimeRect.w);
	Cursor.x += TimeRect.w - SecondsWidth; // 右对齐
	Cursor.y += ((TimeRect.h - SecondsText.MaxCharacterHeight()) / 2.0f - (FontSize - SecondsText.MaxCharacterHeight()));

	// 显示毫秒或百分秒（不足一小时时）
	if(Millis >= 0 && Seconds < 60 * 60)
	{
		constexpr float GoldenRatio = 0.61803398875f;
		const float CentisecondFontSize = FontSize * GoldenRatio;

		// 2 或 3 位数字
		char aMillis[4];
		Millis %= 1000;
		if(!TrueMilliseconds)
			str_format(aMillis, sizeof(aMillis), "%02d", (int)std::round(Millis / 10));
		else
			str_format(aMillis, sizeof(aMillis), "%03d", Millis);
		MillisText.Update(TextRender(), aMillis, CentisecondFontSize);

		const float MillisWidth = MillisText.Width();

		// 为毫秒腾出空间，但间距收紧 1/6 字符
		Cursor.x -= MillisWidth - (TrueMilliseconds ? MillisWidth / (3 * 6) : MillisWidth / (2 * 6));

		vec2 CursorMillis = TimeRect.TopLeft();
		CursorMillis.x += TimeRect.w - MillisWidth; // 右对齐
		CursorMillis.y += ((TimeRect.h - MillisText.MaxCharacterHeight()) / 2.0f - (CentisecondFontSize - MillisText.MaxCharacterHeight()));
		CursorMillis.y -= (CursorMillis.y - Cursor.y) * GoldenRatio;

		SecondsText.Render(TextRender(), Cursor, Color);
		MillisText.Render(TextRender(), CursorMillis, Color);
	}
	else
	{
		SecondsText.Render(TextRender(), Cursor, Color);
	}
}

void CUi::RenderProgressSpinner(vec2 Center, float OuterRadius, const SProgressSpinnerProperties &Props) const
{
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	// The filled and unfilled segments need to begin at the same angle offset
	// or the differences in pixel alignment will make the filled segments flicker.
	const float SegmentsAngle = 2.0f * pi / Props.m_Segments;
	const float InnerRadius = OuterRadius * 0.75f;
	const float AngleOffset = -0.5f * pi;
	Graphics()->SetColor(Props.m_Color.WithMultipliedAlpha(0.5f));
	for(int i = 0; i < Props.m_Segments; ++i)
	{
		const vec2 Dir1 = direction(AngleOffset + i * SegmentsAngle);
		const vec2 Dir2 = direction(AngleOffset + (i + 1) * SegmentsAngle);
		IGraphics::CFreeformItem Item = IGraphics::CFreeformItem(
			Center + Dir1 * InnerRadius, Center + Dir2 * InnerRadius,
			Center + Dir1 * OuterRadius, Center + Dir2 * OuterRadius);
		Graphics()->QuadsDrawFreeform(&Item, 1);
	}

	const float FilledRatio = Props.m_Progress < 0.0f ? 0.333f : Props.m_Progress;
	const int FilledSegmentOffset = Props.m_Progress < 0.0f ? round_to_int(m_ProgressSpinnerOffset * Props.m_Segments) : 0;
	const int FilledNumSegments = minimum<int>(Props.m_Segments * FilledRatio + (Props.m_Progress < 0.0f ? 0 : 1), Props.m_Segments);
	Graphics()->SetColor(Props.m_Color);
	for(int i = 0; i < FilledNumSegments; ++i)
	{
		const float Angle1 = AngleOffset + (i + FilledSegmentOffset) * SegmentsAngle;
		const float Angle2 = AngleOffset + ((i + 1 == FilledNumSegments && Props.m_Progress >= 0.0f) ? (2.0f * pi * Props.m_Progress) : ((i + FilledSegmentOffset + 1) * SegmentsAngle));
		IGraphics::CFreeformItem Item = IGraphics::CFreeformItem(
			Center.x + std::cos(Angle1) * InnerRadius, Center.y + std::sin(Angle1) * InnerRadius,
			Center.x + std::cos(Angle2) * InnerRadius, Center.y + std::sin(Angle2) * InnerRadius,
			Center.x + std::cos(Angle1) * OuterRadius, Center.y + std::sin(Angle1) * OuterRadius,
			Center.x + std::cos(Angle2) * OuterRadius, Center.y + std::sin(Angle2) * OuterRadius);
		Graphics()->QuadsDrawFreeform(&Item, 1);
	}

	Graphics()->QuadsEnd();
}

void CUi::DoBackButton()
{
	if(!g_Config.m_ClBackButton)
		return;

	MapScreen();
	const CUIRect *pScreen = Screen();
	const float Size = pScreen->h * 0.1f;
	constexpr float PositionScale = 1000000.0f;
	const auto ClampPos = [&](vec2 Pos) {
		Pos.x = std::clamp(Pos.x, 0.0f, pScreen->w - Size);
		Pos.y = std::clamp(Pos.y, 0.0f, pScreen->h - Size);
		return Pos;
	};

	vec2 ButtonPos = ClampPos({g_Config.m_ClBackButtonX / PositionScale * pScreen->w, g_Config.m_ClBackButtonY / PositionScale * pScreen->h});
	CUIRect ButtonRect{ButtonPos.x, ButtonPos.y, Size, Size};

	bool Clicked = false;
	bool Abrupted = false;
	const int Result = DoDraggableButtonLogic(&m_BackButtonId, 0, &ButtonRect, &Clicked, &Abrupted);

	// Detect the press transition. DoDraggableButtonLogic sets the active item on the
	// press frame but returns 0 there, so check CheckActiveItem to catch it.
	if(m_BackButtonOp == EBackButtonOp::NONE && CheckActiveItem(&m_BackButtonId))
	{
		m_BackButtonInitialMouse = MousePos();
		m_BackButtonDragOffset = ButtonPos - MousePos();
		m_BackButtonOp = EBackButtonOp::CLICKED;
		if(m_OnBackButtonPressedFunction)
			m_OnBackButtonPressedFunction();
	}

	if(m_BackButtonOp == EBackButtonOp::CLICKED && length(MousePos() - m_BackButtonInitialMouse) > 5.0f)
	{
		m_BackButtonOp = EBackButtonOp::DRAGGING;
	}

	if(m_BackButtonOp == EBackButtonOp::DRAGGING)
	{
		ButtonPos = ClampPos(MousePos() + m_BackButtonDragOffset);
		g_Config.m_ClBackButtonX = round_to_int(ButtonPos.x / pScreen->w * PositionScale);
		g_Config.m_ClBackButtonY = round_to_int(ButtonPos.y / pScreen->h * PositionScale);
		ButtonRect.x = ButtonPos.x;
		ButtonRect.y = ButtonPos.y;
	}

	if(Clicked || Abrupted)
	{
		if(Result && Clicked && m_BackButtonOp == EBackButtonOp::CLICKED && m_DispatchInputFunction)
		{
			IInput::CEvent Event;
			Event.m_Key = KEY_ESCAPE;
			Event.m_InputCount = 0;
			Event.m_aText[0] = '\0';
			Event.m_Flags = IInput::FLAG_PRESS;
			m_DispatchInputFunction(Event);
			Event.m_Flags = IInput::FLAG_RELEASE;
			m_DispatchInputFunction(Event);
		}
		m_BackButtonOp = EBackButtonOp::NONE;
	}

	m_BackButtonRect = ButtonRect;
}

void CUi::RenderBackButton()
{
	if(!g_Config.m_ClBackButton)
		return;

	MapScreen();

	// Override hot/active claims made by UI rendered between DoBackButton and RenderBackButton.
	if(m_BackButtonOp != EBackButtonOp::NONE)
		SetActiveItem(&m_BackButtonId);
	else if(MouseHovered(&m_BackButtonRect) && !MouseButton(0) && !MouseButton(1) && !MouseButton(2))
		SetHotItem(&m_BackButtonId);

	const bool Pressed = m_BackButtonOp != EBackButtonOp::NONE;
	const bool Hovered = !Pressed && HotItem() == &m_BackButtonId;
	const float Alpha = Pressed ? 0.9f : (Hovered ? 0.35f : 0.5f);
	m_BackButtonRect.Draw({0.0f, 0.0f, 0.0f, Alpha}, IGraphics::CORNER_ALL, 12.0f);

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH |
				     ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING |
				     ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	DoLabel_QmIcon(&m_BackButtonRect, EQmIcon::CHEVRON_LEFT, FONT_ICON_CHEVRON_LEFT, m_BackButtonRect.w * 0.5f, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}
