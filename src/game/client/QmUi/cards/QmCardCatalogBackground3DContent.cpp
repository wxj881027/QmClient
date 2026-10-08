#include "QmCardCatalogInternal.h"

#include <base/lock.h>
#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/UiTokens.h>
#include <game/client/components/binds.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/qmclient/qm_music_hook_registry.h>
#include <game/client/components/qmclient/qmclient_utils.h>
#include <game/client/components/qmclient/translate/translate_backend.h>
#include <game/client/components/qmclient/translate/translate_ui_common.h>
#include <game/client/components/qmclient/translate/translate_ui_settings.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;

extern std::unordered_map<std::string, CBindSlot> g_CommandBindCache;

void CMenus::RenderQmHudBackground3DContent(CUIRect &Content, const SSettingsContentMetrics &Metrics, float LabelWidth, bool PrewarmOnly)
{
	const float LineHeight = Metrics.m_LineHeight;
	const float BodySize = Metrics.m_BodySize;
	const float LineSpacing = Metrics.m_LineSpacing;
	// 单选项在正式绘制阶段提交，分支仍按本帧测量时的模式绘制。
	const int ColorModeForLayout = g_Config.m_Qm3DParticlesColorMode;
	CUIRect Row, LabelCol, ControlCol;
	auto DoQmSettingsCheckboxAuto = [this](const void *pId, const char *pTextId, const char *pText, int *pValue, CUIRect *pRect, float) {
		const bool Changed = DoSettingsButton_CheckBox(SETTINGS_QMCLIENT, QMCLIENT_SETTINGS_TAB_HUD, QMCLIENT_SETTINGS_TAB_HUD, pId, pTextId, pText, *pValue, pRect) != 0;
		if(Changed)
			*pValue ^= 1;
		return Changed;
	};
	auto DoQmSettingsLabel = [this](const char *pTextId, CUIRect *pRect, const char *pText, float FontSize) {
		RenderQmHudLabel(pTextId, pRect, pText, FontSize);
	};
	auto RenderSliderWithValueInput = [this, PrewarmOnly](const void *pId, const CUIRect &ControlColumn, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "") {
		RenderQmSettingsSliderWithValueInput(pId, ControlColumn, pValue, MinValue, MaxValue, pSuffix, PrewarmOnly);
	};

	auto RenderIntOption = [&](const void *pId, const char *pLabel, int *pValue, int MinValue, int MaxValue, const char *pSuffix = "", bool TrailingSpacing = true) {
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		Ui()->DoLabel(&LabelCol, pLabel, BodySize, TEXTALIGN_ML);
		RenderSliderWithValueInput(pId, ControlCol, pValue, MinValue, MaxValue, pSuffix);
		if(TrailingSpacing)
			Content.HSplitTop(LineSpacing, nullptr, &Content);
	};

	Content.HSplitTop(LineHeight, &Row, &Content);
	DoQmSettingsCheckboxAuto(&g_Config.m_Qm3DParticles, "Enable 3D background particles", Localize("Enable 3D background particles"), &g_Config.m_Qm3DParticles, &Row, LineHeight);
	if(g_Config.m_Qm3DParticles)
		Content.HSplitTop(LineSpacing, nullptr, &Content);

	if(g_Config.m_Qm3DParticles)
	{
		Content.HSplitTop(LineHeight, &Row, &Content);
		Row.VSplitLeft(LabelWidth, &LabelCol, &ControlCol);
		DoQmSettingsLabel("qmclient-3d-background-particle-type", &LabelCol, Localize("Particle type"), BodySize);
		std::array<const char *, 9> apQm3DParticleTypeNames = {
			Localize("Cube"),
			Localize("Heart"),
			Localize("Sphere"),
			Localize("Pyramid"),
			Localize("Diamond"),
			Localize("Ring"),
			Localize("Star"),
			Localize("Crescent"),
			Localize("Mixed"),
		};
		const std::array<int, 9> aQm3DParticleTypeValues = {1, 2, 4, 5, 6, 7, 8, 9, 3};
		int TypeIndex = 0;
		for(size_t TypeValueIndex = 0; TypeValueIndex < aQm3DParticleTypeValues.size(); ++TypeValueIndex)
		{
			if(aQm3DParticleTypeValues[TypeValueIndex] == g_Config.m_Qm3DParticlesType)
			{
				TypeIndex = (int)TypeValueIndex;
				break;
			}
		}
		static CUi::SDropDownState s_Qm3DParticleTypeDropDownState;
		static CScrollRegion s_Qm3DParticleTypeDropDownScrollRegion;
		s_Qm3DParticleTypeDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_Qm3DParticleTypeDropDownScrollRegion;
		const int NewTypeIndex = DoSettingsDropDown(&ControlCol, TypeIndex, apQm3DParticleTypeNames.data(), static_cast<int>(apQm3DParticleTypeNames.size()), s_Qm3DParticleTypeDropDownState);
		if(NewTypeIndex >= 0 && NewTypeIndex < static_cast<int>(aQm3DParticleTypeValues.size()) && NewTypeIndex != TypeIndex)
			g_Config.m_Qm3DParticlesType = aQm3DParticleTypeValues[NewTypeIndex];
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		static int s_Qm3DParticleCountInputId;
		RenderIntOption(&s_Qm3DParticleCountInputId, Localize("Particle count"), &g_Config.m_Qm3DParticlesCount, 1, 200);
		static int s_Qm3DParticleAlphaInputId;
		RenderIntOption(&s_Qm3DParticleAlphaInputId, Localize("Particle alpha"), &g_Config.m_Qm3DParticlesAlpha, 1, 100, "%");
		static int s_Qm3DParticleMinSizeInputId;
		RenderIntOption(&s_Qm3DParticleMinSizeInputId, Localize("Min size"), &g_Config.m_Qm3DParticlesSizeMin, 2, 64);
		if(!PrewarmOnly && !Ui()->RenderOnly() && g_Config.m_Qm3DParticlesSizeMax < g_Config.m_Qm3DParticlesSizeMin)
			g_Config.m_Qm3DParticlesSizeMax = g_Config.m_Qm3DParticlesSizeMin;
		static int s_Qm3DParticleMaxSizeInputId;
		RenderIntOption(&s_Qm3DParticleMaxSizeInputId, Localize("Max size"), &g_Config.m_Qm3DParticlesSizeMax, g_Config.m_Qm3DParticlesSizeMin, 64);
		static int s_Qm3DParticleSpeedInputId;
		RenderIntOption(&s_Qm3DParticleSpeedInputId, Localize("Particle speed"), &g_Config.m_Qm3DParticlesSpeed, 1, 500);
		static int s_Qm3DParticleDepthInputId;
		RenderIntOption(&s_Qm3DParticleDepthInputId, Localize("Particle depth"), &g_Config.m_Qm3DParticlesDepth, 10, 1000);
		static int s_Qm3DParticleViewMarginInputId;
		RenderIntOption(&s_Qm3DParticleViewMarginInputId, Localize("View margin"), &g_Config.m_Qm3DParticlesViewMargin, 0, 1000);
		static int s_Qm3DParticleFadeInInputId;
		RenderIntOption(&s_Qm3DParticleFadeInInputId, Localize("Fade in"), &g_Config.m_Qm3DParticlesFadeInMs, 1, 5000, "ms");
		static int s_Qm3DParticleFadeOutInputId;
		RenderIntOption(&s_Qm3DParticleFadeOutInputId, Localize("Fade out"), &g_Config.m_Qm3DParticlesFadeOutMs, 1, 5000, "ms");

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_Qm3DParticlesCollide, "Particle collision", Localize("Particle collision"), &g_Config.m_Qm3DParticlesCollide, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		static int s_Qm3DParticlePushRadiusInputId;
		RenderIntOption(&s_Qm3DParticlePushRadiusInputId, Localize("Push radius"), &g_Config.m_Qm3DParticlesPushRadius, 0, 1000);
		static int s_Qm3DParticlePushStrengthInputId;
		RenderIntOption(&s_Qm3DParticlePushStrengthInputId, Localize("Push strength"), &g_Config.m_Qm3DParticlesPushStrength, 0, 2000);

		static std::vector<CButtonContainer> s_vQm3DParticleColorModeButtons = {{}, {}};
		int ColorMode = g_Config.m_Qm3DParticlesColorMode;
		if(DoSettingsLine_RadioMenu(SETTINGS_QMCLIENT, m_QmClientSettingsTab, m_QmClientSettingsTab, Content, "qmclient-3d-particle-color-mode-label", Localize("Particle color"), s_vQm3DParticleColorModeButtons, {"qmclient-3d-particle-color-custom", "qmclient-3d-particle-color-random"}, {Localize("Custom"), Localize("Random")}, {1, 2}, ColorMode, Metrics))
			g_Config.m_Qm3DParticlesColorMode = ColorMode;
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		if(ColorModeForLayout == 1)
		{
			static CButtonContainer s_Qm3DParticleColorId;
			DoLine_ColorPicker(&s_Qm3DParticleColorId, Metrics, &Content, Localize("Particle color"), &g_Config.m_Qm3DParticlesColor, ColorRGBA(0.56f, 0.72f, 0.62f, 1.0f), false, nullptr, true);
		}

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_Qm3DParticlesGlow, "Particle glow", Localize("Particle glow"), &g_Config.m_Qm3DParticlesGlow, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		if(g_Config.m_Qm3DParticlesGlow)
		{
			static int s_Qm3DParticleGlowAlphaInputId;
			RenderIntOption(&s_Qm3DParticleGlowAlphaInputId, Localize("Glow alpha"), &g_Config.m_Qm3DParticlesGlowAlpha, 1, 100, "%");
			static int s_Qm3DParticleGlowOffsetInputId;
			RenderIntOption(&s_Qm3DParticleGlowOffsetInputId, Localize("Glow offset"), &g_Config.m_Qm3DParticlesGlowOffset, 1, 20);
		}

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_Qm3DParticlesTrail, "Particle trail", Localize("Particle trail"), &g_Config.m_Qm3DParticlesTrail, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		if(g_Config.m_Qm3DParticlesTrail)
		{
			static int s_Qm3DParticleTrailLengthInputId;
			RenderIntOption(&s_Qm3DParticleTrailLengthInputId, Localize("Trail length"), &g_Config.m_Qm3DParticlesTrailLength, 2, 6);
			static int s_Qm3DParticleTrailAlphaInputId;
			RenderIntOption(&s_Qm3DParticleTrailAlphaInputId, Localize("Trail alpha"), &g_Config.m_Qm3DParticlesTrailAlpha, 1, 100, "%");
		}

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_Qm3DParticlesPulse, "Particle pulse", Localize("Particle pulse"), &g_Config.m_Qm3DParticlesPulse, &Row, LineHeight);
		Content.HSplitTop(LineSpacing, nullptr, &Content);

		if(g_Config.m_Qm3DParticlesPulse)
		{
			static int s_Qm3DParticlePulseStrengthInputId;
			RenderIntOption(&s_Qm3DParticlePulseStrengthInputId, Localize("Pulse strength"), &g_Config.m_Qm3DParticlesPulseStrength, 0, 50, "%");
			static int s_Qm3DParticlePulseSpeedInputId;
			RenderIntOption(&s_Qm3DParticlePulseSpeedInputId, Localize("Pulse speed"), &g_Config.m_Qm3DParticlesPulseSpeed, 10, 300, "%");
		}

		Content.HSplitTop(LineHeight, &Row, &Content);
		DoQmSettingsCheckboxAuto(&g_Config.m_Qm3DParticlesTwinkle, "Particle twinkle", Localize("Particle twinkle"), &g_Config.m_Qm3DParticlesTwinkle, &Row, LineHeight);

		if(g_Config.m_Qm3DParticlesTwinkle)
		{
			Content.HSplitTop(LineSpacing, nullptr, &Content);
			static int s_Qm3DParticleTwinkleStrengthInputId;
			RenderIntOption(&s_Qm3DParticleTwinkleStrengthInputId, Localize("Twinkle strength"), &g_Config.m_Qm3DParticlesTwinkleStrength, 0, 100, "%", false);
		}
	}
}
