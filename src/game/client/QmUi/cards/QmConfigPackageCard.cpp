#include "QmCardCatalog.h"
#include "QmCardCatalogInternal.h"

#include <base/system.h>

#include <engine/client.h>
#include <engine/config.h>
#include <engine/shared/config.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/QmUi/SettingsContentLayout.h>
#include <game/client/QmUi/UiButtons.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/config_package.h>
#include <game/client/components/tooltips.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>
#include <game/version.h>

#include <array>
#include <string>
#include <vector>

namespace
{
	const char *PackageStatus(const std::string &Text)
	{
		static constexpr const char *s_apMessages[] = {
			Localizable("Configuration package exported."),
			Localizable("Could not list configuration files."),
			Localizable("Too many configuration files."),
			Localizable("Invalid configuration package metadata."),
			Localizable("Invalid or duplicate configuration path."),
			Localizable("Configuration package is too large."),
			Localizable("Configuration package is missing required files."),
			Localizable("Invalid configuration package."),
			Localizable("Could not read configuration files."),
			Localizable("Invalid configuration package path."),
			Localizable("Configuration package already exists."),
			Localizable("Could not save configuration package."),
			Localizable("Could not read configuration package."),
			Localizable("Could not back up configuration files."),
			Localizable("Configuration restore temporary files already exist."),
			Localizable("Could not stage configuration restore."),
			Localizable("Configuration restore failed; previous files were restored."),
			Localizable("Configuration restore failed; recover from the saved backup."),
			Localizable("Error saving settings")};
		for(const char *pMessage : s_apMessages)
			if(Text == pMessage)
				return Localize(pMessage);
		return Text.c_str();
	}

	struct SConfigFileItem
	{
		std::string m_Path;
		bool m_Selected = false;
		char m_Id = 0;
	};

	struct SConfigPackageUiState
	{
		bool m_Open = false;
		bool m_Import = false;
		bool m_Initialized = false;
		bool m_PreviewValid = false;
		std::array<CButtonContainer, 11> m_aButtons;
		CListBox m_CustomList;
		CListBox m_PackageList;
		CListBox m_PreviewList;
		std::vector<SConfigFileItem> m_vCustomFiles;
		std::vector<SConfigFileItem> m_vPackages;
		std::vector<char> m_vPreviewIds;
		qm_config_package::SPackage m_Preview;
		std::string m_PreviewPath;
		std::string m_Status;
		std::string m_ResultPath;
		int m_SelectedPackage = -1;
		int m_CustomSelection = -1;
	};

	SConfigPackageUiState &PackageUi()
	{
		static SConfigPackageUiState s_State;
		return s_State;
	}

	void RefreshFiles(IStorage &Storage)
	{
		auto &State = PackageUi();
		std::vector<std::string> vCustom, vPackages;
		std::string Error;
		if(!qm_config_package::ListCustomConfigs(Storage, vCustom, Error) || !qm_config_package::ListPackages(Storage, vPackages, Error))
		{
			State.m_Status = Error;
			return;
		}
		std::vector<SConfigFileItem> vNewCustom;
		for(const auto &Path : vCustom)
		{
			bool Selected = false;
			for(const auto &Previous : State.m_vCustomFiles)
				if(Previous.m_Path == Path)
					Selected = Previous.m_Selected;
			vNewCustom.push_back({Path, Selected});
		}
		State.m_vCustomFiles = std::move(vNewCustom);
		State.m_vPackages.clear();
		for(const auto &Path : vPackages)
			State.m_vPackages.push_back({Path});
		State.m_SelectedPackage = -1;
		State.m_CustomSelection = -1;
		State.m_PreviewValid = false;
		State.m_PreviewPath.clear();
		State.m_CustomList.Reset();
		State.m_PackageList.Reset();
		State.m_PreviewList.Reset();
		State.m_Initialized = true;
	}
}

bool qm_card_catalog::QmCardRenderHook::BuildConfigFilesCard(const SQmCardBuildContext &Ctx, SSettingsCardDefinition &Out)
{
	CMenus *pMenus = Ctx.m_pMenus;
	CTooltips *pTooltips = Ctx.m_UiContext.m_pTooltips;
	const auto Metrics = Ctx.m_Metrics;
	const bool ReadOnly = Ctx.m_ReadOnly;
	// 沿用同一 stable ID，普通页面和搜索结果共享文件选择及导入预览。
	Out.m_Render = [pMenus, pTooltips, Metrics, ReadOnly](CUIRect Content) {
		auto &State = PackageUi();
		const bool Interactive = !ReadOnly && !pMenus->Ui()->RenderOnly();
		const float RowHeight = Metrics.m_ButtonHeight;
		CSettingsContentLayoutFlow Flow(Content);
		const auto RowAction = [&](const char *pId, float Rows, auto &&Action) {
			CUIRect Rect;
			if(Flow.Control(pId, Rows * RowHeight + (Rows - 1.0f) * Metrics.m_LineSpacing, Rect))
				Action(Rect);
			Flow.Space(Metrics.m_LineSpacing);
		};
		const auto Text = [&](const char *pId, const char *pValue, float Rows = 1) {
			Flow.Text(pId, pValue, 0.0f, Metrics.m_BodySize, Metrics.m_LineSpacing, [pMenus](const char *pText, float Width, float FontSize) { return pMenus->TextRender()->TextBoundingBox(FontSize, pText, -1, Width).m_H + FontSize * 0.25f; }, [pMenus, Metrics](const CUIRect &Rect, const char *pText) { pMenus->Ui()->DoLabel(&Rect, pText, Metrics.m_BodySize, TEXTALIGN_ML, {.m_MaxWidth = Rect.w, .m_EnableWidthCheck = false}); }, Rows * RowHeight + (Rows - 1) * Metrics.m_LineSpacing);
		};
		const auto Button = [pMenus, Metrics, Interactive, &State](int Index, const char *pId, const char *pLabel, const CUIRect &Rect, bool Checked = false) {
			const bool Clicked = pMenus->DoSettingsButton_Menu(CMenus::SETTINGS_GENERAL, -1, -1, &State.m_aButtons[Index], pId, pLabel, Checked, &Rect, Metrics);
			return Interactive && Clicked;
		};
		const auto PathLabel = [pMenus, pTooltips, Metrics](const void *pId, CUIRect Rect, const char *pText, const char *pFullPath) {
			SLabelProperties Props;
			Props.m_MaxWidth = Rect.w;
			Props.m_DisallowNewline = true;
			Props.m_EllipsisAtEnd = true;
			Props.m_EnableWidthCheck = false;
			pMenus->Ui()->DoLabel(&Rect, pText, Metrics.m_BodySize, TEXTALIGN_ML, Props);
			if(pTooltips != nullptr)
				pTooltips->DoToolTipForRect(pId, &Rect, pFullPath);
		};
		CUIRect Left, Right;
		const auto FileButtons = [&](const CUIRect &Rect, int LeftId, int RightId, ConfigDomain LeftDomain, ConfigDomain RightDomain, const char *pLeft, const char *pRight) {
			static constexpr const char *s_apFileIds[] = {"tclient-files-qmclient-settings", "tclient-files-profiles", "tclient-files-warlist", "tclient-files-chatbinds"};
			const char *apLabels[] = {pLeft, pRight};
			static ui_widget::SSettingsActionGroupState s_aFileWidths[2];
			const auto Actions = ui_widget::PrepareSettingsActionGroup(*pMenus->Ui(), s_aFileWidths[LeftId / 2], Rect, apLabels, 2, Metrics, ESettingsActionGroupWidth::FILL);
			Left = Actions.m_aButtons[0];
			Right = Actions.m_aButtons[1];
			const auto Open = [pMenus](ConfigDomain Domain) {
				char aPath[IO_MAX_PATH_LENGTH];
				pMenus->Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[Domain].m_aConfigPath, aPath, sizeof(aPath));
				pMenus->Client()->ViewFile(aPath);
			};
			if(Button(LeftId, s_apFileIds[LeftId], pLeft, Left))
				Open(LeftDomain);
			if(Button(RightId, s_apFileIds[RightId], pRight, Right))
				Open(RightDomain);
		};
		RowAction("config-files-primary", 1, [&](CUIRect Rect) { FileButtons(Rect, 0, 1, ConfigDomain::QMCLIENT, ConfigDomain::TCLIENTPROFILES, Localize("QmClient Settings"), Localize("Profiles")); });
		RowAction("config-files-secondary", 1, [&](CUIRect Rect) { FileButtons(Rect, 2, 3, ConfigDomain::TCLIENTWARLIST, ConfigDomain::TCLIENTCHATBINDS, Localize("War List"), Localize("Chat Binds")); });
		RowAction("config-package-toggle", 1, [&](CUIRect Rect) {
			if(Button(4, "config-package-manager", Localize("Configuration packages"), Rect, State.m_Open))
			{
				State.m_Open = !State.m_Open;
				// 列表入场不依赖渲染间隙启发：反复展开的间隔常常短于 0.4 秒，
				// 间隙判据永远不成立，逐行浮现会静默失效，这里显式重放。
				if(State.m_Open)
				{
					State.m_CustomList.RestartEntryAnimation();
					State.m_PackageList.RestartEntryAnimation();
				}
				if(State.m_Open && !State.m_Initialized)
					RefreshFiles(*pMenus->Storage());
				// 输入提交后公共 Deck 会重新声明展开内容。
			}
		});
		if(!State.m_Open)
			return;
		RowAction("config-package-mode", 1, [&](CUIRect Rect) {
			const char *apModes[] = {Localize("Export configuration package"), Localize("Import configuration package")};
			static CButtonContainer s_aModeButtons[2];
			const bool Import = pMenus->DoSegmentedChoice(s_aModeButtons, apModes, 2, State.m_Import ? 1 : 0, Rect) == 1;
			// 两张列表各自持有入场生命周期；切模式只重放即将显示的那一张。
			if(Import != State.m_Import)
			{
				State.m_Import = Import;
				(Import ? State.m_PackageList : State.m_CustomList).RestartEntryAnimation();
			}
		});
		qm_config_package::CStorageFiles Files(*pMenus->Storage());
		if(!State.m_Import)
		{
			Text("config-export-help", Localize("Settings, binds, profiles, chat binds and the war list are included. Select additional cfg files below."), 2);
			RowAction("config-export-files", 8, [&](CUIRect ListRect) {
				State.m_CustomList.SetActive(Interactive);
				State.m_CustomList.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
				State.m_CustomList.DoStart(RowHeight, State.m_vCustomFiles.size(), 1, 4, State.m_CustomSelection, &ListRect);
				for(auto &Item : State.m_vCustomFiles)
				{
					const auto ListItem = State.m_CustomList.DoNextItem(&Item.m_Id);
					if(ListItem.m_Visible)
						PathLabel(&Item.m_Id, ListItem.m_Rect, ((Item.m_Selected ? "[x] " : "[ ] ") + Item.m_Path).c_str(), Item.m_Path.c_str());
				}
				const int Selected = State.m_CustomList.DoEnd();
				if(Interactive && Selected >= 0 && Selected < static_cast<int>(State.m_vCustomFiles.size()))
				{
					State.m_CustomSelection = Selected;
					if(State.m_CustomList.WasItemSelected() || State.m_CustomList.WasItemActivated())
						State.m_vCustomFiles[Selected].m_Selected = !State.m_vCustomFiles[Selected].m_Selected;
				}
			});
			RowAction("config-export-action", 1, [&](CUIRect Rect) {
				if(Button(7, "config-package-export", Localize("Export configuration package"), Rect))
				{
					std::vector<std::string> vSelected;
					for(const auto &Item : State.m_vCustomFiles)
						if(Item.m_Selected)
							vSelected.push_back(Item.m_Path);
					qm_config_package::SPackage Package;
					std::string Error;
					const std::string Path = qm_config_package::NewPackagePath(Files);
					if(!pMenus->ConfigManager()->Save(true))
						State.m_Status = "Error saving settings";
					else if(qm_config_package::Collect(Files, vSelected, QMCLIENT_VERSION, qm_config_package::ExportTimestamp().c_str(), Package, Error) && qm_config_package::Save(Files, Path, Package, Error))
					{
						State.m_Status = "Configuration package exported.";
						State.m_ResultPath = Path;
						RefreshFiles(*pMenus->Storage());
					}
					else
						State.m_Status = Error;
				}
			});
		}
		else
		{
			// 空包列表不占位：没有包时只留一行说明，卡片高度交给公共 Deck 的高度
			// 动画过渡，避免导入页在空状态下留下一大块空白区域。
			const bool HasPackages = !State.m_vPackages.empty();
			Text("config-import-help", Localize("Copy a .qmconfig file into the package folder, then refresh."));
			if(HasPackages)
			{
				RowAction("config-package-list", 5, [&](CUIRect ListRect) {
					State.m_PackageList.SetActive(Interactive);
					State.m_PackageList.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
					State.m_PackageList.DoStart(RowHeight, State.m_vPackages.size(), 1, 4, State.m_SelectedPackage, &ListRect);
					for(auto &Item : State.m_vPackages)
					{
						const auto ListItem = State.m_PackageList.DoNextItem(&Item.m_Id, false);
						if(ListItem.m_Visible)
							PathLabel(&Item.m_Id, ListItem.m_Rect, Item.m_Path.c_str() + str_length(qm_config_package::PACKAGE_DIRECTORY) + 1, Item.m_Path.c_str());
					}
					const int Selected = State.m_PackageList.DoEnd();
					if(Interactive && Selected != State.m_SelectedPackage && Selected >= 0 && Selected < static_cast<int>(State.m_vPackages.size()))
					{
						State.m_SelectedPackage = Selected;
						State.m_PreviewPath = State.m_vPackages[Selected].m_Path;
						State.m_PreviewValid = qm_config_package::Load(Files, State.m_PreviewPath, State.m_Preview, State.m_Status);
						State.m_vPreviewIds.resize(State.m_PreviewValid ? State.m_Preview.m_vFiles.size() : 0);
						State.m_PreviewList.Reset();
					}
				});
			}
			if(State.m_PreviewValid)
			{
				std::string Metadata = State.m_Preview.m_ClientVersion + " | " + State.m_Preview.m_ExportedAt;
				Text("config-package-metadata", Metadata.c_str(), 2);
				RowAction("config-package-preview", 7, [&](CUIRect ListRect) {
					State.m_PreviewList.SetActive(false);
					State.m_PreviewList.SetScrollProfile(EQmScrollProfile::SETTINGS_INNER);
					State.m_PreviewList.DoStart(RowHeight, State.m_Preview.m_vFiles.size(), 1, 4, -1, &ListRect);
					for(size_t Index = 0; Index < State.m_Preview.m_vFiles.size(); ++Index)
					{
						const auto &File = State.m_Preview.m_vFiles[Index];
						const auto Item = State.m_PreviewList.DoNextItem(&State.m_vPreviewIds[Index]);
						if(Item.m_Visible)
						{
							const std::string Text = File.m_Path + (File.m_Present ? " (" + std::to_string(File.m_Content.size()) + " B)" : std::string(" (") + Localize("Remove") + ")");
							PathLabel(&State.m_vPreviewIds[Index], Item.m_Rect, Text.c_str(), File.m_Path.c_str());
						}
					}
					State.m_PreviewList.DoEnd();
				});
				Text("config-restore-help", Localize("Complete restore replaces the listed files. A backup is saved before importing. The client will restart."), 2);
				RowAction("config-restore-action", 1, [&](CUIRect Rect) {
					static CUi::SConfirmPopupContext s_RestoreConfirm;
					static bool s_RestorePending = false;
					if(Button(7, "config-package-restore", Localize("Import and restart"), Rect))
					{
						// 恢复会覆盖设置与 cfg 并可重启：Cancel/OK + 危险色，正文说明备份。
						s_RestoreConfirm.Reset();
						s_RestoreConfirm.DefaultButtons();
						s_RestoreConfirm.m_Tone = CUi::SConfirmPopupContext::ETone::DANGER;
						str_copy(s_RestoreConfirm.m_aMessage, Localize("Replace settings, binds and the listed cfg files, then restart? The current configuration will be backed up first."), sizeof(s_RestoreConfirm.m_aMessage));
						pMenus->Ui()->ShowPopupConfirm(pMenus->Ui()->MouseX(), pMenus->Ui()->MouseY(), &s_RestoreConfirm);
						s_RestorePending = true;
					}
					// 结果下一帧轮询：确认后才执行恢复，取消只清等待状态。
					if(s_RestorePending)
					{
						if(s_RestoreConfirm.m_Result == CUi::SConfirmPopupContext::CONFIRMED)
						{
							s_RestorePending = false;
							pMenus->PopupConfirmRestoreConfigPackage();
						}
						else if(s_RestoreConfirm.m_Result == CUi::SConfirmPopupContext::CANCELED)
						{
							s_RestorePending = false;
						}
					}
				});
			}
			else if(HasPackages)
				Text("config-package-no-preview", Localize("Select a configuration package to preview its contents."), 2);
		}
		RowAction("config-package-actions", 1, [&](CUIRect Rect) {
			const char *apActions[] = {Localize("Refresh"), Localize("Open package folder")};
			static ui_widget::SSettingsActionGroupState s_ActionWidths;
			const auto Actions = ui_widget::PrepareSettingsActionGroup(*pMenus->Ui(), s_ActionWidths, Rect, apActions, 2, Metrics);
			Left = Actions.m_aButtons[0];
			Right = Actions.m_aButtons[1];
			if(Button(8, "config-package-refresh", Localize("Refresh"), Left))
				RefreshFiles(*pMenus->Storage());
			if(Button(9, "config-package-folder", Localize("Open package folder"), Right))
			{
				pMenus->Storage()->CreateFolder("qmclient", IStorage::TYPE_SAVE);
				pMenus->Storage()->CreateFolder(qm_config_package::PACKAGE_DIRECTORY, IStorage::TYPE_SAVE);
				char aPath[IO_MAX_PATH_LENGTH];
				pMenus->Storage()->GetCompletePath(IStorage::TYPE_SAVE, qm_config_package::PACKAGE_DIRECTORY, aPath, sizeof(aPath));
				pMenus->Client()->ViewFile(aPath);
			}
		});
		if(!State.m_Status.empty())
			Text("config-package-status", PackageStatus(State.m_Status), 2);
		if(!State.m_ResultPath.empty())
			RowAction("config-package-result", 1, [&](CUIRect Rect) { PathLabel(&State.m_ResultPath, Rect, State.m_ResultPath.c_str(), State.m_ResultPath.c_str()); });
	};
	const auto Render = Out.m_Render;
	Out.m_RenderMeasured = [Render](CUIRect &Content) { Render(Content); };
	qm_card_catalog::UseDeclaredModuleContent(Ctx, Out);
	return true;
}

void CMenus::PopupConfirmRestoreConfigPackage()
{
	auto &State = PackageUi();
	if(!State.m_PreviewValid)
		return;
	// 使用已预览的内存包，避免确认前文件变动使实际导入内容与预览不同。
	qm_config_package::CStorageFiles Files(*Storage());
	const std::string BackupPath = qm_config_package::NewPackagePath(Files, true);
	std::string Error;
	if(!ConfigManager()->Save(true))
		Error = "Error saving settings";
	else if(qm_config_package::Restore(Files, State.m_Preview, BackupPath, Error, QMCLIENT_VERSION))
	{
		// 仅成功恢复并明确选择重启时，阻止退出阶段把旧内存配置写回刚恢复的文件。
		g_Config.m_ClSaveSettings = 0;
		Client()->Restart();
		return;
	}
	State.m_Status = Error;
	State.m_ResultPath = Files.Exists(BackupPath) ? BackupPath : "";
	// 结果要读原因和备份路径，属于必须知会：居中单按钮 Alert，不用结果 Toast。
	static CUi::SMessagePopupContext s_RestoreResult;
	str_copy(s_RestoreResult.m_aMessage, PackageStatus(Error), sizeof(s_RestoreResult.m_aMessage));
	if(Error.empty())
		s_RestoreResult.DefaultColor(Ui()->TextRender());
	else
		s_RestoreResult.ErrorColor();
	Ui()->ShowPopupMessage(0.0f, 0.0f, &s_RestoreResult);
}
