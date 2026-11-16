#include "QmSearchAnswerCard.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/input.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsCardContentBridge.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/components/qmclient/search_answers.h>

#include <algorithm>
#include <unordered_map>

namespace
{
	struct SAnswerButtons
	{
		CButtonContainer m_Select;
		CButtonContainer m_CopyAnswer;
		CButtonContainer m_CopyCommand;
		CButtonContainer m_OpenDirectory;
		const char *m_pStatus = "";
	};
}

void qm_card_catalog::BuildSearchAnswerCard(const SSearchAnswerContext &Ctx, const qm_search_answers::SResult &Result, bool ReadOnly, const SSettingsContentMetrics &Metrics, const std::function<void(const char *)> &SelectQuestion, SSettingsCardDefinition &Out)
{
	Out.m_Spec = {SEARCH_ANSWER_CARD_ID, "常见问题", "输入常见问法可以查看说明，并在下方直接调整相关设置。"};
	Out.m_RenderMeasured = [Ctx, Result, ReadOnly, Metrics, SelectQuestion](CUIRect &Content) {
		// 条目地址属于静态问答表；按钮按条目区分，候选重排不会复用其他问题的按下状态。
		static std::unordered_map<const qm_search_answers::SAnswer *, SAnswerButtons> s_Buttons;
		CSettingsContentLayoutFlow Flow(Content);
		const bool Interactive = Flow.Input() && !ReadOnly && !Ctx.m_pUi->RenderOnly() && !Ctx.m_pUi->MeasurementOnly() && !Ctx.m_pUi->BackgroundRender() && !Ctx.m_pUi->IsPopupOpen();
		const auto DrawText = [Ctx, Metrics](const CUIRect &Rect, const char *pText) {
			SLabelProperties Props;
			Props.m_MaxWidth = Rect.w;
			Props.m_EnableWidthCheck = false;
			Ctx.m_pUi->DoLabel(&Rect, pText, Metrics.m_BodySize, TEXTALIGN_ML, Props);
		};
		const auto Text = [&](const char *pId, const char *pText) {
			Flow.Text(pId, pText, 0, Metrics.m_BodySize, Metrics.m_LineSpacing, {}, DrawText, Metrics.m_LineHeight);
		};
		const auto Button = [&](const char *pId, const char *pLabel, CButtonContainer &Id) {
			CUIRect Row;
			bool Clicked = false;
			if(Flow.MeasuredControl(pId, pLabel, Metrics.m_ButtonHeight, Metrics.m_BodySize, 0, 0, 4.0f, Row,
				   [Ctx](const SSettingsContentRowDeclaration &Declaration, float Width) {
					   return Ctx.m_pTextRender->TextBoundingBox(Declaration.m_FontSize, Declaration.m_Text.c_str(), -1, std::max(1.0f, Width - 8.0f)).m_H + 8.0f;
				   }))
			{
				if(Interactive)
					Clicked = Ctx.m_pUi->DoButtonLogic(&Id, 0, &Row, BUTTONFLAG_LEFT);
				if(Flow.Drawing())
				{
					ui_widget::DrawButtonSurface(ui_widget::ControlContext(Ctx.m_pUi), &Id, Row);
					Row.Margin(4.0f, &Row);
					DrawText(Row, pLabel);
				}
			}
			Flow.Space(Metrics.m_LineSpacing);
			return Clicked;
		};

		if(Result.m_pAnswer == nullptr)
		{
			Text("answer-choose", "你可能想问以下问题，点击查看答案：");
			for(const auto *pCandidate : Result.m_vCandidates)
			{
				if(Button(pCandidate->m_pId, pCandidate->m_pQuestion, s_Buttons[pCandidate].m_Select))
					SelectQuestion(pCandidate->m_pQuestion);
			}
			return;
		}

		const auto *pAnswer = Result.m_pAnswer;
		auto &Buttons = s_Buttons[pAnswer];
		Text("answer-question", pAnswer->m_pQuestion);
		Text("answer-body", pAnswer->m_pAnswer);
		if(Button("answer-copy", "复制答案", Buttons.m_CopyAnswer))
		{
			Ctx.m_pInput->SetClipboardText(pAnswer->m_pAnswer);
			Buttons.m_pStatus = "已复制答案";
		}
		if(pAnswer->m_pCommand != nullptr)
		{
			Text("answer-command", pAnswer->m_pCommand);
			if(Button("answer-copy-command", "复制命令", Buttons.m_CopyCommand))
			{
				Ctx.m_pInput->SetClipboardText(pAnswer->m_pCommand);
				Buttons.m_pStatus = "已复制命令，请检查内容后在控制台使用";
			}
		}
		if(pAnswer->m_pDirectory != nullptr && Button("answer-open-directory", "打开目录", Buttons.m_OpenDirectory))
		{
			if(Ctx.m_pStorage->FolderExists(pAnswer->m_pDirectory, IStorage::TYPE_SAVE))
			{
				char aPath[IO_MAX_PATH_LENGTH];
				Ctx.m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, pAnswer->m_pDirectory, aPath, sizeof(aPath));
				Buttons.m_pStatus = Ctx.m_pClient->ViewFile(aPath) ? "已请求打开目录" : "无法打开目录";
			}
			else
				Buttons.m_pStatus = "目录尚未生成，请先按说明使用对应功能";
		}
		if(Buttons.m_pStatus[0] != '\0')
			Text("answer-status", Buttons.m_pStatus);
	};
	qm_settings_content::UseDeclaredContent(Out, Ctx.m_pUi, Ctx.m_pTextRender);
}
