#include "QmSearchAnswerCard.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/input.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/QmCardWikiPanel.h>
#include <game/client/QmUi/QmWikiDocs.h>
#include <game/client/QmUi/SettingsCardContentBridge.h>
#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/QmUi/UiContext.h>
#include <game/client/components/qmclient/search_answers.h>
#include <game/client/components/tooltips.h>
#include <game/client/qm_icon.h>

#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace
{
	struct SAnswerButtons
	{
		CButtonContainer m_Select;
		CButtonContainer m_CopyCommand;
		CButtonContainer m_OpenDirectory;
		const char *m_pStatus = "";
	};

	void SetCopyAction(SSettingsCardDefinition &Card, IUiContext Ctx, const void *pId, const SSettingsContentMetrics &Metrics, bool ReadOnly, std::function<bool()> Copy)
	{
		Card.m_LeadingHeaderActionWidth = ui_token::settings::CARD_HANDLE_SIZE * Metrics.m_UiScale;
		Card.m_LeadingHeaderAction = [Ctx, pId, FontSize = Metrics.m_SmallSize](const SSettingsCardFrame &Frame, bool) {
			const CUIRect Button = QmUiSquareIconButtonRect(Frame.m_LeadingHeaderActionRect);
			if(Button.w <= 0.0f)
				return;
			RenderSettingsCardHeaderIcon(Ctx, Button, EQmIcon::COPY, FontIcons::FONT_ICON_COPY, 1.0f, pId);
			if(Ctx.m_pTooltips != nullptr)
				Ctx.m_pTooltips->DoSmallToolTip(pId, &Button, "复制答案", FontSize);
		};
		Card.m_LeadingHeaderInput = [pUi = Ctx.m_pUi, pId, ReadOnly, Copy = std::move(Copy)](const SSettingsCardFrame &Frame, bool) {
			if(ReadOnly || !pUi->PreLayoutInput() || pUi->RenderOnly() || pUi->IsPopupOpen())
				return false;
			const CUIRect Button = QmUiSquareIconButtonRect(Frame.m_LeadingHeaderActionRect);
			return Button.w > 0.0f && pUi->DoButtonLogic(pId, 0, &Button, BUTTONFLAG_LEFT) && Copy();
		};
	}
}

struct qm_card_catalog::SSearchAnswerCardState
{
	std::unordered_map<const qm_search_answers::SAnswer *, SAnswerButtons> m_Buttons;
};

std::shared_ptr<qm_card_catalog::SSearchAnswerCardState> qm_card_catalog::CreateSearchAnswerCardState()
{
	return std::make_shared<SSearchAnswerCardState>();
}

void qm_card_catalog::BuildSearchAnswerCard(const SSearchAnswerContext &Ctx, const qm_search_answers::SResult &Result, bool ReadOnly, const SSettingsContentMetrics &Metrics, const std::function<void(const char *)> &SelectQuestion, SSettingsCardDefinition &Out, const std::shared_ptr<SSearchAnswerCardState> &State)
{
	const auto pState = State ? State : [] {
		static const auto s_pState = CreateSearchAnswerCardState();
		return s_pState;
	}();
	// 声明、输入和绘制只借用已准备的节点；重排不改变按钮身份。
	if(Result.m_pAnswer != nullptr)
		pState->m_Buttons.try_emplace(Result.m_pAnswer);
	else
		for(const auto *pCandidate : Result.m_vCandidates)
			pState->m_Buttons.try_emplace(pCandidate);

	const std::function<void(const CUIRect &, const char *)> DrawText = [Ctx, Metrics](const CUIRect &Rect, const char *pText) {
		SLabelProperties Props;
		Props.m_MaxWidth = Rect.w;
		Props.m_EnableWidthCheck = false;
		Ctx.m_pUi->DoLabel(&Rect, pText, Metrics.m_BodySize, TEXTALIGN_ML, Props);
	};
	const CSettingsContentLayoutCache::FMeasureControlText MeasureButton = [pText = Ctx.m_pTextRender](const SSettingsContentRowDeclaration &Declaration, float Width) {
		return pText->TextBoundingBox(Declaration.m_FontSize, Declaration.m_Text.c_str(), -1, std::max(1.0f, Width - 8.0f)).m_H + 8.0f;
	};
	SSettingsCardDefinition Candidate;
	Candidate.m_Spec = {SEARCH_ANSWER_CARD_ID, "常见问题", "输入常见问法可以查看说明，并在下方直接调整相关设置。"};
	if(const auto *pAnswer = Result.m_pAnswer)
	{
		auto HeaderCtx = ui_widget::ControlContext(Ctx.m_pUi);
		HeaderCtx.m_UiScale = Metrics.m_UiScale;
		HeaderCtx.m_pTooltips = Ctx.m_pTooltips;
		SetCopyAction(Candidate, HeaderCtx, pAnswer, Metrics, ReadOnly, [pAnswer, pInput = Ctx.m_pInput, pState] {
			pInput->SetClipboardText(pAnswer->m_pAnswer);
			pState->m_Buttons.at(pAnswer).m_pStatus = "已复制答案";
			return true;
		});
	}
	Candidate.m_RenderMeasured = [Ctx, Result, ReadOnly, Metrics, SelectQuestion, pState, DrawText, MeasureButton](CUIRect &Content) {
		// 条目地址属于静态问答表；按钮按条目区分，候选重排不会复用其他问题的按下状态。
		CSettingsContentLayoutFlow Flow(Content);
		const bool Interactive = Flow.Input() && !ReadOnly && !Ctx.m_pUi->RenderOnly() && !Ctx.m_pUi->MeasurementOnly() && !Ctx.m_pUi->BackgroundRender() && !Ctx.m_pUi->IsPopupOpen();
		const auto Text = [&](const char *pId, const char *pText) {
			Flow.Text(pId, pText, 0, Metrics.m_BodySize, ui_token::settings::RowGap(Metrics.m_LineSpacing), {}, DrawText, Metrics.m_LineHeight);
		};
		const auto Button = [&](const char *pId, const char *pLabel, CButtonContainer &Id) {
			CUIRect Row;
			bool Clicked = false;
			if(Flow.MeasuredControl(pId, pLabel, Metrics.m_ButtonHeight, Metrics.m_BodySize, 0, 0, 4.0f, Row, MeasureButton))
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
			Flow.Space(ui_token::settings::RowGap(Metrics.m_LineSpacing));
			return Clicked;
		};

		if(Result.m_pAnswer == nullptr)
		{
			Text("answer-choose", "你可能想问以下问题，点击查看答案：");
			for(const auto *pCandidate : Result.m_vCandidates)
			{
				if(Button(pCandidate->m_pId, pCandidate->m_pQuestion, pState->m_Buttons.find(pCandidate)->second.m_Select))
					SelectQuestion(pCandidate->m_pQuestion);
			}
			return;
		}

		const auto *pAnswer = Result.m_pAnswer;
		auto &Buttons = pState->m_Buttons.find(pAnswer)->second;
		Text("answer-question", pAnswer->m_pQuestion);
		Text("answer-body", pAnswer->m_pAnswer);
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
			try
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
			catch(const std::bad_alloc &)
			{
				// 只处理已知资源失败，不重放已经请求过的系统操作。
				Buttons.m_pStatus = "无法打开目录";
			}
			catch(const std::length_error &)
			{
				Buttons.m_pStatus = "无法打开目录";
			}
		}
		if(Buttons.m_pStatus[0] != '\0')
			Text("answer-status", Buttons.m_pStatus);
	};
	qm_settings_content::UseDeclaredContent(Candidate, Ctx.m_pUi, Ctx.m_pTextRender);
	static_assert(std::is_nothrow_move_assignable_v<SSettingsCardDefinition>);
	Out = std::move(Candidate);
}

void qm_card_catalog::BuildDdnetWikiSearchCard(const IUiContext &Ctx, const qm_wiki_docs::SDdnetWikiMatch &Match, bool PreferChinese, bool ReadOnly, const SSettingsContentMetrics &Metrics, SSettingsCardDefinition &Out)
{
	using namespace qm_wiki_docs;
	const auto &Article = DdnetWikiArticle(Match.m_Article);
	int Language = PreferChinese ? SIMPLIFIED_CHINESE : ENGLISH;
	const int OtherLanguage = PreferChinese ? ENGLISH : SIMPLIFIED_CHINESE;
	if(!Match.m_aAvailable[Language] || (Match.m_aExcerpts[Language].empty() && !Match.m_aExcerpts[OtherLanguage].empty()))
		Language = OtherLanguage;
	std::string Excerpt = Match.m_aExcerpts[Language].empty() ? Article.m_apIntroductions[Language] : Match.m_aExcerpts[Language].c_str();
	Out.m_Spec = {Article.m_pCardId, Article.m_apTitles[Language], "DDNet Wiki"};
	SetCopyAction(Out, Ctx, &Article, Metrics, ReadOnly, [pUi = Ctx.m_pUi, Excerpt] {
		pUi->Input()->SetClipboardText(Excerpt.c_str());
		return false;
	});
	Out.m_RenderMeasured = [Ctx, ArticleIndex = Match.m_Article, Available = Match.m_aAvailable, Blocks = Match.m_aBlocks, Excerpt = std::move(Excerpt), ReadOnly, Metrics](CUIRect &Content) {
		static std::array<std::array<CButtonContainer, NUM_LANGUAGES>, NUM_DDNET_WIKI_ARTICLES> s_aOpenButtons{};
		CSettingsContentLayoutFlow Flow(Content);
		const bool Interactive = Flow.Input() && !ReadOnly && !Ctx.m_pUi->RenderOnly() && !Ctx.m_pUi->MeasurementOnly() && !Ctx.m_pUi->BackgroundRender() && !Ctx.m_pUi->IsPopupOpen();
		const float Gap = ui_token::settings::RowGap(Metrics.m_LineSpacing);
		const auto &Wiki = DdnetWikiArticle(ArticleIndex);
		const auto DrawText = [Ctx, Metrics](const CUIRect &Rect, const char *pText) {
			SLabelProperties Props;
			Props.m_MaxWidth = Rect.w;
			Props.m_EnableWidthCheck = false;
			Ctx.m_pUi->DoLabel(&Rect, pText, Metrics.m_BodySize, TEXTALIGN_ML, Props);
		};
		char aExcerptId[64];
		char aOpenId[64];
		str_format(aExcerptId, sizeof(aExcerptId), "%s-excerpt", Wiki.m_pSlug);
		str_format(aOpenId, sizeof(aOpenId), "%s-open", Wiki.m_pSlug);
		Flow.Text(aExcerptId, Excerpt.c_str(), 0, Metrics.m_BodySize, Gap, {}, DrawText, Metrics.m_LineHeight);
		CUIRect Row;
		if(Flow.Control(aOpenId, Metrics.m_ButtonHeight, Row))
		{
			const int Buttons = Available[ENGLISH] + Available[SIMPLIFIED_CHINESE];
			const auto Layout = ResolveSettingsActionGroupLayout(Row, Buttons, Metrics, 0.0f);
			const char *apLabels[] = {"English", "简体中文"};
			int ButtonIndex = 0;
			for(int ArticleLanguage = 0; ArticleLanguage < NUM_LANGUAGES; ++ArticleLanguage)
			{
				if(!Available[ArticleLanguage])
					continue;
				const CUIRect &Button = Layout.m_aButtons[ButtonIndex++];
				auto &Id = s_aOpenButtons[ArticleIndex][ArticleLanguage];
				if(Interactive && Ctx.m_pStorage != nullptr && Ctx.m_pUi->DoButtonLogic(&Id, 0, &Button, BUTTONFLAG_LEFT))
				{
					std::string Markdown = ReadDdnetWikiArticle(*Ctx.m_pStorage, ArticleIndex, static_cast<EDdnetWikiLanguage>(ArticleLanguage));
					OpenWikiMarkdownPopup(Ctx, Wiki.m_apTitles[ArticleLanguage], std::move(Markdown), Blocks[ArticleLanguage]);
				}
				if(Flow.Drawing())
				{
					ui_widget::DrawButtonSurface(ui_widget::ControlContext(Ctx.m_pUi), &Id, Button);
					Ctx.m_pUi->DoLabel(&Button, apLabels[ArticleLanguage], Metrics.m_BodySize, TEXTALIGN_MC);
				}
			}
		}
		Flow.Space(Gap);
	};
	qm_settings_content::UseDeclaredContent(Out, Ctx.m_pUi, Ctx.m_pTextRender);
}
