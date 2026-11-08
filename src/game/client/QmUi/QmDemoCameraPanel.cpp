#include <game/client/components/menus.h>
#include <game/client/components/qmclient/demo_camera.h>
#include <game/client/components/qmclient/demo_cut.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

void CMenus::RenderDemoCameraPanel(CUIRect View, bool Enabled)
{
	auto &Camera = GameClient()->m_DemoCamera;
	View.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.05f), IGraphics::CORNER_ALL, 8.0f);
	View.Margin(6.0f, &View);
	CUIRect Row, Label, Button;
	View.HSplitTop(18.0f, &Row, &View);
	Row.VSplitLeft(Row.w * 0.6f, &Label, &Button);
	Ui()->DoLabel(&Label, Localize("Camera path"), 11.0f, TEXTALIGN_ML);
	static CButtonContainer s_Enabled;
	if(DoButton_Menu(&s_Enabled, Localize("Enable camera path"), Enabled ? Camera.Enabled() : -1, &Button, BUTTONFLAG_LEFT,
		   nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, Camera.Enabled() ? ui_token::color::ACCENT_PRIMARY_DIM : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), nullptr, 10.0f) &&
		Enabled)
		Camera.SetEnabled(!Camera.Enabled());

	static CButtonContainer s_aButtons[10];
	int ButtonIndex = 0;
	const auto TextButton = [&](const char *pText, int Index, bool Available, bool Active = false) {
		const ColorRGBA Fill = Active ? ui_token::color::ACCENT_PRIMARY_DIM : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f);
		const bool Result = DoButton_Menu(&s_aButtons[Index], pText, Enabled && Available ? static_cast<int>(Active) : -1, &Button,
					    BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, Fill, nullptr, 10.0f) != 0;
		GameClient()->m_Tooltips.DoToolTip(&s_aButtons[Index], &Button, pText);
		return Result && Enabled && Available;
	};
	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(22.0f, &Row, &View);
	const float ActionWidth = (Row.w - 9.0f) / 4.0f;
	const auto NextButton = [&](float Width) {
		Row.VSplitLeft(Width, &Button, &Row);
		Row.VSplitLeft(3.0f, nullptr, &Row);
	};
	NextButton(ActionWidth);
	if(TextButton(Localize("Edit camera"), ButtonIndex++, true, Camera.Editing()))
		Camera.BeginEditing();
	NextButton(ActionWidth);
	if(TextButton(Localize("Add keyframe"), ButtonIndex++, Camera.Editing()))
		Camera.AddKeyframe();
	NextButton(ActionWidth);
	const bool Previewing = Camera.Enabled() && !Camera.Editing() && !PlaybackPlayer()->BaseInfo()->m_Paused;
	if(TextButton(Previewing ? Localize("Stop preview") : Localize("Preview path"), ButtonIndex++, !Camera.Track().Empty()))
	{
		if(Previewing)
			Camera.BeginEditing();
		else
			Camera.Preview();
	}
	NextButton(ActionWidth);
	if(TextButton(Localize("Show path"), ButtonIndex++, true, Camera.ShowGuides()))
		Camera.ToggleGuides();

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(22.0f, &Row, &View);
	const float EditWidth = (Row.w - 12.0f) / 5.0f;
	NextButton(EditWidth);
	if(TextButton(Localize("Previous"), ButtonIndex++, Camera.Selected() > 0))
		Camera.Select(Camera.Selected() - 1);
	NextButton(EditWidth);
	if(TextButton(Localize("Next"), ButtonIndex++, Camera.Selected() >= 0 && Camera.Selected() + 1 < static_cast<int>(Camera.Track().Frames().size())))
		Camera.Select(Camera.Selected() + 1);
	NextButton(EditWidth);
	if(TextButton(Localize("Go to keyframe"), ButtonIndex++, Camera.Selected() >= 0))
		Camera.GoToKeyframe();
	NextButton(EditWidth);
	if(TextButton(Localize("Update keyframe"), ButtonIndex++, Camera.Selected() >= 0 && Camera.Editing()))
		Camera.UpdateKeyframe();
	NextButton(EditWidth);
	if(TextButton(Localize("Delete"), ButtonIndex++, Camera.Selected() >= 0))
		Camera.RemoveKeyframe();

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(20.0f, &Row, &View);
	Row.VSplitRight(std::min(160.0f, Row.w * 0.4f), &Label, &Button);
	char aStatus[128];
	if(Camera.Selected() >= 0)
	{
		const auto &Frame = Camera.Track().Frames()[Camera.Selected()];
		char aTime[32];
		str_time(qm_demo_cut::ToCentiseconds(static_cast<int64_t>(Frame.m_Tick - PlaybackPlayer()->BaseInfo()->m_FirstTick), Client()->GameTickSpeed()), TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
		str_format(aStatus, sizeof(aStatus), Localize("Keyframe %d/%d: %s"), Camera.Selected() + 1, static_cast<int>(Camera.Track().Frames().size()), aTime);
		// 相机过渡的「停住」不能和工坊的 Hold（按住）共用同一个 Localize identity：
		// 共用会让两处抢同一份译文，术语修一处就污染另一处。
		const char *pTransition = Frame.m_Transition == qm_demo_camera::ETransition::SMOOTH ? Localize("Smooth") :
					  Frame.m_Transition == qm_demo_camera::ETransition::LINEAR ? Localize("Linear") :
												      Localize("Hold still");
		if(TextButton(pTransition, ButtonIndex++, true))
			Camera.CycleTransition();
	}
	else
		str_copy(aStatus, Localize("No camera keyframes"));
	Ui()->DoLabel(&Label, aStatus, 10.0f, TEXTALIGN_ML, {.m_MaxWidth = Label.w - 4.0f, .m_EllipsisAtEnd = true});

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	const char *pStatus = Camera.LoadFailed() ? Localize("Failed to load camera path") :
			      Camera.SaveFailed() ? Localize("Failed to save camera path") :
			      Camera.EditFailed() ? Localize("Unable to record camera keyframe") :
						    Localize("Move the free camera, then add a keyframe at the current demo time.");
	if(Camera.Dirty())
	{
		Row.VSplitRight(std::min(80.0f, Row.w * 0.3f), &Row, &Button);
		static CButtonContainer s_Save;
		if(DoButton_Menu(&s_Save, Localize("Save"), Enabled ? 0 : -1, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), nullptr, 10.0f) && Enabled)
			Camera.Save();
	}
	Ui()->DoLabel(&Row, pStatus, 10.0f, TEXTALIGN_ML, {.m_MaxWidth = Row.w - 4.0f, .m_EllipsisAtEnd = true});
}
