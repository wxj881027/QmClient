#ifndef GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGINTERNAL_H
#define GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGINTERNAL_H

#include "QmCardCatalog.h"

#include <engine/console.h>
#include <engine/graphics.h>

#include <game/client/QmUi/QmModuleLayoutAdapter.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/components/qmclient/bind_editor.h>
#include <game/client/components/qmclient/chat_command_hud.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace qm_card_catalog
{
	// 仅接入已经显式声明全部内容行的模块；不拦截任意旧绘制函数来推测布局。
	void UseDeclaredModuleContent(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out);
	bool VoiceHasPendingInput();

	enum class EBindCommandCommitResult
	{
		COMMITTED,
		INVALID_COMMAND,
		INVALID_CONFIG_LINE,
		FAILED,
	};

	template<typename FReplace>
	EBindCommandCommitResult CommitBindCommandDraft(IConsole &Console, CLineInput &Draft, const char *pKeyName, const char *pCurrent, FReplace &&Replace)
	{
		std::string Updated;
		try
		{
			if(!Console.LineIsValid(Draft.GetString()))
				return EBindCommandCommitResult::INVALID_COMMAND;
			if(!qm_bind_editor::AppendCommand(pKeyName, pCurrent, Draft.GetString(), Updated))
				return EBindCommandCommitResult::INVALID_CONFIG_LINE;
		}
		catch(const std::bad_alloc &)
		{
			return EBindCommandCommitResult::FAILED;
		}
		catch(const std::length_error &)
		{
			return EBindCommandCommitResult::FAILED;
		}

		if(!Replace(Updated.c_str()))
			return EBindCommandCommitResult::FAILED;
		Draft.Clear();
		return EBindCommandCommitResult::COMMITTED;
	}

	template<typename FPrepare>
	bool TryPrepareCardData(FPrepare &&Prepare)
	{
		try
		{
			Prepare();
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
	}

	struct SBindCommandArgumentInput
	{
		qm_bind_editor::SParameter m_Parameter;
		CLineInputBuffered<qm_bind_editor::INPUT_CAPACITY> m_Input;
		CButtonContainer m_IncludeButton;
		bool m_Included = true;
	};

	using TBindCommandArgumentInputs = std::vector<std::unique_ptr<SBindCommandArgumentInput>>;

	inline bool ComposeBindCommandInput(const qm_bind_editor::SRegisteredCommand &Command, const TBindCommandArgumentInputs &vInputs, std::string &Out)
	{
		std::vector<qm_bind_editor::SParameter> vParameters;
		std::vector<std::optional<std::string>> vArguments;
		for(const auto &pInput : vInputs)
		{
			vParameters.push_back(pInput->m_Parameter);
			vArguments.emplace_back(pInput->m_Included ? std::make_optional<std::string>(pInput->m_Input.GetString()) : std::nullopt);
		}
		return qm_bind_editor::ComposeCommand(Command.m_Name, vParameters, vArguments, Out);
	}

	struct SBindCommandPickerSelection
	{
		std::string m_Label;
		std::string m_Text;
		std::optional<qm_bind_editor::SRegisteredCommand> m_Command;
		TBindCommandArgumentInputs m_vArguments;
		bool m_ArgumentsValid = true;
	};

	// 先准备可接管的输入框和命令文本，再替换草稿，避免在参数构造失败时丢失原输入。
	template<typename FPublish>
	bool SelectBindCommandOption(const qm_bind_editor::SCommandOption &Option, FPublish &&Publish)
	{
		SBindCommandPickerSelection Candidate;
		if(!TryPrepareCardData([&] {
			   Candidate.m_Label = Option.m_Label;
			   Candidate.m_Text = Option.m_Command.m_Name;
			   if(Option.m_Preset)
				   return;

			   Candidate.m_Command = Option.m_Command;
			   for(auto &Parameter : qm_bind_editor::ParseParameters(Option.m_Command.m_Parameters))
			   {
				   auto pInput = std::make_unique<SBindCommandArgumentInput>();
				   pInput->m_Included = !Parameter.m_Optional;
				   pInput->m_Parameter = std::move(Parameter);
				   Candidate.m_vArguments.push_back(std::move(pInput));
			   }
			   Candidate.m_ArgumentsValid = ComposeBindCommandInput(Option.m_Command, Candidate.m_vArguments, Candidate.m_Text);
		   }))
			return false;

		Publish(std::move(Candidate));
		return true;
	}

	inline CQmChatCommandHud PrepareChatCompletionPreview(const char *pWeaponDescription, const char *pInfoDescription)
	{
		CQmChatCommandHud Preview;
		// 预览数据准备失败时，本帧仍然声明和绘制配色控件。
		if(!TryPrepareCardData([&] {
			   Preview.BeginUpdate("/", 1, true, 0);
			   Preview.AddServerCommand("addweapon", "i[weapon-id]", pWeaponDescription);
			   Preview.AddServerCommand("info", "", pInfoDescription);
			   Preview.EndUpdate();
			   Preview.MoveSelection(1, "/", 1);
			   }))
			return {};
		return Preview;
	}

	// 一个可用的 GL 版本也会有自动选项，因此已满足原先的两个候选门槛。
	template<typename FHasDriver>
	bool HasGraphicsModeChoice(bool DriverBlocked, FHasDriver &&HasDriver)
	{
		int Supported = 0;
		for(int BackendIndex = 0; BackendIndex < BACKEND_TYPE_COUNT; ++BackendIndex)
		{
			const auto Backend = static_cast<EBackendType>(BackendIndex);
			if(Backend == BACKEND_TYPE_AUTO)
				continue;
			const bool BlockedOpenGl = DriverBlocked && Backend == BACKEND_TYPE_OPENGL;
			for(int AgeIndex = 0; AgeIndex < GRAPHICS_DRIVER_AGE_TYPE_COUNT; ++AgeIndex)
			{
				const auto Age = static_cast<EGraphicsDriverAgeType>(AgeIndex);
				if(BlockedOpenGl && Age != GRAPHICS_DRIVER_AGE_TYPE_LEGACY)
					continue;
				if(!HasDriver(Backend, Age))
					continue;
				++Supported;
				if(Supported > 1 || Backend == BACKEND_TYPE_OPENGL_ES || (Backend == BACKEND_TYPE_OPENGL && !BlockedOpenGl))
					return true;
			}
		}
		return false;
	}

	// 卡片模块文件共享的 definition 装配：标题/描述取自 QmCardRegistry（同一份事实源），
	// 折叠状态与折叠按钮由页面注入，内容布局由各卡的声明或纯几何 Block 提供。
	// Render 为 FSettingsCardRenderMeasured（可消费 CUIRect &Content），与各页面既有内容函数签名一致。
	void MakeModuleCard(
		const SQmCardBuildContext &Ctx,
		qm_module::EQmModuleId Id,
		const char *pStableId,
		const char *pTitle,
		const char *pSubtitle,
		const FSettingsCardRenderMeasured &Render,
		SSettingsCardDefinition &Out);

	// 皮肤外观与切换动画的独立全局卡片构造入口。
	bool BuildSkinCard(const SQmCardBuildContext &Ctx, qm_module::EQmModuleId Id, SSettingsCardDefinition &Out);

	// 卡片模块共享的行高/行距推导（LineHeight + LineSpacing 的整数倍）。
	inline float CardRows(const SSettingsContentMetrics &Metrics, const float Count)
	{
		return Count * (Metrics.m_LineHeight + Metrics.m_LineSpacing);
	}

	inline float CardRow(const SSettingsContentMetrics &Metrics, const float Spacing = 1.0f)
	{
		return Metrics.m_LineHeight + Metrics.m_LineSpacing * Spacing;
	}

	// 灵动岛开关倒计时位置复选框的控件 id：渲染函数（menus_qmclient.cpp）与卡片预布局输入
	// 必须共用同一组 id，否则同一次点击会被两条路径各处理一次。权威定义在卡片模块文件里。
	const void *SwitchCountdownFollowTeeId();
	const void *SwitchCountdownMediaIslandId();
} // namespace qm_card_catalog

#endif // GAME_CLIENT_QMUI_CARDS_QMCARDCATALOGINTERNAL_H
