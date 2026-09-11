/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "ui.h"

#include "QmUi/QmDropdown.h"
#include "QmUi/QmUiPerf.h"
#include "QmUi/UiSurface.h"
#include "components/qmclient/perf_logging.h"
#include "qm_icon_manager.h"
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
	if(!g_Config.m_QmGaussianBlur && (m_GaussianBlurSource.IsValid() || m_GaussianBlurTemporary.IsValid() || m_GaussianBlurTarget.IsValid()))
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
	Graphics()->DestroyRenderTarget(&m_GaussianBlurTemporary);
	Graphics()->DestroyRenderTarget(&m_GaussianBlurTarget);
	m_GaussianBlurWidth = 0;
	m_GaussianBlurHeight = 0;
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
		if(m_GaussianBlurSource.IsValid() || m_GaussianBlurTemporary.IsValid() || m_GaussianBlurTarget.IsValid())
			DestroyGaussianBlurTargets();
		return false;
	}

	const int BlurWidth = UiGaussianBlurTargetDimension(Graphics()->ScreenWidth());
	const int BlurHeight = UiGaussianBlurTargetDimension(Graphics()->ScreenHeight());
	if(BlurWidth <= 0 || BlurHeight <= 0)
	{
		m_GaussianBlurPrepared = false;
		return false;
	}

	if(BlurWidth != m_GaussianBlurWidth || BlurHeight != m_GaussianBlurHeight || !m_GaussianBlurSource.IsValid() || !m_GaussianBlurTemporary.IsValid() || !m_GaussianBlurTarget.IsValid())
	{
		DestroyGaussianBlurTargets();
		m_GaussianBlurSource = Graphics()->CreateRenderTarget(BlurWidth, BlurHeight);
		m_GaussianBlurTemporary = Graphics()->CreateRenderTarget(BlurWidth, BlurHeight);
		m_GaussianBlurTarget = Graphics()->CreateRenderTarget(BlurWidth, BlurHeight);
		if(!m_GaussianBlurSource.IsValid() || !m_GaussianBlurTemporary.IsValid() || !m_GaussianBlurTarget.IsValid())
		{
			DestroyGaussianBlurTargets();
			return false;
		}
		m_GaussianBlurWidth = BlurWidth;
		m_GaussianBlurHeight = BlurHeight;
	}

	const uint64_t PerfFrame = Client()->PerfFrame();
	if(m_GaussianBlurPrepared && m_GaussianBlurPreparedFrame == PerfFrame)
		return true;

	FlushQuadBatch();
	Graphics()->FlushVertices();
	if(!Graphics()->CaptureBackbufferToRenderTarget(m_GaussianBlurSource))
	{
		m_GaussianBlurPrepared = false;
		return false;
	}

	IGraphics::SGaussianBlurParams BlurParams;
	BlurParams.m_Radius = 4;
	BlurParams.m_Sigma = 2.0f;
	if(!Graphics()->GaussianBlurRenderTarget(m_GaussianBlurSource, m_GaussianBlurTemporary, m_GaussianBlurTarget, BlurParams))
	{
		m_GaussianBlurPrepared = false;
		return false;
	}
	m_GaussianBlurPreparedFrame = PerfFrame;
	m_GaussianBlurPrepared = true;
	return true;
}

void CUi::RenderGaussianBlur(const CUIRect &Rect, float Alpha, int Corners, float Rounding)
{
	if(m_GaussianBlurSuppressionDepth > 0 || Rect.w <= 0.0f || Rect.h <= 0.0f || Alpha <= 0.0f || !PrepareGaussianBlur())
		return;

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
		return PopupMenu.m_Props.m_BlockUnderlyingPointerInput;
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
	bool PreLayoutCurrentFramePress = false;
	if(PreLayoutInput() && Inside && !IsPopupOpen())
	{
		for(int Button = 0; Button < 3; ++Button)
		{
			if((Flags & (BUTTONFLAG_LEFT << Button)) && MouseButtonClicked(Button))
			{
				PreLayoutCurrentFramePress = true;
				break;
			}
		}
	}
	// Deck 的预布局发生在正式渲染之前，鼠标按下可能早于上一帧正式布局建立
	// HotItem。只在该受控阶段按当前命中矩形补齐 HotItem，避免首次点击丢失；
	// popup 打开时仍保持底层控件不可穿透。
	if(PreLayoutCurrentFramePress)
	{
		// 新按下意味着旧控件不应继续占用 ActiveItem。被裁剪的卡片可能
		// 没有机会在上一帧处理释放，这里只在 Deck 的预布局路径清理陈旧状态。
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
			// 预布局先于正式渲染，同帧新出现或正在重排的控件可能尚未
			// 进入上一帧 HotItem。首次按下直接建立 ActiveItem，释放仍由
			// 同一套按钮状态机处理，避免只在按住期间显示 pressed。
			if(HotItem() == pId || (PreLayoutCurrentFramePress && MouseButtonClicked(Button)))
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

CLabelResult CUi::DoLabel(const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps) const
{
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
	TextRender()->TextEx(&Cursor, pText, -1);
	return CLabelResult{.m_Truncated = Cursor.m_Truncated};
}

void CUi::DoLabel(CUIElement::SUIElementRect &RectEl, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor) const
{
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
	TextRender()->RenderTextContainer(RectEl.m_UITextContainer, RectEl.m_TextColor, RectEl.m_TextOutlineColor, CursorPos.x, CursorPos.y);
}

void CUi::DoLabelStreamed(CUIElement::SUIElementRect &RectEl, const CUIRect *pRect, const char *pText, float Size, int Align, const SLabelProperties &LabelProps, int StrLen, const CTextCursor *pReadCursor, bool Render, bool *pTextContainerRecreated) const
{
	const int Flags = GetFlagsForLabelProperties(LabelProps, pReadCursor);
	const int ReadCursorGlyphCount = pReadCursor == nullptr ? -1 : pReadCursor->m_GlyphCount;
	bool NeedsRecreate = false;
	bool ColorChanged = RectEl.m_TextColor != TextRender()->GetTextColor() || RectEl.m_TextOutlineColor != TextRender()->GetTextOutlineColor();
	bool StyleChanged = RectEl.m_FontSize != Size || RectEl.m_TextAlign != Align || RectEl.m_LabelMaxWidth != LabelProps.m_MaxWidth || RectEl.m_LabelFlags != Flags;
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
	const float VSpacing = 2.0f;
	const float EditBoxRounding = ui_token::radius::BASE;
	const CUIRect *pHitRect = RenderOptions.m_pHitRect != nullptr ? RenderOptions.m_pHitRect : pRect;
	CUIRect Textbox;
	pRect->VMargin(VSpacing, &Textbox);
	if(RenderOnly())
	{
		const bool Active = m_pLastActiveItem == pLineInput;
		if(RenderOptions.m_DrawBackground)
			DrawRoundedSurface(this, *pRect, ScaleBackgroundAlpha(ms_LightButtonColorFunction.GetColor(Active, HotItem() == pLineInput)), ColorRGBA(), EditBoxRounding, 0.0f, Corners);
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
	else if(Inside)
	{
		if(MouseButton(0))
		{
			if(!Active)
				JustGotActive = true;
			SetActiveItem(pLineInput);
		}
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
		DrawRoundedSurface(this, *pRect, ScaleBackgroundAlpha(ms_LightButtonColorFunction.GetColor(Active, HotItem() == pLineInput)), ColorRGBA(), EditBoxRounding, 0.0f, Corners);
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
	else if(HotItem() == pLineInput)
	{
		if(MouseButton(0))
		{
			if(!Active)
				JustGotActive = true;
			SetActiveItem(pLineInput);
		}
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
		DrawRoundedSurface(this, *pRect, ms_LightButtonColorFunction.GetColor(Active, HotItem() == pLineInput), ColorRGBA(), ui_token::radius::TIGHT);
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
	DoLabel(&QuickSearch, FONT_ICON_MAGNIFYING_GLASS, FontSize, TEXTALIGN_ML);
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
				ColorRGBA Color = Props.m_Color;
				if(i == 0)
					Color.a *= ButtonColorMulActive();
				else if(i == 1)
					Color.a *= ButtonColorMulHot();
				else if(i == 2)
					Color.a *= ButtonColorMulDefault();
				if(!Enabled)
					Color.a *= 0.65f;
				Color = ScaleBackgroundAlpha(Color);
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
	if(UseRoundedRectSdf)
		DrawRoundedSurface(this, *pRect, UIElement.Rect(Index)->m_QuadColor, ColorRGBA(), Props.m_Rounding, 0.0f, Props.m_Corners);
	else
	{
		Graphics()->TextureClear();
		Graphics()->RenderQuadContainer(UIElement.Rect(Index)->m_UIRectQuadContainer, -1);
	}
	if(Props.m_ShowDropDownIcon)
	{
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
		DoLabel(&DropDownIcon, FONT_ICON_CIRCLE_CHEVRON_DOWN, DropDownIcon.h * CUi::ms_FontmodHeight, TEXTALIGN_MR);
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}
	ColorRGBA ColorText(TextRender()->DefaultTextColor());
	ColorRGBA ColorTextOutline(TextRender()->DefaultTextOutlineColor());
	if(!Enabled)
	{
		ColorText.a *= 0.65f;
		ColorTextOutline.a *= 0.65f;
	}
	if(UIElement.Rect(0)->m_UITextContainer.Valid())
		FlushQuadBatch();
	if(UIElement.Rect(0)->m_UITextContainer.Valid())
		TextRender()->RenderTextContainer(UIElement.Rect(0)->m_UITextContainer, ColorText, ColorTextOutline);
	if(!Enabled)
		return 0;
	return DoButtonLogic(pId, Props.m_Checked, pRect, Props.m_Flags);
}

int CUi::DoButton_FontIcon(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, const unsigned Flags, int Corners, bool Enabled, const std::optional<ColorRGBA> ButtonColor)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(this);
	DrawRoundedSurface(this, *pRect, ScaleBackgroundAlpha(ButtonColor.value_or(ColorRGBA(1.0f, 1.0f, 1.0f, (Checked ? 0.1f : 0.5f) * ButtonColorMul(pButtonContainer)))), ColorRGBA(), ui_token::radius::BASE, 0.0f, Corners);

	const ColorRGBA PreviousColor = TextRender()->GetTextColor();
	const ColorRGBA PreviousOutlineColor = TextRender()->GetTextOutlineColor();
	const unsigned PreviousFlags = TextRender()->GetRenderFlags();
	const EFontPreset PreviousPreset = TextRender()->GetFontPreset();
	TextRender()->SetFontPreset(QmIconWeightUsesBoldFontFallback(g_Config.m_QmUiIconWeight) ? EFontPreset::ICON_FONT_BOLD : EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	TextRender()->TextColor(ConfiguredQmUiIconColor(TextRender()->DefaultTextColor()));

	CUIRect Label;
	pRect->HMargin(2.0f, &Label);
	DoLabel(&Label, pText, Label.h * ms_FontmodHeight, TEXTALIGN_MC);

	if(!Enabled)
	{
		TextRender()->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f));
		TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f));
		DoLabel(&Label, FONT_ICON_SLASH, Label.h * ms_FontmodHeight, TEXTALIGN_MC);
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	TextRender()->SetRenderFlags(PreviousFlags);
	TextRender()->SetFontPreset(PreviousPreset);
	TextRender()->TextOutlineColor(PreviousOutlineColor);
	TextRender()->TextColor(PreviousColor);

	return DoButtonLogic(pButtonContainer, Checked, pRect, Flags);
}

int CUi::DoButton_PopupMenu(CButtonContainer *pButtonContainer, const char *pText, const CUIRect *pRect, float Size, int Align, float Padding, bool TransparentInactive, bool Enabled, const std::optional<ColorRGBA> ButtonColor, float MinimumFontSize)
{
	CUiScopedGaussianBlurSuppression GaussianBlurSuppression(this);
	if(ButtonColor.has_value() || !TransparentInactive || CheckActiveItem(pButtonContainer) || HotItem() == pButtonContainer)
		DrawRoundedSurface(this, *pRect, ScaleBackgroundAlpha(ButtonColor.value_or(Enabled ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * ButtonColorMul(pButtonContainer)) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f))), ColorRGBA(), ui_token::radius::BASE);

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
		const bool Active = CheckActiveItem(pId) || m_ActiveValueSelectorState.m_pLastTextId == pId;
		const bool Hovered = HotItem() == pId;
		DrawRoundedSurface(this, *pRect, ScaleBackgroundAlpha(ms_LightButtonColorFunction.GetColor(Active, Hovered)), ColorRGBA(), ui_token::radius::BASE);
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
	const bool UseQmSliderStyle = g_Config.m_QmNewUi != 0;

	// layout
	CUIRect Rail;
	if(UseQmSliderStyle || pColorInner)
		Rail = *pRect;
	else
		pRect->HMargin(5.0f, &Rail);

	const float HandleSize = UseQmSliderStyle ? std::clamp(Rail.h * 0.75f, 8.0f, 16.0f) : 0.0f;
	CUIRect Handle;
	Rail.VSplitLeft(UseQmSliderStyle ? HandleSize : (pColorInner ? 8.0f : std::clamp(33.0f, Rail.h, Rail.w / 3.0f)), &Handle, nullptr);
	if(UseQmSliderStyle)
	{
		Handle.h = HandleSize;
		Handle.y = Rail.y + (Rail.h - Handle.h) * 0.5f;
	}
	Handle.x += (Rail.w - Handle.w) * Current;

	CUIRect HandleArea = Handle;
	if(UseQmSliderStyle || !pColorInner)
	{
		HandleArea.h = pRect->h * 0.9f;
		HandleArea.y = pRect->y + pRect->h * 0.05f;
		HandleArea.w += 6.0f;
		HandleArea.x -= 3.0f;
	}

	if(!UseQmSliderStyle && !pColorInner && MouseHovered(&HandleArea) && (CheckActiveItem(pId) || HotItem() == pId))
	{
		Handle.h += 3.0f;
		Handle.y -= 1.5f;
	}

	const ColorRGBA HandleColor = ms_ScrollBarColorFunction.GetColor(CheckActiveItem(pId), HotItem() == pId);
	if(UseQmSliderStyle)
	{
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
	else if(pColorInner)
	{
		CUIRect Slider;
		Handle.VMargin(-2.0f, &Slider);
		Slider.HMargin(-3.0f, &Slider);
		DrawRoundedSurface(this, Slider, ScaleBackgroundAlpha(ColorRGBA(0.15f, 0.15f, 0.15f, 1.0f).Multiply(HandleColor)), ColorRGBA(), ui_token::radius::BASE);
		Slider.Margin(2.0f, &Slider);
		DrawRoundedSurface(this, Slider, ScaleBackgroundAlpha(pColorInner->Multiply(HandleColor)), ColorRGBA(), ui_token::radius::TIGHT);
	}
	else
	{
		DrawRoundedSurface(this, Rail, ScaleBackgroundAlpha(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f)), ColorRGBA(), Rail.h / 2.0f);
		DrawRoundedSurface(this, Handle, ScaleBackgroundAlpha(HandleColor), ColorRGBA(), Rail.h / 2.0f);
	}
}

float CUi::DoScrollbarH(const void *pId, const CUIRect *pRect, float Current, const ColorRGBA *pColorInner)
{
	Current = std::clamp(Current, 0.0f, 1.0f);
	const bool UseQmSliderStyle = g_Config.m_QmNewUi != 0;

	// layout
	CUIRect Rail;
	if(UseQmSliderStyle || pColorInner)
		Rail = *pRect;
	else
		pRect->HMargin(5.0f, &Rail);

	const float HandleSize = UseQmSliderStyle ? std::clamp(Rail.h * 0.75f, 8.0f, 16.0f) : 0.0f;
	CUIRect Handle;
	Rail.VSplitLeft(UseQmSliderStyle ? HandleSize : (pColorInner ? 8.0f : std::clamp(33.0f, Rail.h, Rail.w / 3.0f)), &Handle, nullptr);
	if(UseQmSliderStyle)
	{
		Handle.h = HandleSize;
		Handle.y = Rail.y + (Rail.h - Handle.h) * 0.5f;
	}
	Handle.x += (Rail.w - Handle.w) * Current;

	CUIRect HandleArea = Handle;
	if(UseQmSliderStyle || !pColorInner)
	{
		HandleArea.h = pRect->h * 0.9f;
		HandleArea.y = pRect->y + pRect->h * 0.05f;
		HandleArea.w += 6.0f;
		HandleArea.x -= 3.0f;
	}

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

void CUi::RenderTime(CUIRect TimeRect, float FontSize, int Seconds, bool NotFinished, int Millis, bool TrueMilliseconds) const
{
	if(NotFinished)
		return;

	char aBuf[128];

	str_time(((int64_t)absolute(Seconds)) * 100, TIME_HOURS, aBuf, sizeof(aBuf));

	// align in vertical middle
	vec2 Cursor = TimeRect.TopLeft();
	float TextHeight = 0.0f;
	float SecondsMaxHeight = 0.0f;
	STextSizeProperties TextSizeProps{};
	TextSizeProps.m_pMaxCharacterHeightInLine = &SecondsMaxHeight;
	TextSizeProps.m_pHeight = &TextHeight;

	float SecondsWidth = std::min(TextRender()->TextWidth(FontSize, aBuf, -1, -1.0f, 0, TextSizeProps), TimeRect.w);
	Cursor.x += TimeRect.w - SecondsWidth; // align right
	Cursor.y += ((TimeRect.h - SecondsMaxHeight) / 2.0f - (FontSize - SecondsMaxHeight));

	// show milliseconds or centiseconds if we are under an hour
	if(Millis >= 0 && Seconds < 60 * 60)
	{
		constexpr float GoldenRatio = 0.61803398875f;
		const float CentisecondFontSize = FontSize * GoldenRatio;

		// format 2 or 3 digits
		char aMillis[4];
		Millis %= 1000;
		if(!TrueMilliseconds)
			str_format(aMillis, sizeof(aMillis), "%02d", (int)std::round(Millis / 10));
		else
			str_format(aMillis, sizeof(aMillis), "%03d", Millis);

		float MillisWidth = TextRender()->TextWidth(CentisecondFontSize, aMillis, -1, -1.0f, 0, TextSizeProps);

		// make space for millis, but put them 1/6th of a char tighter together
		Cursor.x -= MillisWidth - (TrueMilliseconds ? MillisWidth / (3 * 6) : MillisWidth / (2 * 6));

		vec2 CursorMillis = TimeRect.TopLeft();
		CursorMillis.x += TimeRect.w - MillisWidth; // align right
		CursorMillis.y += ((TimeRect.h - SecondsMaxHeight) / 2.0f - (CentisecondFontSize - SecondsMaxHeight));
		CursorMillis.y -= (CursorMillis.y - Cursor.y) * GoldenRatio;

		TextRender()->Text(Cursor.x, Cursor.y, FontSize, aBuf);
		TextRender()->Text(CursorMillis.x, CursorMillis.y, CentisecondFontSize, aMillis);
	}
	else
	{
		str_time(((int64_t)absolute(Seconds)) * 100, TIME_HOURS, aBuf, sizeof(aBuf));
		TextRender()->Text(Cursor.x, Cursor.y, FontSize, aBuf);
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

	if(Result && Clicked)
	{
		if(m_BackButtonOp == EBackButtonOp::CLICKED && m_DispatchInputFunction)
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
	else if(Result && Abrupted)
	{
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
	DoLabel(&m_BackButtonRect, FONT_ICON_CHEVRON_LEFT, m_BackButtonRect.w * 0.5f, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

void CUi::DoPopupMenu(const SPopupMenuId *pId, float X, float Y, float Width, float Height, void *pContext, FPopupMenuFunction pfnFunc, const SPopupMenuProperties &Props)
{
	if(Props.m_AutoReposition)
	{
		constexpr float Margin = SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN;
		if(X + Width > Screen()->w - Margin)
			X = maximum<float>(X - Width, Margin);
		if(Y + Height > Screen()->h - Margin)
			Y = maximum<float>(Y - Height, Margin);
	}

	auto ExistingPopupMenu = std::find_if(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu &PopupMenu) { return PopupMenu.m_pId == pId; });
	if(ExistingPopupMenu != m_vPopupMenus.end())
	{
		ExistingPopupMenu->m_Props = Props;
		ExistingPopupMenu->m_Rect.x = X;
		ExistingPopupMenu->m_Rect.y = Y;
		ExistingPopupMenu->m_Rect.w = Width;
		ExistingPopupMenu->m_Rect.h = Height;
		ExistingPopupMenu->m_pContext = pContext;
		ExistingPopupMenu->m_pfnFunc = pfnFunc;
		return;
	}

	m_vPopupMenus.emplace_back();
	SPopupMenu *pNewMenu = &m_vPopupMenus.back();
	pNewMenu->m_pId = pId;
	pNewMenu->m_Props = Props;
	pNewMenu->m_Rect.x = X;
	pNewMenu->m_Rect.y = Y;
	pNewMenu->m_Rect.w = Width;
	pNewMenu->m_Rect.h = Height;
	pNewMenu->m_pContext = pContext;
	pNewMenu->m_pfnFunc = pfnFunc;
	if(Props.m_BlockUnderlyingPointerInput)
	{
		if(CLineInput *pActiveInput = CLineInput::GetActiveInput())
			pActiveInput->Deactivate();
		m_pLastActiveItem = nullptr;
		SetActiveItem(nullptr);
		m_ActiveButtonLogicButton = -1;
		SetHotItem(pId);
	}
}

void CUi::RenderPopupMenus()
{
	m_RenderingPopupMenus = true;
	for(size_t i = 0; i < m_vPopupMenus.size(); ++i)
	{
		const SPopupMenu &PopupMenu = m_vPopupMenus[i];
		const SPopupMenuId *pId = PopupMenu.m_pId;
		if(PopupMenu.m_Props.m_RequireSourceRefresh && !QmDropdownSourceAlive(Client()->PerfFrame(), PopupMenu.m_Props.m_SourceFrame, true))
		{
			ClosePopupMenu(pId);
			--i;
			continue;
		}
		const bool Inside = MouseInside(&PopupMenu.m_Rect) && (!PopupMenu.m_Props.m_ClipToViewport || MouseInside(&PopupMenu.m_Props.m_Viewport));
		const bool Active = i == m_vPopupMenus.size() - 1;
		const bool ClipToViewport = PopupMenu.m_Props.m_ClipToViewport;
		const bool AllowPopupPointerInput = Active && PopupMenu.m_Props.m_BlockUnderlyingPointerInput;
		if(AllowPopupPointerInput)
			++m_PopupInputDepth;

		if(Active)
		{
			// Prevent UI elements below the popup menu from being activated.
			SetHotItem(pId);
		}

		if(CheckActiveItem(pId))
		{
			if(!MouseButton(0))
			{
				if(!Inside)
				{
					ClosePopupMenu(pId);
					--i;
					continue;
				}
				SetActiveItem(nullptr);
			}
		}
		else if(HotItem() == pId)
		{
			if(MouseButton(0))
				SetActiveItem(pId);
		}

		if(Inside && PopupMenu.m_Props.m_BlockUnderlyingScroll)
		{
			// Prevent scroll regions directly behind popup menus from using the mouse scroll events.
			SetHotScrollRegion(nullptr);
		}
		if(ClipToViewport)
			ClipEnable(&PopupMenu.m_Props.m_Viewport);

		CUIRect PopupRect = PopupMenu.m_Rect;
		DrawRoundedSurface(this, PopupRect, PopupMenu.m_Props.m_BackgroundColor, PopupMenu.m_Props.m_BorderColor, ui_token::radius::CARD, SPopupMenu::POPUP_BORDER, PopupMenu.m_Props.m_Corners);
		PopupRect.Margin(SPopupMenu::POPUP_BORDER, &PopupRect);
		PopupRect.Margin(SPopupMenu::POPUP_MARGIN, &PopupRect);

		// The popup render function can open/close popups, which may resize the vector and thus
		// invalidate the variable PopupMenu. We therefore store pId in a separate variable.
		EPopupMenuFunctionResult Result = PopupMenu.m_pfnFunc(PopupMenu.m_pContext, PopupRect, Active);
		if(ClipToViewport)
			ClipDisable();
		if(AllowPopupPointerInput)
			--m_PopupInputDepth;
		if(Result != POPUP_KEEP_OPEN || (Active && ConsumeHotkey(HOTKEY_ESCAPE)))
			ClosePopupMenu(pId, Result == POPUP_CLOSE_CURRENT_AND_DESCENDANTS);
	}
	m_RenderingPopupMenus = false;
}

void CUi::ClosePopupMenu(const SPopupMenuId *pId, bool IncludeDescendants)
{
	auto PopupMenuToClose = std::find_if(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu &PopupMenu) { return PopupMenu.m_pId == pId; });
	if(PopupMenuToClose != m_vPopupMenus.end())
	{
		if(IncludeDescendants)
			m_vPopupMenus.erase(PopupMenuToClose, m_vPopupMenus.end());
		else
			m_vPopupMenus.erase(PopupMenuToClose);
		SetActiveItem(nullptr);
		if(m_pfnPopupMenuClosedCallback)
			m_pfnPopupMenuClosedCallback();
	}
}

void CUi::ClosePopupMenus()
{
	if(m_vPopupMenus.empty())
		return;

	m_vPopupMenus.clear();
	SetActiveItem(nullptr);
	if(m_pfnPopupMenuClosedCallback)
		m_pfnPopupMenuClosedCallback();
}

bool CUi::IsPopupOpen() const
{
	return !m_vPopupMenus.empty();
}

bool CUi::IsPopupOpen(const SPopupMenuId *pId) const
{
	return std::any_of(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu PopupMenu) { return PopupMenu.m_pId == pId; });
}

const CUIRect *CUi::GetPopupMenuRect(const SPopupMenuId *pId) const
{
	const auto PopupMenuIt = std::find_if(m_vPopupMenus.begin(), m_vPopupMenus.end(), [pId](const SPopupMenu &PopupMenu) { return PopupMenu.m_pId == pId; });
	return PopupMenuIt == m_vPopupMenus.end() ? nullptr : &PopupMenuIt->m_Rect;
}

bool CUi::IsPopupHovered() const
{
	return std::any_of(m_vPopupMenus.begin(), m_vPopupMenus.end(), [this](const SPopupMenu PopupMenu) { return MouseHovered(&PopupMenu.m_Rect); });
}

void CUi::SetPopupMenuClosedCallback(FPopupMenuClosedCallback pfnCallback)
{
	m_pfnPopupMenuClosedCallback = std::move(pfnCallback);
}

void CUi::SMessagePopupContext::DefaultColor(ITextRender *pTextRender)
{
	m_TextColor = pTextRender->DefaultTextColor();
}

void CUi::SMessagePopupContext::ErrorColor()
{
	m_TextColor = ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f);
}

CUi::EPopupMenuFunctionResult CUi::PopupMessage(void *pContext, CUIRect View, bool Active)
{
	SMessagePopupContext *pMessagePopup = static_cast<SMessagePopupContext *>(pContext);
	CUi *pUI = pMessagePopup->m_pUI;

	pUI->TextRender()->TextColor(pMessagePopup->m_TextColor);
	pUI->TextRender()->Text(View.x, View.y, SMessagePopupContext::POPUP_FONT_SIZE, pMessagePopup->m_aMessage, View.w);
	pUI->TextRender()->TextColor(pUI->TextRender()->DefaultTextColor());

	return (Active && pUI->ConsumeHotkey(HOTKEY_ENTER)) ? CUi::POPUP_CLOSE_CURRENT : CUi::POPUP_KEEP_OPEN;
}

void CUi::ShowPopupMessage(float X, float Y, SMessagePopupContext *pContext)
{
	const float TextWidth = minimum(std::ceil(TextRender()->TextWidth(SMessagePopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, -1.0f) + 0.5f), SMessagePopupContext::POPUP_MAX_WIDTH);
	float TextHeight = 0.0f;
	STextSizeProperties TextSizeProps{};
	TextSizeProps.m_pHeight = &TextHeight;
	TextRender()->TextWidth(SMessagePopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, TextWidth, 0, TextSizeProps);
	pContext->m_pUI = this;
	DoPopupMenu(pContext, X, Y, TextWidth + 10.0f, TextHeight + 10.0f, pContext, PopupMessage);
}

CUi::SConfirmPopupContext::SConfirmPopupContext()
{
	Reset();
}

void CUi::SConfirmPopupContext::Reset()
{
	m_Result = SConfirmPopupContext::UNSET;
}

void CUi::SConfirmPopupContext::YesNoButtons()
{
	str_copy(m_aPositiveButtonLabel, Localize("Yes"));
	str_copy(m_aNegativeButtonLabel, Localize("No"));
}

void CUi::ShowPopupConfirm(float X, float Y, SConfirmPopupContext *pContext)
{
	const float TextWidth = minimum(std::ceil(TextRender()->TextWidth(SConfirmPopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, -1.0f) + 0.5f), SConfirmPopupContext::POPUP_MAX_WIDTH);
	float TextHeight = 0.0f;
	STextSizeProperties TextSizeProps{};
	TextSizeProps.m_pHeight = &TextHeight;
	TextRender()->TextWidth(SConfirmPopupContext::POPUP_FONT_SIZE, pContext->m_aMessage, -1, TextWidth, 0, TextSizeProps);
	const float PopupHeight = TextHeight + SConfirmPopupContext::POPUP_BUTTON_HEIGHT + SConfirmPopupContext::POPUP_BUTTON_SPACING + 10.0f;
	pContext->m_pUI = this;
	pContext->m_Result = SConfirmPopupContext::UNSET;
	DoPopupMenu(pContext, X, Y, TextWidth + 10.0f, PopupHeight, pContext, PopupConfirm);
}

CUi::EPopupMenuFunctionResult CUi::PopupConfirm(void *pContext, CUIRect View, bool Active)
{
	SConfirmPopupContext *pConfirmPopup = static_cast<SConfirmPopupContext *>(pContext);
	CUi *pUI = pConfirmPopup->m_pUI;

	CUIRect Label, ButtonBar, CancelButton, ConfirmButton;
	View.HSplitBottom(SConfirmPopupContext::POPUP_BUTTON_HEIGHT, &Label, &ButtonBar);
	ButtonBar.VSplitMid(&CancelButton, &ConfirmButton, SConfirmPopupContext::POPUP_BUTTON_SPACING);

	pUI->TextRender()->Text(Label.x, Label.y, SConfirmPopupContext::POPUP_FONT_SIZE, pConfirmPopup->m_aMessage, Label.w);

	if(pUI->DoButton_PopupMenu(&pConfirmPopup->m_CancelButton, pConfirmPopup->m_aNegativeButtonLabel, &CancelButton, SConfirmPopupContext::POPUP_FONT_SIZE, TEXTALIGN_MC))
	{
		pConfirmPopup->m_Result = SConfirmPopupContext::CANCELED;
		return CUi::POPUP_CLOSE_CURRENT;
	}

	if(pUI->DoButton_PopupMenu(&pConfirmPopup->m_ConfirmButton, pConfirmPopup->m_aPositiveButtonLabel, &ConfirmButton, SConfirmPopupContext::POPUP_FONT_SIZE, TEXTALIGN_MC) || (Active && pUI->ConsumeHotkey(HOTKEY_ENTER)))
	{
		pConfirmPopup->m_Result = SConfirmPopupContext::CONFIRMED;
		return CUi::POPUP_CLOSE_CURRENT;
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::SSelectionPopupContext::SSelectionPopupContext()
{
	Reset();
}

void CUi::SSelectionPopupContext::Reset()
{
	m_pUI = nullptr;
	m_pScrollRegion = nullptr;
	m_Props = SPopupMenuProperties();
	m_aMessage[0] = '\0';
	m_pSelection = nullptr;
	m_SelectionIndex = -1;
	m_ActiveIndex = -1;
	m_vEntries.clear();
	m_vButtonContainers.clear();
	m_EntryHeight = 12.0f;
	m_EntryPadding = 0.0f;
	m_EntrySpacing = 5.0f;
	m_FontSize = 10.0f;
	m_MinimumFontSize = -1.0f;
	m_Width = 300.0f + (SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN) * 2;
	m_AlignmentHeight = -1.0f;
	m_ActiveEntryColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.22f);
	m_TransparentButtons = false;
	m_AnchorVisible = true;
	m_PopupVisible = true;
	m_BlockUnderlyingScroll = false;
	m_Scrollable = false;
	m_ScrollToActiveItem = false;
	m_MenuUiFirstWheelLogged = false;
	m_Viewport = {};
	m_PopupPolicy = {};
	m_SpecialFontRenderMode = false;
}

CUi::EPopupMenuFunctionResult CUi::PopupSelection(void *pContext, CUIRect View, bool Active)
{
	const bool MenuUiPerfEnabled = QmPerfEnabled();
	const auto MenuUiStartTime = MenuUiPerfEnabled ? time_get_nanoseconds() : std::chrono::nanoseconds::zero();
	SSelectionPopupContext *pSelectionPopup = static_cast<SSelectionPopupContext *>(pContext);
	CUi *pUI = pSelectionPopup->m_pUI;
	CScrollRegion *pScrollRegion = pSelectionPopup->m_pScrollRegion;
	if(pScrollRegion == nullptr)
	{
		log_error("ui", "Selection popup opened without a scroll region");
		return CUi::POPUP_CLOSE_CURRENT;
	}

	vec2 ScrollOffset(0.0f, 0.0f);
	SQmScrollRequest ScrollRequest;
	ScrollRequest.m_Profile = EQmScrollProfile::POPUP_LIST;
	ScrollRequest.m_RowExtent = pSelectionPopup->m_EntryHeight + pSelectionPopup->m_EntrySpacing;
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy(ScrollRequest);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	ScrollParams.m_HideScrollbar = !pSelectionPopup->m_Scrollable;
	ScrollParams.m_ScrollbarNoOuterMargin = true;
	ScrollParams.m_pWheelOwnerId = pSelectionPopup;
	ScrollParams.m_WheelOwnerPreRegistered = true;
	const float PopupOuterHeight = (SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN) * 2.0f;
	pScrollRegion->SetContentHeightForNextFrame(std::max(0.0f, pSelectionPopup->m_PopupPolicy.m_ContentHeight - PopupOuterHeight));
	pScrollRegion->Begin(&View, &ScrollOffset, &ScrollParams);
	View.y += ScrollOffset.y;

	CUIRect Slot;
	if(pSelectionPopup->m_aMessage[0] != '\0')
	{
		const STextBoundingBox TextBoundingBox = pUI->TextRender()->TextBoundingBox(pSelectionPopup->m_FontSize, pSelectionPopup->m_aMessage, -1, pSelectionPopup->m_Width);
		View.HSplitTop(TextBoundingBox.m_H, &Slot, &View);
		if(pScrollRegion->AddRect(Slot))
		{
			pUI->TextRender()->Text(Slot.x, Slot.y, pSelectionPopup->m_FontSize, pSelectionPopup->m_aMessage, Slot.w);
		}
	}

	pSelectionPopup->m_vButtonContainers.resize(pSelectionPopup->m_vEntries.size());

	size_t Index = 0;
	int VisibleEntries = 0;
	for(const auto &Entry : pSelectionPopup->m_vEntries)
	{
		// TClient
		if(pSelectionPopup->m_SpecialFontRenderMode)
			pUI->TextRender()->SetCustomFace(Entry.c_str());

		if(pSelectionPopup->m_aMessage[0] != '\0' || Index != 0)
			View.HSplitTop(pSelectionPopup->m_EntrySpacing, nullptr, &View);
		View.HSplitTop(pSelectionPopup->m_EntryHeight, &Slot, &View);
		const bool ActiveEntry = pSelectionPopup->m_ActiveIndex == static_cast<int>(Index);
		if(pScrollRegion->AddRect(Slot, QmDropdownActiveItemShouldScrollIntoView(pSelectionPopup->m_ScrollToActiveItem, ActiveEntry)))
		{
			++VisibleEntries;
			// 活动项与悬浮项使用同一种整行背景，避免左侧竖条与条目背景重叠。
			const std::optional<ColorRGBA> ActiveColor = ActiveEntry ? std::optional<ColorRGBA>(pSelectionPopup->m_ActiveEntryColor) : std::nullopt;
			if(pUI->DoButton_PopupMenu(&pSelectionPopup->m_vButtonContainers[Index], Entry.c_str(), &Slot, pSelectionPopup->m_FontSize, TEXTALIGN_ML, pSelectionPopup->m_EntryPadding, pSelectionPopup->m_TransparentButtons, true, ActiveColor))
			{
				pSelectionPopup->m_pSelection = &Entry;
				pSelectionPopup->m_SelectionIndex = Index;
			}
		}
		++Index;
	}
	// TClient
	if(pSelectionPopup->m_SpecialFontRenderMode)
		pUI->TextRender()->SetCustomFace(g_Config.m_TcCustomFont);

	pScrollRegion->End();
	pSelectionPopup->m_ScrollToActiveItem = false;
	if(!pSelectionPopup->m_MenuUiFirstWheelLogged && pScrollRegion->WheelConsumedThisFrame())
	{
		pUI->m_MenuUiFirstWheelPerf = MenuUiPerfEnabled;
		SQmMenuUiFramePerf MenuUiPerf;
		MenuUiPerf.m_pPage = "dropdown";
		MenuUiPerf.m_pOperation = "dropdown_first_wheel";
		MenuUiPerf.m_ItemsTotal = (int)pSelectionPopup->m_vEntries.size();
		MenuUiPerf.m_ItemsVisible = VisibleEntries;
		MenuUiPerf.m_ItemsProcessed = VisibleEntries;
		MenuUiPerf.m_ItemsSkipped = maximum(0, MenuUiPerf.m_ItemsTotal - VisibleEntries);
		MenuUiPerf.m_UiMs = MenuUiPerfEnabled ? std::chrono::duration<double, std::milli>(time_get_nanoseconds() - MenuUiStartTime).count() : -1.0;
		QmLogMenuUiFramePerf(MenuUiPerf, pUI->Client());
		pSelectionPopup->m_MenuUiFirstWheelLogged = true;
	}

	return pSelectionPopup->m_pSelection == nullptr ? CUi::POPUP_KEEP_OPEN : CUi::POPUP_CLOSE_CURRENT;
}

void CUi::ShowPopupSelection(float X, float Y, SSelectionPopupContext *pContext)
{
	const bool HasMessage = pContext->m_aMessage[0] != '\0';
	const STextBoundingBox TextBoundingBox = TextRender()->TextBoundingBox(pContext->m_FontSize, pContext->m_aMessage, -1, pContext->m_Width);
	const float OuterHeight = (SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN) * 2;
	pContext->m_PopupPolicy = QmResolveDropdownPopupPolicy(pContext->m_vEntries.size(), pContext->m_EntryHeight, pContext->m_EntrySpacing, HasMessage, TextBoundingBox.m_H, OuterHeight);
	const float PopupHeight = pContext->m_PopupPolicy.m_PreferredHeight;
	if(pContext->m_Viewport.w <= 0.0f || pContext->m_Viewport.h <= 0.0f)
		pContext->m_Viewport = *Screen();
	const CUIRect &Viewport = pContext->m_Viewport;
	pContext->m_pUI = this;
	pContext->m_pSelection = nullptr;
	pContext->m_SelectionIndex = -1;
	pContext->m_Props.m_Corners = IGraphics::CORNER_ALL;
	float PopupWidth = pContext->m_Width;
	float PopupHeightResolved = PopupHeight;
	if(pContext->m_AlignmentHeight >= 0.0f)
	{
		constexpr float Margin = SPopupMenu::POPUP_BORDER + SPopupMenu::POPUP_MARGIN;
		CUIRect AnchorRect;
		AnchorRect.x = X;
		AnchorRect.y = Y;
		AnchorRect.w = pContext->m_Width;
		AnchorRect.h = pContext->m_AlignmentHeight;
		SQmDropdownGeometryConfig GeometryConfig;
		GeometryConfig.m_Width = pContext->m_Width;
		GeometryConfig.m_Height = PopupHeight;
		GeometryConfig.m_Margin = Margin;
		const SQmDropdownGeometryResult Geometry = QmComputeDropdownPopupGeometry(AnchorRect, Viewport, GeometryConfig);
		pContext->m_AnchorVisible = Geometry.m_AnchorVisible;
		pContext->m_PopupVisible = Geometry.m_PopupVisible;
		if(!pContext->m_AnchorVisible || !pContext->m_PopupVisible)
		{
			ClosePopupMenu(pContext);
			return;
		}
		X = Geometry.m_Rect.x;
		Y = Geometry.m_Rect.y;
		PopupWidth = Geometry.m_Rect.w;
		PopupHeightResolved = Geometry.m_Rect.h;
		pContext->m_Props.m_AutoReposition = false;
		pContext->m_Props.m_Corners = Geometry.m_PlacedBelow ? IGraphics::CORNER_B : IGraphics::CORNER_T;
	}
	const CUIRect PopupRect{X, Y, PopupWidth, PopupHeightResolved};
	const bool Scrollable = pContext->m_PopupVisible && QmDropdownPopupScrollable(pContext->m_PopupPolicy, PopupHeightResolved);
	const bool BlockUnderlying = QmDropdownPopupBlocksUnderlying(pContext->m_PopupVisible);
	RegisterWheelOwner(pContext, EUiWheelOwnerPriority::POPUP, PopupRect, BlockUnderlying);
	pContext->m_Scrollable = Scrollable;
	pContext->m_BlockUnderlyingScroll = BlockUnderlying;
	pContext->m_Props.m_ClipToViewport = true;
	pContext->m_Props.m_BlockUnderlyingScroll = BlockUnderlying;
	pContext->m_Props.m_Viewport = Viewport;
	DoPopupMenu(pContext, X, Y, PopupWidth, PopupHeightResolved, pContext, PopupSelection, pContext->m_Props);
}

int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, const SDropDownProperties &DropDownProps)
{
	const float ResolvedFontSize = DropDownProps.m_FontSize > 0.0f ? DropDownProps.m_FontSize : m_DropDownFontSize > 0.0f ? m_DropDownFontSize :
																pRect->h * ms_FontmodHeight * 0.8f;
	if(RenderOnly())
	{
		if(pRect != nullptr && pStrs != nullptr && CurSelection >= 0 && CurSelection < Num)
			DoLabel(pRect, pStrs[CurSelection], ResolvedFontSize, TEXTALIGN_MC);
		return CurSelection;
	}

	if(!State.m_Init)
	{
		State.m_UiElement.Init(this, -1);
		State.m_pOwnedScrollRegion = std::make_shared<CScrollRegion>();
		State.m_pScrollRegion = State.m_SelectionPopupContext.m_pScrollRegion != nullptr ? State.m_SelectionPopupContext.m_pScrollRegion : State.m_pOwnedScrollRegion.get();
		State.m_SelectionPopupContext.m_pScrollRegion = State.m_pScrollRegion;
		State.m_Init = true;
	}
	else if(State.m_SelectionPopupContext.m_pScrollRegion != nullptr && State.m_SelectionPopupContext.m_pScrollRegion != State.m_pScrollRegion)
		State.m_pScrollRegion = State.m_SelectionPopupContext.m_pScrollRegion;

	bool PopupOpen = IsPopupOpen(&State.m_SelectionPopupContext);
	// 弹窗使用设置页最外层裁剪区，不能越过 Tab 或页面容器；卡片内容裁剪区
	// 只判断锚点是否仍完整可见，锚点滚出卡片后应关闭弹窗。
	const CUIRect Viewport = DropDownProps.m_pPopupViewport != nullptr ? *DropDownProps.m_pPopupViewport : IsClipped() ? *OutermostClipArea() :
															     *Screen();
	const CUIRect AnchorViewport = DropDownProps.m_pAnchorViewport != nullptr ? *DropDownProps.m_pAnchorViewport : IsClipped() ? *ClipArea() :
																     Viewport;
	const uint64_t SourceFrame = Client()->PerfFrame();
	if(PopupOpen && !QmDropdownAnchorFullyVisible(*pRect, AnchorViewport))
	{
		ClosePopupMenu(&State.m_SelectionPopupContext);
		State.m_DropDownState.Reset();
		State.m_SelectionPopupContext.Reset();
		PopupOpen = false;
	}
	if(State.m_DropDownState.IsOpen() && !PopupOpen)
		State.m_DropDownState.Reset();

	const auto LabelFunc = [CurSelection, pStrs]() {
		return CurSelection > -1 ? pStrs[CurSelection] : "";
	};
	if(!DropDownProps.m_Enabled)
	{
		if(DropDownProps.m_ClosePopupWhenDisabled)
		{
			if(State.m_DropDownState.Disable(PopupOpen))
				ClosePopupMenu(&State.m_SelectionPopupContext);
			State.m_SelectionPopupContext.m_SelectionIndex = -1;
			State.m_SelectionPopupContext.m_ActiveIndex = -1;
		}
		SMenuButtonProperties ButtonProps;
		ButtonProps.m_Enabled = false;
		ButtonProps.m_HintRequiresStringCheck = true;
		ButtonProps.m_HintCanChangePositionOrSize = true;
		ButtonProps.m_ShowDropDownIcon = true;
		ButtonProps.m_FontSize = ResolvedFontSize;
		ButtonProps.m_Color = DropDownProps.m_VisualStyle.m_TriggerColor;
		DoButton_Menu(State.m_UiElement, &State.m_ButtonContainer, LabelFunc, pRect, ButtonProps);
		return CurSelection;
	}

	SMenuButtonProperties Props;
	Props.m_HintRequiresStringCheck = true;
	Props.m_HintCanChangePositionOrSize = true;
	Props.m_ShowDropDownIcon = true;
	Props.m_FontSize = ResolvedFontSize;
	Props.m_Color = DropDownProps.m_VisualStyle.m_TriggerColor;
	if(PopupOpen)
	{
		State.m_SelectionPopupContext.m_Props.m_RequireSourceRefresh = true;
		State.m_SelectionPopupContext.m_Props.m_SourceFrame = SourceFrame;
		Props.m_Corners = IGraphics::CORNER_ALL & (~State.m_SelectionPopupContext.m_Props.m_Corners);
	}
	const bool TogglePressed = DoButton_Menu(State.m_UiElement, &State.m_ButtonContainer, LabelFunc, pRect, Props);

	SQmDropdownInput DropDownInput;
	DropDownInput.m_TogglePressed = TogglePressed;
	DropDownInput.m_InitialIndex = CurSelection;
	if(PopupOpen && State.m_SelectionPopupContext.m_SelectionIndex < 0)
	{
		DropDownInput.m_KeyUp = ConsumeHotkey(HOTKEY_UP);
		DropDownInput.m_KeyDown = ConsumeHotkey(HOTKEY_DOWN);
		DropDownInput.m_KeyEnter = ConsumeHotkey(HOTKEY_ENTER);
		DropDownInput.m_KeyEscape = ConsumeHotkey(HOTKEY_ESCAPE);
	}
	const int PreviousActiveIndex = State.m_DropDownState.ActiveIndex();
	const SQmDropdownUpdateResult DropDownResult = State.m_DropDownState.Update(DropDownInput, Num);
	State.m_SelectionPopupContext.m_ActiveIndex = State.m_DropDownState.ActiveIndex();
	if(QmDropdownShouldRequestActiveScroll(PopupOpen, PreviousActiveIndex, State.m_DropDownState.ActiveIndex()))
		State.m_SelectionPopupContext.m_ScrollToActiveItem = true;
	if(PopupOpen)
	{
		State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;
		State.m_SelectionPopupContext.m_EntryHeight = pRect->h;
		State.m_SelectionPopupContext.m_EntryPadding = pRect->h >= 20.0f ? 2.0f : 1.0f;
		State.m_SelectionPopupContext.m_Width = pRect->w;
		State.m_SelectionPopupContext.m_AlignmentHeight = pRect->h;
		State.m_SelectionPopupContext.m_Viewport = Viewport;
		State.m_SelectionPopupContext.m_Props.m_BorderColor = DropDownProps.m_VisualStyle.m_PopupBorderColor;
		State.m_SelectionPopupContext.m_Props.m_BackgroundColor = DropDownProps.m_VisualStyle.m_PopupBackgroundColor;
		State.m_SelectionPopupContext.m_ActiveEntryColor = DropDownProps.m_VisualStyle.m_ActiveEntryColor;
		State.m_SelectionPopupContext.m_TransparentButtons = DropDownProps.m_VisualStyle.m_TransparentEntries;
		ShowPopupSelection(pRect->x, pRect->y, &State.m_SelectionPopupContext);
		PopupOpen = IsPopupOpen(&State.m_SelectionPopupContext);
		if(State.m_DropDownState.IsOpen() && !PopupOpen)
			State.m_DropDownState.Reset();
	}
	if(DropDownResult.m_Opened)
	{
		CScrollRegion *pScrollRegion = State.m_SelectionPopupContext.m_pScrollRegion;
		const bool SpecialFontRenderMode = State.m_SelectionPopupContext.m_SpecialFontRenderMode;
		State.m_SelectionPopupContext.Reset();
		State.m_SelectionPopupContext.m_pScrollRegion = pScrollRegion != nullptr ? pScrollRegion : State.m_pScrollRegion;
		State.m_SelectionPopupContext.m_SpecialFontRenderMode = SpecialFontRenderMode;
		State.m_SelectionPopupContext.m_Props.m_BorderColor = DropDownProps.m_VisualStyle.m_PopupBorderColor;
		State.m_SelectionPopupContext.m_Props.m_BackgroundColor = DropDownProps.m_VisualStyle.m_PopupBackgroundColor;
		State.m_SelectionPopupContext.m_ActiveEntryColor = DropDownProps.m_VisualStyle.m_ActiveEntryColor;
		for(int i = 0; i < Num; ++i)
			State.m_SelectionPopupContext.m_vEntries.emplace_back(pStrs[i]);
		State.m_SelectionPopupContext.m_EntryHeight = pRect->h;
		State.m_SelectionPopupContext.m_EntryPadding = pRect->h >= 20.0f ? 2.0f : 1.0f;
		State.m_SelectionPopupContext.m_FontSize = ResolvedFontSize;
		State.m_SelectionPopupContext.m_Width = pRect->w;
		State.m_SelectionPopupContext.m_AlignmentHeight = pRect->h;
		State.m_SelectionPopupContext.m_TransparentButtons = DropDownProps.m_VisualStyle.m_TransparentEntries;
		State.m_SelectionPopupContext.m_ActiveIndex = State.m_DropDownState.ActiveIndex();
		State.m_SelectionPopupContext.m_Props.m_RequireSourceRefresh = true;
		State.m_SelectionPopupContext.m_Props.m_SourceFrame = SourceFrame;
		State.m_SelectionPopupContext.m_ScrollToActiveItem = true;
		State.m_SelectionPopupContext.m_Viewport = Viewport;
		ShowPopupSelection(pRect->x, pRect->y, &State.m_SelectionPopupContext);
	}
	if(DropDownResult.m_Selected)
	{
		ClosePopupMenu(&State.m_SelectionPopupContext);
		State.m_SelectionPopupContext.Reset();
		return DropDownResult.m_SelectedIndex;
	}
	else if(DropDownResult.m_Closed)
	{
		ClosePopupMenu(&State.m_SelectionPopupContext);
		State.m_SelectionPopupContext.Reset();
	}
	else if(State.m_SelectionPopupContext.m_SelectionIndex >= 0)
	{
		const int NewSelection = State.m_SelectionPopupContext.m_SelectionIndex;
		State.m_DropDownState.Reset();
		State.m_SelectionPopupContext.Reset();
		return NewSelection;
	}

	return CurSelection;
}

int CUi::DoDropDown(CUIRect *pRect, int CurSelection, const char *const *pStrs, int Num, SDropDownState &State, bool Enabled)
{
	SDropDownProperties DropDownProps;
	DropDownProps.m_Enabled = Enabled;
	DropDownProps.m_ClosePopupWhenDisabled = false;
	return DoDropDown(pRect, CurSelection, pStrs, Num, State, DropDownProps);
}

CUi::EPopupMenuFunctionResult CUi::PopupColorPicker(void *pContext, CUIRect View, bool Active)
{
	SColorPickerPopupContext *pColorPicker = static_cast<SColorPickerPopupContext *>(pContext);
	CUi *pUI = pColorPicker->m_pUI;
	pColorPicker->m_State = EEditState::NONE;

	CUIRect ColorsArea, HueArea, BottomArea, ModeButtonArea, HueRect, SatRect, ValueRect, HexRect, AlphaRect;

	View.HSplitTop(140.0f, &ColorsArea, &BottomArea);
	ColorsArea.VSplitRight(20.0f, &ColorsArea, &HueArea);
	const CUIRect ColorsHitArea = ColorsArea;

	BottomArea.HSplitTop(3.0f, nullptr, &BottomArea);
	HueArea.VSplitLeft(3.0f, nullptr, &HueArea);
	const CUIRect HueHitArea = HueArea;

	BottomArea.HSplitTop(20.0f, &HueRect, &BottomArea);
	BottomArea.HSplitTop(3.0f, nullptr, &BottomArea);

	constexpr float ValuePadding = 5.0f;
	const float HsvValueWidth = (HueRect.w - ValuePadding * 2) / 3.0f;
	const float HexValueWidth = HsvValueWidth * 2 + ValuePadding;

	HueRect.VSplitLeft(HsvValueWidth, &HueRect, &SatRect);
	SatRect.VSplitLeft(ValuePadding, nullptr, &SatRect);
	SatRect.VSplitLeft(HsvValueWidth, &SatRect, &ValueRect);
	ValueRect.VSplitLeft(ValuePadding, nullptr, &ValueRect);

	BottomArea.HSplitTop(20.0f, &HexRect, &BottomArea);
	BottomArea.HSplitTop(3.0f, nullptr, &BottomArea);
	HexRect.VSplitLeft(HexValueWidth, &HexRect, &AlphaRect);
	AlphaRect.VSplitLeft(ValuePadding, nullptr, &AlphaRect);
	BottomArea.HSplitTop(20.0f, &ModeButtonArea, &BottomArea);

	const ColorRGBA BlackColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f);

	HueArea.Draw(BlackColor, IGraphics::CORNER_NONE, 0.0f);
	HueArea.Margin(1.0f, &HueArea);

	ColorsArea.Draw(BlackColor, IGraphics::CORNER_NONE, 0.0f);
	ColorsArea.Margin(1.0f, &ColorsArea);

	ColorHSVA PickerColorHSV = pColorPicker->m_HsvaColor;
	ColorRGBA PickerColorRGB = pColorPicker->m_RgbaColor;
	ColorHSLA PickerColorHSL = pColorPicker->m_HslaColor;

	// Color Area
	ColorRGBA TL, TR, BL, BR;
	TL = BL = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 0.0f, 1.0f));
	TR = BR = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 1.0f, 1.0f));
	ColorsArea.Draw4(TL, TR, BL, BR, IGraphics::CORNER_NONE, 0.0f);

	TL = TR = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	BL = BR = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
	ColorsArea.Draw4(TL, TR, BL, BR, IGraphics::CORNER_NONE, 0.0f);

	// Hue Area
	static const float s_aaColorIndices[7][3] = {
		{1.0f, 0.0f, 0.0f}, // red
		{1.0f, 0.0f, 1.0f}, // magenta
		{0.0f, 0.0f, 1.0f}, // blue
		{0.0f, 1.0f, 1.0f}, // cyan
		{0.0f, 1.0f, 0.0f}, // green
		{1.0f, 1.0f, 0.0f}, // yellow
		{1.0f, 0.0f, 0.0f}, // red
	};

	const float HuePickerOffset = HueArea.h / 6.0f;
	CUIRect HuePartialArea = HueArea;
	HuePartialArea.h = HuePickerOffset;

	for(size_t j = 0; j < std::size(s_aaColorIndices) - 1; j++)
	{
		TL = ColorRGBA(s_aaColorIndices[j][0], s_aaColorIndices[j][1], s_aaColorIndices[j][2], 1.0f);
		BL = ColorRGBA(s_aaColorIndices[j + 1][0], s_aaColorIndices[j + 1][1], s_aaColorIndices[j + 1][2], 1.0f);

		HuePartialArea.y = HueArea.y + HuePickerOffset * j;
		HuePartialArea.Draw4(TL, TL, BL, BL, IGraphics::CORNER_NONE, 0.0f);
	}

	SValueSelectorProperties ColorValueProps;
	ColorValueProps.m_UseScroll = false;

	const auto &&RenderAlphaSelector = [&](unsigned OldA) -> SEditResult<int64_t> {
		if(pColorPicker->m_Alpha)
		{
			return pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[3], &AlphaRect, "A:", OldA, 0, 255, ColorValueProps);
		}
		else
		{
			char aBuf[8];
			str_format(aBuf, sizeof(aBuf), "A: %d", OldA);
			pUI->DoLabel(&AlphaRect, aBuf, 10.0f, TEXTALIGN_MC);
			AlphaRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.65f), IGraphics::CORNER_ALL, ui_token::radius::TIGHT);
			return {EEditState::NONE, OldA};
		}
	};

	// Editboxes Area
	if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_HSVA)
	{
		const unsigned OldH = round_to_int(PickerColorHSV.h * 255.0f);
		const unsigned OldS = round_to_int(PickerColorHSV.s * 255.0f);
		const unsigned OldV = round_to_int(PickerColorHSV.v * 255.0f);
		const unsigned OldA = round_to_int(PickerColorHSV.a * 255.0f);

		const auto [StateH, H] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[0], &HueRect, "H:", OldH, 0, 255, ColorValueProps);
		const auto [StateS, S] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[1], &SatRect, "S:", OldS, 0, 255, ColorValueProps);
		const auto [StateV, V] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[2], &ValueRect, "V:", OldV, 0, 255, ColorValueProps);
		const auto [StateA, A] = RenderAlphaSelector(OldA);

		if(OldH != H || OldS != S || OldV != V || OldA != A)
		{
			PickerColorHSV = ColorHSVA(H / 255.0f, S / 255.0f, V / 255.0f, A / 255.0f);
			PickerColorHSL = color_cast<ColorHSLA>(PickerColorHSV);
			PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		}

		for(auto State : {StateH, StateS, StateV, StateA})
		{
			if(State != EEditState::NONE)
			{
				pColorPicker->m_State = State;
				break;
			}
		}
	}
	else if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_RGBA)
	{
		const unsigned OldR = round_to_int(PickerColorRGB.r * 255.0f);
		const unsigned OldG = round_to_int(PickerColorRGB.g * 255.0f);
		const unsigned OldB = round_to_int(PickerColorRGB.b * 255.0f);
		const unsigned OldA = round_to_int(PickerColorRGB.a * 255.0f);

		const auto [StateR, R] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[0], &HueRect, "R:", OldR, 0, 255, ColorValueProps);
		const auto [StateG, G] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[1], &SatRect, "G:", OldG, 0, 255, ColorValueProps);
		const auto [StateB, B] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[2], &ValueRect, "B:", OldB, 0, 255, ColorValueProps);
		const auto [StateA, A] = RenderAlphaSelector(OldA);

		if(OldR != R || OldG != G || OldB != B || OldA != A)
		{
			PickerColorRGB = ColorRGBA(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f);
			PickerColorHSL = color_cast<ColorHSLA>(PickerColorRGB);
			PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
		}

		for(auto State : {StateR, StateG, StateB, StateA})
		{
			if(State != EEditState::NONE)
			{
				pColorPicker->m_State = State;
				break;
			}
		}
	}
	else if(pColorPicker->m_ColorMode == SColorPickerPopupContext::MODE_HSLA)
	{
		const unsigned OldH = round_to_int(PickerColorHSL.h * 255.0f);
		const unsigned OldS = round_to_int(PickerColorHSL.s * 255.0f);
		const unsigned OldL = round_to_int(PickerColorHSL.l * 255.0f);
		const unsigned OldA = round_to_int(PickerColorHSL.a * 255.0f);

		const auto [StateH, H] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[0], &HueRect, "H:", OldH, 0, 255, ColorValueProps);
		const auto [StateS, S] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[1], &SatRect, "S:", OldS, 0, 255, ColorValueProps);
		const auto [StateL, L] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[2], &ValueRect, "L:", OldL, 0, 255, ColorValueProps);
		const auto [StateA, A] = RenderAlphaSelector(OldA);

		if(OldH != H || OldS != S || OldL != L || OldA != A)
		{
			PickerColorHSL = ColorHSLA(H / 255.0f, S / 255.0f, L / 255.0f, A / 255.0f);
			PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
			PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		}

		for(auto State : {StateH, StateS, StateL, StateA})
		{
			if(State != EEditState::NONE)
			{
				pColorPicker->m_State = State;
				break;
			}
		}
	}
	else
	{
		dbg_assert_failed("Color picker mode invalid: %d", (int)pColorPicker->m_ColorMode);
	}

	SValueSelectorProperties Props;
	Props.m_UseScroll = false;
	Props.m_IsHex = true;
	Props.m_HexPrefix = pColorPicker->m_Alpha ? 8 : 6;
	const unsigned OldHex = PickerColorRGB.PackAlphaLast(pColorPicker->m_Alpha);
	auto [HexState, Hex] = pUI->DoValueSelectorWithState(&pColorPicker->m_aValueSelectorIds[4], &HexRect, "Hex:", OldHex, 0, pColorPicker->m_Alpha ? 0xFFFFFFFFll : 0xFFFFFFll, Props);
	if(OldHex != Hex)
	{
		const float OldAlpha = PickerColorRGB.a;
		PickerColorRGB = ColorRGBA::UnpackAlphaLast<ColorRGBA>(Hex, pColorPicker->m_Alpha);
		if(!pColorPicker->m_Alpha)
			PickerColorRGB.a = OldAlpha;
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorRGB);
		PickerColorHSV = color_cast<ColorHSVA>(PickerColorHSL);
	}

	if(HexState != EEditState::NONE)
		pColorPicker->m_State = HexState;

	// Logic
	float PickerX, PickerY;
	EEditState ColorPickerRes = pUI->DoPickerLogic(&pColorPicker->m_ColorPickerId, &ColorsHitArea, &PickerX, &PickerY);
	if(ColorPickerRes != EEditState::NONE)
	{
		const float ColorX = std::clamp(PickerX - (ColorsArea.x - ColorsHitArea.x), 0.0f, ColorsArea.w);
		const float ColorY = std::clamp(PickerY - (ColorsArea.y - ColorsHitArea.y), 0.0f, ColorsArea.h);
		PickerColorHSV.y = ColorX / ColorsArea.w;
		PickerColorHSV.z = 1.0f - ColorY / ColorsArea.h;
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorHSV);
		PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		pColorPicker->m_State = ColorPickerRes;
	}

	EEditState HuePickerRes = pUI->DoPickerLogic(&pColorPicker->m_HuePickerId, &HueHitArea, &PickerX, &PickerY);
	if(HuePickerRes != EEditState::NONE)
	{
		const float HueY = std::clamp(PickerY - (HueArea.y - HueHitArea.y), 0.0f, HueArea.h);
		PickerColorHSV.x = 1.0f - HueY / HueArea.h;
		PickerColorHSL = color_cast<ColorHSLA>(PickerColorHSV);
		PickerColorRGB = color_cast<ColorRGBA>(PickerColorHSL);
		pColorPicker->m_State = HuePickerRes;
	}

	// Marker Color Area
	const float MarkerX = ColorsArea.x + ColorsArea.w * PickerColorHSV.y;
	const float MarkerY = ColorsArea.y + ColorsArea.h * (1.0f - PickerColorHSV.z);

	const float MarkerOutlineInd = PickerColorHSV.z > 0.5f ? 0.0f : 1.0f;
	const ColorRGBA MarkerOutline = ColorRGBA(MarkerOutlineInd, MarkerOutlineInd, MarkerOutlineInd, 1.0f);

	const CUIRect ColorMarker{MarkerX - 4.5f, MarkerY - 4.5f, 9.0f, 9.0f};
	DrawRoundedSurface(pUI, ColorMarker, PickerColorRGB, MarkerOutline, 4.5f, 1.0f);

	// Marker Hue Area
	CUIRect HueMarker;
	HueArea.Margin(-2.5f, &HueMarker);
	HueMarker.h = 6.5f;
	HueMarker.y = (HueArea.y + HueArea.h * (1.0f - PickerColorHSV.x)) - HueMarker.h / 2.0f;

	const ColorRGBA HueMarkerColor = color_cast<ColorRGBA>(ColorHSVA(PickerColorHSV.x, 1.0f, 1.0f, 1.0f));
	const float HueMarkerOutlineColor = PickerColorHSV.x > 0.75f ? 1.0f : 0.0f;
	const ColorRGBA HueMarkerOutline = ColorRGBA(HueMarkerOutlineColor, HueMarkerOutlineColor, HueMarkerOutlineColor, 1.0f);

	DrawRoundedSurface(pUI, HueMarker, HueMarkerColor, HueMarkerOutline, 1.2f, 1.2f);

	pColorPicker->m_HsvaColor = PickerColorHSV;
	pColorPicker->m_RgbaColor = PickerColorRGB;
	pColorPicker->m_HslaColor = PickerColorHSL;
	if(pColorPicker->m_pHslaColor != nullptr)
		*pColorPicker->m_pHslaColor = PickerColorHSL.Pack(pColorPicker->m_Alpha);

	static constexpr SColorPickerPopupContext::EColorPickerMode PICKER_MODES[] = {SColorPickerPopupContext::MODE_HSVA, SColorPickerPopupContext::MODE_RGBA, SColorPickerPopupContext::MODE_HSLA};
	static constexpr const char *PICKER_MODE_LABELS[] = {"HSVA", "RGBA", "HSLA"};
	static_assert(std::size(PICKER_MODES) == std::size(PICKER_MODE_LABELS));
	for(SColorPickerPopupContext::EColorPickerMode Mode : PICKER_MODES)
	{
		CUIRect ModeButton;
		ModeButtonArea.VSplitLeft(HsvValueWidth, &ModeButton, &ModeButtonArea);
		ModeButtonArea.VSplitLeft(ValuePadding, nullptr, &ModeButtonArea);
		if(pUI->DoButton_PopupMenu(&pColorPicker->m_aModeButtons[(int)Mode], PICKER_MODE_LABELS[Mode], &ModeButton, 10.0f, TEXTALIGN_MC, 2.0f, false, pColorPicker->m_ColorMode != Mode))
		{
			pColorPicker->m_ColorMode = Mode;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

void CUi::ShowPopupColorPicker(float X, float Y, SColorPickerPopupContext *pContext)
{
	pContext->m_pUI = this;
	if(pContext->m_ColorMode == SColorPickerPopupContext::MODE_UNSET)
		pContext->m_ColorMode = SColorPickerPopupContext::MODE_HSVA;
	SPopupMenuProperties PopupProps;
	PopupProps.m_BlockUnderlyingPointerInput = true;
	PopupProps.m_BlockUnderlyingScroll = true;
	DoPopupMenu(pContext, X, Y, 160.0f + 10.0f, 209.0f + 10.0f, pContext, PopupColorPicker, PopupProps);
}
