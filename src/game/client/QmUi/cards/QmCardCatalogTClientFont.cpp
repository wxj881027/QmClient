#include <base/log.h>
#include <base/math.h>
#include <base/perf_timer.h>
#include <base/str.h>
#include <base/system.h>
#include <base/types.h>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/config_tags.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/shared/localization.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/warning.h>

#include <game/client/QmUi/QmCardOrderModel.h>
#include <game/client/QmUi/QmCardRegistry.h>
#include <game/client/QmUi/QmDropdown.h>
#include <game/client/QmUi/SecondaryPanel.h>
#include <game/client/QmUi/SettingsCard.h>
#include <game/client/QmUi/SettingsFontSelection.h>
#include <game/client/QmUi/SettingsPageLayout.h>
#include <game/client/QmUi/UiForms.h>
#include <game/client/QmUi/UiNavigation.h>
#include <game/client/QmUi/UiSurface.h>
#include <game/client/QmUi/cards/QmCardCatalog.h>
#include <game/client/QmUi/cards/QmCardCatalogTClientInternal.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/qmclient/font_download_storage.h>
#include <game/client/components/qmclient/perf_logging.h>
#include <game/client/components/section_loader.h>
#include <game/client/components/skins.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/qm_icon.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <SDL_audio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace FontIcons;
using namespace qm_tclient_cards;

// ============ QmClient 字体商店 ============
// 可搜索的在线字体目录：data/fonts/fonts_store/font_catalog.json 覆盖 Google Fonts 全量
// 字体族（族名/分类/google/fonts 仓库路径），随包发行且用户目录同名文件可整体
// 覆盖。字体文件本体不随包——搜索命中后按需下载安装到 fonts/fonts_store（逐文件
// 串行，镜像链 ghproxy.net → ghfast.top → raw.githubusercontent.com）；预览按需
// 下载到 qmclient/fontcache 并临时加载为预览面（不进字体族列表）。安装与预览
// 共用“同一时刻最多一个下载”的串行通道，安装任务优先。
namespace fontstore
{
	struct SEntry
	{
		std::string m_Family; // 字体族显示名
		std::string m_Category; // serif / sans-serif / ...
		std::string m_RepoPath; // google/fonts 仓库相对路径
		std::string m_File; // 落盘文件名（仓库路径基名，目录内由生成器保证唯一）
		std::string m_DirectUrl; // 国内直链（fonts.gstatic.cn，生成器只给静态单文件拉丁族），可空
		// 覆盖的脚本子集（生成器白名单：japanese/korean/cyrillic/...），供商店
		// 按界面语言排序与徽标标注；仅 latin 覆盖的族为空。
		std::vector<std::string> m_vSubsets;
		bool m_Variable = false;
	};

	// 字体族聚合：搜索、安装与预览都以族为单位。
	struct SFamily
	{
		std::string m_Name;
		std::string m_Category;
		// 族内条目的脚本子集（同族条目一致，取首条）。
		std::vector<std::string> m_vSubsets;
		std::vector<size_t> m_vEntryIndices; // 首条为主文件（预览/默认下载）
		bool m_Installed = false; // 目录加载与重扫后按文件存在性刷新
		bool m_PreviewReady = false; // 预览面已可渲染
		bool m_PreviewFailed = false; // 预览下载或解析失败：不再自动重试
	};

	// 安装任务：一个族一条，文件逐个串行下载，单文件失败自动换镜像。
	struct SInstallJob
	{
		size_t m_FamilyIndex = 0;
		size_t m_FileCursor = 0; // 族内文件游标（也等于已完成文件数）
		size_t m_UrlIndex = 0; // 当前文件的镜像游标
		std::shared_ptr<IHttpRequest> m_pRequest = nullptr;
		bool m_Done = false;
		bool m_Failed = false;
	};

	// 预览任务：只下载族内主文件到预览缓存，尽力而为（失败不重试换镜像）。
	struct SPreviewJob
	{
		size_t m_FamilyIndex = 0;
		std::shared_ptr<IHttpRequest> m_pRequest = nullptr;
	};

	// 下载状态变化时递增：卡片测量哈希引用它，驱动卡片高度过渡动画。
	int s_StateVersion = 0;
	// 最近一次从商店发起安装的族（卡片行状态位显示进度/重试/已安装）。
	int s_LastInstallFamily = -1;
	std::vector<SInstallJob> s_vInstallJobs;
	std::vector<SPreviewJob> s_vPreviewQueue;
	std::optional<SPreviewJob> s_ActivePreview;
	bool s_ReloadPending = false;

	bool IsValidFontFileName(const std::string &File)
	{
		return qm_font_download::ValidFilename(File);
	}

	// 仓库相对路径会拼进下载 URL 与落盘路径：锁死形态，禁止任何穿越写法。
	bool IsValidRepoPath(const std::string &Path)
	{
		return Path.size() > 2 &&
		       Path[0] != '/' &&
		       Path.find("..") == std::string::npos &&
		       Path.find('\\') == std::string::npos &&
		       Path.find("//") == std::string::npos &&
		       IsValidFontFileName(Path.substr(Path.rfind('/') + 1));
	}

	// 目录与族聚合共用一次解析（懒加载）：条目按族名（大小写不敏感）归并，
	// 同名族合并文件列表，族内首条即生成器排序后的主文件（可变字体优先）。
	void LoadCatalog(IStorage *pStorage, std::vector<SEntry> &vEntries, std::vector<SFamily> &vFamilies)
	{
		void *pCatalogData = nullptr;
		unsigned CatalogSize = 0;
		// TYPE_ALL 会先搜用户目录再搜数据目录：用户侧同名目录可整体覆盖随包目录。
		if(!pStorage->ReadFile("fonts/fonts_store/font_catalog.json", IStorage::TYPE_ALL, &pCatalogData, &CatalogSize))
			return;

		json_value *pJson = json_parse((json_char *)pCatalogData, CatalogSize);
		if(pJson != nullptr && pJson->type == json_object)
		{
			const json_value &Fonts = (*pJson)["fonts"];
			if(Fonts.type == json_array)
			{
				std::map<std::string, size_t> FamilyIndex;
				for(unsigned i = 0; i < Fonts.u.array.length; ++i)
				{
					const json_value &Entry = Fonts[i];
					if(Entry.type != json_object)
						continue;
					const json_value &Name = Entry["n"];
					const json_value &Path = Entry["f"];
					if(Name.type != json_string || Path.type != json_string)
						continue;
					SEntry Parsed;
					Parsed.m_Family = Name.u.string.ptr;
					Parsed.m_RepoPath = Path.u.string.ptr;
					const json_value &Category = Entry["c"];
					if(Category.type == json_string)
						Parsed.m_Category = Category.u.string.ptr;
					const json_value &Variable = Entry["v"];
					Parsed.m_Variable = Variable.type == json_integer && Variable.u.integer != 0;
					// 国内直链锁死 gstatic.cn 域，避免目录被换入任意下载源。
					const json_value &Direct = Entry["u"];
					if(Direct.type == json_string && str_startswith(Direct.u.string.ptr, "https://fonts.gstatic.cn/") && str_endswith(Direct.u.string.ptr, ".ttf"))
						Parsed.m_DirectUrl = Direct.u.string.ptr;
					const json_value &Subsets = Entry["s"];
					if(Subsets.type == json_array)
					{
						for(unsigned si = 0; si < Subsets.u.array.length; ++si)
							if(Subsets[si].type == json_string)
								Parsed.m_vSubsets.emplace_back(Subsets[si].u.string.ptr);
					}
					if(Parsed.m_Family.empty() || !IsValidRepoPath(Parsed.m_RepoPath))
					{
						log_warn("fontstore", "Font catalog entry for '%s' has an unsafe path, skipped", Parsed.m_Family.c_str());
						continue;
					}
					Parsed.m_File = Parsed.m_RepoPath.substr(Parsed.m_RepoPath.rfind('/') + 1);

					std::string LowerFamily = Parsed.m_Family;
					std::transform(LowerFamily.begin(), LowerFamily.end(), LowerFamily.begin(), [](unsigned char c) { return (char)std::tolower(c); });
					auto It = FamilyIndex.find(LowerFamily);
					if(It == FamilyIndex.end())
					{
						SFamily Family;
						Family.m_Name = Parsed.m_Family;
						Family.m_Category = Parsed.m_Category;
						Family.m_vSubsets = Parsed.m_vSubsets;
						Family.m_vEntryIndices.push_back(vEntries.size());
						vFamilies.push_back(std::move(Family));
						It = FamilyIndex.emplace(LowerFamily, vFamilies.size() - 1).first;
					}
					else
						vFamilies[It->second].m_vEntryIndices.push_back(vEntries.size());
					vEntries.push_back(std::move(Parsed));
				}
			}
		}
		else
		{
			log_warn("fontstore", "Failed to parse font catalog data/fonts/fonts_store/font_catalog.json");
		}
		json_value_free(pJson);
		free(pCatalogData);
		log_info("fontstore", "Font catalog loaded with %" PRIzu " files in %" PRIzu " families", vEntries.size(), vFamilies.size());
	}

	struct SCatalog
	{
		std::vector<SEntry> m_vEntries;
		std::vector<SFamily> m_vFamilies;
		bool m_Loaded = false;
	};

	void RefreshInstalled(IStorage *pStorage);

	SCatalog &Catalog(IStorage *pStorage)
	{
		static SCatalog s_Catalog;
		if(!s_Catalog.m_Loaded)
		{
			s_Catalog.m_Loaded = true;
			LoadCatalog(pStorage, s_Catalog.m_vEntries, s_Catalog.m_vFamilies);
			RefreshInstalled(pStorage);
		}
		return s_Catalog;
	}

	std::vector<SEntry> &Entries(IStorage *pStorage)
	{
		return Catalog(pStorage).m_vEntries;
	}

	std::vector<SFamily> &Families(IStorage *pStorage)
	{
		return Catalog(pStorage).m_vFamilies;
	}

	// 刷新各族安装状态：全部文件都存在于用户字体目录才算已安装。
	// 仅在目录加载与重扫后调用（文件存在性检查量大，不能逐帧做）。
	void RefreshInstalled(IStorage *pStorage)
	{
		std::vector<SEntry> &vEntries = Catalog(pStorage).m_vEntries;
		for(SFamily &Family : Catalog(pStorage).m_vFamilies)
		{
			Family.m_Installed = std::all_of(Family.m_vEntryIndices.begin(), Family.m_vEntryIndices.end(), [&](size_t EntryIndex) {
				return qm_font_download::Installed(*pStorage, vEntries[EntryIndex].m_File);
			});
		}
	}

	// 界面语言 → 脚本子集键：商店排序/徽标用（与目录 "s" 字段同键，见
	// generate_font_catalog.py 的 UI_SUBSET_WHITELIST）。拉丁系语言返回
	// nullptr——字体目录默认都覆盖拉丁，无信息量，不排序不标注。
	const char *LanguageSubsetKey()
	{
		static const char *const apCyrillic[] = {"russian.txt", "ukrainian.txt", "belarusian.txt", "bulgarian.txt", "serbian_cyrillic.txt", "kyrgyz.txt", "chuvash.txt"};
		const char *pFile = g_Config.m_ClLanguagefile;
		if(str_comp_nocase(pFile, "japanese.txt") == 0)
			return "japanese";
		if(str_comp_nocase(pFile, "korean.txt") == 0)
			return "korean";
		if(str_comp_nocase(pFile, "simplified_chinese.txt") == 0)
			return "chinese-simplified";
		if(str_comp_nocase(pFile, "traditional_chinese.txt") == 0)
			return "chinese-traditional";
		for(const char *pLang : apCyrillic)
			if(str_comp_nocase(pFile, pLang) == 0)
				return "cyrillic";
		if(str_comp_nocase(pFile, "greek.txt") == 0)
			return "greek";
		if(str_comp_nocase(pFile, "arabic.txt") == 0 || str_comp_nocase(pFile, "persian.txt") == 0)
			return "arabic";
		return nullptr;
	}

	// 子集键 → 本地化徽标名；未知子集返回 nullptr（不显示）。
	const char *LocalizedSubsetName(const std::string &Subset)
	{
		if(Subset == "japanese")
			return Localize("Japanese");
		if(Subset == "korean")
			return Localize("Korean");
		if(Subset == "chinese-simplified")
			return Localize("Chinese (Simplified)");
		if(Subset == "chinese-traditional")
			return Localize("Chinese (Traditional)");
		if(Subset == "cyrillic")
			return Localize("Cyrillic");
		if(Subset == "greek")
			return Localize("Greek");
		if(Subset == "arabic")
			return Localize("Arabic");
		return nullptr;
	}

	// google/fonts 仓库文件直链：两级 GitHub 加速代理 + 官方 raw 兜底。
	// 路径中只有方括号/圆括号/逗号这类字符需要百分号编码，其余保持原样。
	std::vector<std::string> MakeDownloadUrls(const std::string &RepoPath)
	{
		std::string Encoded;
		Encoded.reserve(RepoPath.size() + 8);
		for(char c : RepoPath)
		{
			switch(c)
			{
			case '[': Encoded += "%5B"; break;
			case ']': Encoded += "%5D"; break;
			case '(': Encoded += "%28"; break;
			case ')': Encoded += "%29"; break;
			case ',': Encoded += "%2C"; break;
			default: Encoded += c; break;
			}
		}
		const std::string Raw = "https://raw.githubusercontent.com/google/fonts/main/" + Encoded;
		return {"https://ghproxy.net/" + Raw, "https://ghfast.top/" + Raw, Raw};
	}

	// 条目完整下载链：国内直链（fonts.gstatic.cn，若有）优先，其后 GitHub 加速链
	// ghproxy.net → ghfast.top → raw.githubusercontent.com 兜底。
	std::vector<std::string> EntryDownloadUrls(const SEntry &Entry)
	{
		std::vector<std::string> vUrls;
		if(!Entry.m_DirectUrl.empty())
			vUrls.push_back(Entry.m_DirectUrl);
		const std::vector<std::string> vMirrorUrls = MakeDownloadUrls(Entry.m_RepoPath);
		vUrls.insert(vUrls.end(), vMirrorUrls.begin(), vMirrorUrls.end());
		return vUrls;
	}

	// 请求必须经 IHttp::Run 提交给 HTTP 引擎线程，否则永远不被调度：
	// Done() 恒 false、Progress() 恒 0（安装卡 0% 不动、预览卡「加载中」不减的根因）。
	std::shared_ptr<IHttpRequest> StartRequest(IHttp *pHttp, IStorage *pStorage, const char *pDir, const std::string &File, const std::vector<std::string> &vUrls, size_t UrlIndex)
	{
		if(UrlIndex >= vUrls.size())
			return nullptr;
		char aDest[IO_MAX_PATH_LENGTH];
		if(str_comp(pDir, qm_font_download::DIRECTORY) == 0)
		{
			if(!qm_font_download::TargetPath(File, aDest, sizeof(aDest)) || !qm_font_download::EnsureDirectory(*pStorage))
				return nullptr;
		}
		else
		{
			str_format(aDest, sizeof(aDest), "%s/%s", pDir, File.c_str());
			pStorage->CreateFolder("qmclient", IStorage::TYPE_SAVE);
			pStorage->CreateFolder(pDir, IStorage::TYPE_SAVE);
		}
		std::shared_ptr<IHttpRequest> pRequest(CreateHttpRequest(vUrls[UrlIndex].c_str()));
		pRequest->WriteToFile(pStorage, aDest, IStorage::TYPE_SAVE);
		pRequest->Timeout(CTimeout{4000, 0, 500, 5});
		pRequest->LogProgress(HTTPLOG::NONE);
		pHttp->Run(pRequest);
		return pRequest;
	}

	SInstallJob *FindInstallJob(size_t FamilyIndex)
	{
		for(SInstallJob &Job : s_vInstallJobs)
			if(Job.m_FamilyIndex == FamilyIndex)
				return &Job;
		return nullptr;
	}

	void StartInstallFile(IHttp *pHttp, IStorage *pStorage, SInstallJob &Job)
	{
		std::vector<SEntry> &vEntries = Entries(pStorage);
		const SEntry &Entry = vEntries[Families(pStorage)[Job.m_FamilyIndex].m_vEntryIndices[Job.m_FileCursor]];
		Job.m_pRequest = StartRequest(pHttp, pStorage, qm_font_download::DIRECTORY, Entry.m_File, EntryDownloadUrls(Entry), Job.m_UrlIndex);
		if(Job.m_pRequest == nullptr)
		{
			Job.m_Failed = true;
			s_StateVersion++;
			log_warn("fontstore", "Could not create font download target for '%s'", Entry.m_File.c_str());
		}
	}

	// UI 发起安装（含重试）：同一族同时只允许一个任务；已存在的文件直接跳过，
	// 支持手工放置部分字体文件后的断点续装。
	void StartInstall(IHttp *pHttp, IStorage *pStorage, size_t FamilyIndex)
	{
		if(FindInstallJob(FamilyIndex) != nullptr)
			return;
		std::vector<SFamily> &vFamilies = Families(pStorage);
		if(FamilyIndex >= vFamilies.size())
			return;
		SFamily &Family = vFamilies[FamilyIndex];
		SInstallJob Job;
		Job.m_FamilyIndex = FamilyIndex;
		std::vector<SEntry> &vEntries = Entries(pStorage);
		while(Job.m_FileCursor < Family.m_vEntryIndices.size())
		{
			if(!qm_font_download::Installed(*pStorage, vEntries[Family.m_vEntryIndices[Job.m_FileCursor]].m_File))
				break;
			++Job.m_FileCursor;
		}
		if(Job.m_FileCursor >= Family.m_vEntryIndices.size())
		{
			Family.m_Installed = true;
			return;
		}
		StartInstallFile(pHttp, pStorage, Job);
		s_vInstallJobs.push_back(std::move(Job));
		s_StateVersion++;
	}

	// 重试：清掉失败记录后重新发起。
	void RetryInstall(IHttp *pHttp, IStorage *pStorage, size_t FamilyIndex)
	{
		s_vInstallJobs.erase(std::remove_if(s_vInstallJobs.begin(), s_vInstallJobs.end(), [FamilyIndex](const SInstallJob &Job) { return Job.m_FamilyIndex == FamilyIndex; }), s_vInstallJobs.end());
		StartInstall(pHttp, pStorage, FamilyIndex);
	}

	// 商店弹层中可见的字体按帧入队（内部去重）；队列有上限防快速滚动时无限膨胀。
	// 大文件（CJK/可变/多样式族）同样自动预览：浏览到才入队、一次一个串行下载，
	// 且 fontcache 跨会话持久缓存（成功文件不删除、重启不重下），CDN 负载可控。
	void EnsurePreviewQueued(IStorage *pStorage, size_t FamilyIndex)
	{
		std::vector<SFamily> &vFamilies = Families(pStorage);
		if(FamilyIndex >= vFamilies.size())
			return;
		const SFamily &Family = vFamilies[FamilyIndex];
		if(Family.m_Installed || Family.m_PreviewReady || Family.m_PreviewFailed)
			return;
		if(s_ActivePreview && s_ActivePreview->m_FamilyIndex == FamilyIndex)
			return;
		for(const SPreviewJob &Job : s_vPreviewQueue)
			if(Job.m_FamilyIndex == FamilyIndex)
				return;
		if(s_vPreviewQueue.size() >= 48)
			return;
		SPreviewJob Job;
		Job.m_FamilyIndex = FamilyIndex;
		s_vPreviewQueue.push_back(std::move(Job));
	}

	// 弹层关闭时丢弃未开始的预览任务，避免后台继续下载用户看不到的字体。
	void ClearPreviewQueue()
	{
		s_vPreviewQueue.clear();
	}

	// 每帧推进一次（仅渲染趟调用）：安装任务优先占用下载通道（文件推进/换镜像/
	// 完成后重扫），空闲时按队列逐个拉取预览字体。
	void Update(IHttp *pHttp, IStorage *pStorage, ITextRender *pTextRender, bool PreviewActive)
	{
		std::vector<SEntry> &vEntries = Entries(pStorage);
		std::vector<SFamily> &vFamilies = Families(pStorage);

		bool InstallActive = false;
		for(SInstallJob &Job : s_vInstallJobs)
		{
			if(Job.m_Done || Job.m_Failed)
				continue;
			if(Job.m_pRequest && !Job.m_pRequest->Done())
			{
				InstallActive = true;
				continue;
			}
			if(Job.m_pRequest && Job.m_pRequest->State() != EHttpState::DONE)
			{
				// 当前文件失败：换下一条镜像重试。
				++Job.m_UrlIndex;
				const SEntry &Entry = vEntries[vFamilies[Job.m_FamilyIndex].m_vEntryIndices[Job.m_FileCursor]];
				const std::vector<std::string> vUrls = EntryDownloadUrls(Entry);
				if(Job.m_UrlIndex < vUrls.size())
				{
					log_info("fontstore", "Retrying '%s' via next mirror", Entry.m_File.c_str());
					Job.m_pRequest = StartRequest(pHttp, pStorage, qm_font_download::DIRECTORY, Entry.m_File, vUrls, Job.m_UrlIndex);
					if(Job.m_pRequest == nullptr)
					{
						Job.m_Failed = true;
						s_StateVersion++;
					}
					else
						InstallActive = true;
				}
				else
				{
					Job.m_pRequest = nullptr;
					Job.m_Failed = true;
					s_StateVersion++;
					log_warn("fontstore", "Download of '%s' failed on all mirrors", Entry.m_File.c_str());
				}
				continue;
			}
			if(Job.m_pRequest)
			{
				// 当前文件完成：推进游标并跳过已存在文件（续装）。
				Job.m_pRequest = nullptr;
				Job.m_UrlIndex = 0;
				++Job.m_FileCursor;
				s_StateVersion++;
				SFamily &Family = vFamilies[Job.m_FamilyIndex];
				while(Job.m_FileCursor < Family.m_vEntryIndices.size())
				{
					if(!qm_font_download::Installed(*pStorage, vEntries[Family.m_vEntryIndices[Job.m_FileCursor]].m_File))
						break;
					++Job.m_FileCursor;
				}
				if(Job.m_FileCursor >= Family.m_vEntryIndices.size())
				{
					Job.m_Done = true;
					Family.m_Installed = true;
					s_ReloadPending = true;
					continue;
				}
			}
			StartInstallFile(pHttp, pStorage, Job);
			InstallActive = true;
		}

		// 全部安装任务结束后统一重扫字体目录一次，并刷新安装状态。
		if(s_ReloadPending && std::all_of(s_vInstallJobs.begin(), s_vInstallJobs.end(), [](const SInstallJob &Job) { return Job.m_Done || Job.m_Failed; }))
		{
			pTextRender->ReloadCustomFonts();
			s_ReloadPending = false;
			s_StateVersion++;
			RefreshInstalled(pStorage);
		}
		// 已完成的任务出队（失败任务保留，供状态位显示重试按钮）。
		s_vInstallJobs.erase(std::remove_if(s_vInstallJobs.begin(), s_vInstallJobs.end(), [](const SInstallJob &Job) { return Job.m_Done; }), s_vInstallJobs.end());

		// 预览：弹层打开时才启动新任务，安装占用通道时让路；失败尽力而为。
		if(PreviewActive && !s_ActivePreview && !InstallActive && !s_vPreviewQueue.empty())
		{
			while(!s_vPreviewQueue.empty())
			{
				SPreviewJob Job = s_vPreviewQueue.front();
				s_vPreviewQueue.erase(s_vPreviewQueue.begin());
				SFamily &Family = vFamilies[Job.m_FamilyIndex];
				if(Family.m_Installed || Family.m_PreviewReady)
					continue;
				const SEntry &Entry = vEntries[Family.m_vEntryIndices.front()];
				char aCachePath[IO_MAX_PATH_LENGTH];
				str_format(aCachePath, sizeof(aCachePath), "qmclient/fontcache/%s", Entry.m_File.c_str());
				// 跨会话缓存命中：fontcache 里已有文件直接本地加载，不再发起网络
				// 请求（预览文件成功后永不删除，同一 URL 每个客户端只拉一次，
				// 对 CDN 保持回源友好的行为）；本地解析失败视为半截或坏文件，
				// 清掉后照常走下载路径重下一次。
				if(pStorage->FileExists(aCachePath, IStorage::TYPE_SAVE))
				{
					if(pTextRender->QmEnsurePreviewFace(Entry.m_Family.c_str(), aCachePath))
					{
						Family.m_PreviewReady = true;
						s_StateVersion++;
						continue;
					}
					pStorage->RemoveFile(aCachePath, IStorage::TYPE_SAVE);
				}
				s_ActivePreview = Job;
				s_ActivePreview->m_pRequest = StartRequest(pHttp, pStorage, "qmclient/fontcache", Entry.m_File, EntryDownloadUrls(Entry), 0);
				break;
			}
		}
		if(s_ActivePreview && s_ActivePreview->m_pRequest && s_ActivePreview->m_pRequest->Done())
		{
			SFamily &Family = vFamilies[s_ActivePreview->m_FamilyIndex];
			const SEntry &Entry = vEntries[Family.m_vEntryIndices.front()];
			char aCachePath[IO_MAX_PATH_LENGTH];
			str_format(aCachePath, sizeof(aCachePath), "qmclient/fontcache/%s", Entry.m_File.c_str());
			if(s_ActivePreview->m_pRequest->State() == EHttpState::DONE && pTextRender->QmEnsurePreviewFace(Entry.m_Family.c_str(), aCachePath))
			{
				Family.m_PreviewReady = true;
				s_StateVersion++;
			}
			else
			{
				// 下载失败、内容不是有效字体或族名与文件内部族名不一致：
				// 清除坏缓存并标记失败，避免每次可见都重新发起下载。
				if(s_ActivePreview->m_pRequest->State() == EHttpState::DONE)
					pStorage->RemoveFile(aCachePath, IStorage::TYPE_SAVE);
				Family.m_PreviewFailed = true;
				s_StateVersion++;
			}
			s_ActivePreview.reset();
		}
	}
} // namespace fontstore

// ============ QmClient 字体商店弹层 ============
// DoPopupMenu 的回调是函数指针，不能捕获 lambda：弹层状态集中在本上下文，
// 以文件级静态对象存活（跨开关保留搜索词与滚动位置）。
struct SFontStorePopupContext
{
	CMenus *m_pMenus = nullptr;
	char m_aSearch[64] = "";
	char m_aSearchPrev[64] = "";
	CLineInput m_SearchInput;
	int m_Category = 0; // 0=全部，1..5 = sans-serif/serif/display/handwriting/monospace
	int m_CategoryPrev = 0;
	int m_InstallFilter = 0; // 0=全部，1=未安装，2=已安装
	int m_InstallFilterPrev = 0;
	size_t m_InstalledCountPrev = (size_t)-1; // 已安装族数量：任何安装完成都会变化，驱动筛选结果刷新
	std::vector<size_t> m_vResults; // 过滤后的族索引
	std::vector<CButtonContainer> m_vCardButtons;
	bool m_New = false;

	SFontStorePopupContext() :
		m_SearchInput(m_aSearch, sizeof(m_aSearch)) {}
};
static SFontStorePopupContext s_FontStorePopupCtx;
static SPopupMenuId s_FontStorePopupId;

CUi::EPopupMenuFunctionResult CMenus::PopupFontStore(void *pContext, CUIRect View, bool Active)
{
	SFontStorePopupContext *pCtx = static_cast<SFontStorePopupContext *>(pContext);
	CMenus *pSelf = pCtx->m_pMenus;
	CUi *pUi = pSelf->Ui();
	ITextRender *pTextRender = pSelf->TextRender();
	IStorage *pStorage = pSelf->Storage();
	const std::vector<fontstore::SFamily> &vFamilies = fontstore::Families(pStorage);
	const SSettingsCardDeckVisualOptions CardVisual = pSelf->SettingsCardDeckVisualOptions();
	const SQmDropdownVisualStyle Style = QmSettingsDropdownVisualStyle(pSelf->m_SettingsUiTheme, CardVisual.m_BorderColor);

	// 推进下载状态机：弹层每帧必然渲染，字体卡滚出屏幕也不中断安装/预览。
	fontstore::Update(pSelf->Http(), pStorage, pTextRender, true);

	IUiContext HeaderCtx;
	HeaderCtx.m_pUi = pUi;
	static ui_widget::SSecondaryPanelLabel s_Title;
	static CButtonContainer s_CloseButton;
	ui_widget::CSecondaryPanel Panel(HeaderCtx, View, Active, ui_widget::ResolveSecondaryPanelMetrics(pUi->Screen()->w), {});
	if(Panel.Header(s_Title, s_CloseButton, Localize("Font store")))
		return CUi::POPUP_CLOSE_CURRENT_AND_DESCENDANTS;
	View = Panel.ContentRect();

	// 顶部栏：搜索框 + 分类下拉 + 结果数量。
	CUIRect TopBar, List;
	View.HSplitTop(LineSize, &TopBar, &List);
	List.HSplitTop(MarginSmall, nullptr, &List);

	CUIRect SearchRect, CategoryRect, FilterRect, CountRect;
	// 顶部栏（从右往左切）：字体总数 | 安装筛选 | 分类 —— 两个下拉等宽；
	// 剩余整段给搜索框（最左，视觉主入口）。
	const float FilterWidth = std::min(150.0f, TopBar.w * 0.20f);
	const float CountWidth = std::min(150.0f, TopBar.w * 0.18f);
	TopBar.VSplitRight(CountWidth, &TopBar, &CountRect);
	TopBar.VSplitRight(MarginSmall, &TopBar, nullptr);
	TopBar.VSplitRight(FilterWidth, &TopBar, &FilterRect);
	TopBar.VSplitRight(MarginSmall, &TopBar, nullptr);
	TopBar.VSplitRight(FilterWidth, &TopBar, &CategoryRect);
	SearchRect = TopBar;
	SearchRect.VSplitRight(MarginSmall, &SearchRect, nullptr);

	IUiContext TextInputCtx;
	TextInputCtx.m_pUi = pUi;
	TextInputCtx.m_pAnim = &pSelf->GameClient()->UiRuntimeV2()->AnimRuntime();
	TextInputCtx.m_pTree = &pSelf->GameClient()->UiRuntimeV2()->Tree();
	TextInputCtx.m_ScopeHash = MakeUiScopeHash("settings_tclient_font_store_popup");
	TextInputCtx.m_FrameDt = pSelf->GameClient()->UiRuntimeV2()->FrameDt();
	ui_widget::SInputFieldOptions SearchOptions;
	SearchOptions.m_Mode = ui_widget::EInputFieldMode::SEARCH;
	SearchOptions.m_pPlaceholder = Localize("Search online fonts…");
	SearchOptions.m_Clearable = true;
	ui_widget::InputField(TextInputCtx, &pCtx->m_SearchInput, SearchRect, SearchOptions);

	// 分类名每帧重新 Localize：语言切换会重载本地化库并释放全部旧翻译串
	// （localization.cpp Load 的 Clear 分支调用 m_StringsHeap.Reset），
	// 缓存 Localize 返回的指针会在切语言后悬空；标签栏（1164 行）有按
	// 语言文件键控的刷新条件，这里没有键控需求，每帧取用最安全。
	const char *aCategoryNames[] = {Localize("All"), Localize("Sans-serif"), Localize("Serif"), Localize("Display"), Localize("Handwriting"), Localize("Monospace")};
	static CUi::SDropDownState s_CategoryState;
	static CScrollRegion s_CategoryScrollRegion;
	s_CategoryState.m_SelectionPopupContext.m_pScrollRegion = &s_CategoryScrollRegion;
	// 弹窗内没有激活裁剪区，显式提供下拉弹层 viewport，避免回退逻辑越界。
	// viewport 用弹窗「外框」（内容区加回边框+边距环）：分类按钮贴着内容区
	// 左缘，若直接用内容区做 viewport，几何钳制会把选择列表右移压窄，锚点
	// 对齐判定失效，外框无法把按钮与列表包成一个整体；用外框后钳制边界恰好
	// 等于内容区边界，列表与按钮完全对齐，且仍不会溢出商店弹窗。
	CUIRect PopupOuterRect = View;
	PopupOuterRect.Margin(-CUi::PopupMenuContentInset() * 0.5f, &PopupOuterRect);
	CUi::SDropDownProperties CategoryProps;
	CategoryProps.m_Enabled = Active;
	CategoryProps.m_pAnchorViewport = &PopupOuterRect;
	CategoryProps.m_pPopupViewport = &PopupOuterRect;
	// 字体商店的下拉框属于当前弹窗，父回调打开子层的下一帧不能按禁用策略关闭它。
	CategoryProps.m_RequireSourceRefresh = false;
	CategoryProps.m_ClosePopupWhenDisabled = false;
	pCtx->m_Category = pSelf->DoSettingsDropDown(&CategoryRect, pCtx->m_Category, aCategoryNames, (int)std::size(aCategoryNames), s_CategoryState, CategoryProps);

	// 安装状态筛选：全部 / 未安装 / 已安装（与卡片底部状态文字同语义）。
	const char *aInstallFilterNames[] = {Localize("All"), Localize("Not installed"), Localize("Installed")};
	static CUi::SDropDownState s_InstallFilterState;
	static CScrollRegion s_InstallFilterScrollRegion;
	s_InstallFilterState.m_SelectionPopupContext.m_pScrollRegion = &s_InstallFilterScrollRegion;
	pCtx->m_InstallFilter = pSelf->DoSettingsDropDown(&FilterRect, pCtx->m_InstallFilter, aInstallFilterNames, (int)std::size(aInstallFilterNames), s_InstallFilterState, CategoryProps);

	// 初次打开或过滤条件变化时重建结果集（两千余族线性扫描，代价可忽略）；
	// 否则弹窗首次显示为空列表，要切换一次分类才会出现内容。
	// 已安装计数变化（下载完成/文件被删）同样触发，保证状态筛选即时准确。
	size_t InstalledCount = 0;
	for(const fontstore::SFamily &Family : vFamilies)
		InstalledCount += Family.m_Installed ? 1 : 0;
	if(pCtx->m_New || str_comp(pCtx->m_aSearch, pCtx->m_aSearchPrev) != 0 || pCtx->m_Category != pCtx->m_CategoryPrev || pCtx->m_InstallFilter != pCtx->m_InstallFilterPrev || InstalledCount != pCtx->m_InstalledCountPrev)
	{
		str_copy(pCtx->m_aSearchPrev, pCtx->m_aSearch, sizeof(pCtx->m_aSearchPrev));
		pCtx->m_CategoryPrev = pCtx->m_Category;
		pCtx->m_InstallFilterPrev = pCtx->m_InstallFilter;
		pCtx->m_InstalledCountPrev = InstalledCount;
		pCtx->m_vResults.clear();
		static const char *const apCategoryKeys[] = {"", "sans-serif", "serif", "display", "handwriting", "monospace"};
		const char *pCategoryKey = apCategoryKeys[std::clamp(pCtx->m_Category, 0, 5)];
		for(size_t fi = 0; fi < vFamilies.size(); ++fi)
		{
			if(pCategoryKey[0] != '\0' && vFamilies[fi].m_Category != pCategoryKey)
				continue;
			// 安装状态：RefreshInstalled 在弹层每帧按文件存在性刷新，筛选即时准确。
			if(pCtx->m_InstallFilter == 1 && vFamilies[fi].m_Installed)
				continue;
			if(pCtx->m_InstallFilter == 2 && !vFamilies[fi].m_Installed)
				continue;
			if(pCtx->m_aSearch[0] != '\0' && str_find_nocase(vFamilies[fi].m_Name.c_str(), pCtx->m_aSearch) == nullptr)
				continue;
			pCtx->m_vResults.push_back(fi);
		}
		// 覆盖当前界面语言脚本的族排在前面（stable_sort 保持组内字母序）；
		// 拉丁系语言无对应子集，保持原序。语言切换只能在弹窗外进行，重开
		// 弹窗（m_New）必然触发重建，无需额外键控。
		if(const char *pLanguageSubset = fontstore::LanguageSubsetKey())
		{
			const auto Covers = [pLanguageSubset, &vFamilies](size_t fi) {
				return std::find(vFamilies[fi].m_vSubsets.begin(), vFamilies[fi].m_vSubsets.end(), pLanguageSubset) != vFamilies[fi].m_vSubsets.end();
			};
			std::stable_sort(pCtx->m_vResults.begin(), pCtx->m_vResults.end(), [&Covers](size_t a, size_t b) {
				return Covers(a) && !Covers(b);
			});
		}
	}
	char aCount[64];
	str_format(aCount, sizeof(aCount), "%s %" PRIzu, Localize("Total fonts:"), pCtx->m_vResults.size());
	pUi->DoLabel(&CountRect, aCount, FontSize, TEXTALIGN_MR);

	// 卡片网格：多列方块卡片（类似资源页 workshop 商店），滚动区裁剪。
	static CScrollRegion s_GridScrollRegion;
	vec2 ScrollOffset(0.0f, 0.0f);
	// 滚动条样式与一级设置页同源（SETTINGS_OUTER 策略）：粗细/边距/配色统一。
	const float UiScale = pSelf->SettingsPageUiScale(pUi->Screen()->w);
	const SQmResolvedScrollPolicy ScrollPolicy = QmResolveScrollPolicy({EQmScrollProfile::SETTINGS_OUTER}, UiScale, 0.0f);
	CScrollRegionParams ScrollParams = QmScrollRegionParamsFromPolicy(ScrollPolicy);
	// 滚轮步长按卡片行计（每格 2 行）：目录数千行，沿用设置页固定 120px/格 体感过慢。
	const float Gap = MarginSmall;
	const float CardH = 108.0f;
	ScrollParams.m_ScrollUnit = (CardH + Gap) * 2.0f;
	s_GridScrollRegion.Begin(&List, &ScrollOffset, &ScrollParams);
	List.y += ScrollOffset.y;

	pCtx->m_vCardButtons.resize(vFamilies.size());

	// 每行 4 卡：期望宽度按 4 等分推导，窄弹窗时 Columns 公式自动减列。
	const float CardW = std::clamp((List.w - 3.0f * Gap) / 4.0f, 150.0f, 320.0f);
	const int Columns = std::max(1, (int)((List.w + Gap) / (CardW + Gap)));
	const float RowW = Columns * CardW + (Columns - 1) * Gap;

	size_t Index = 0;
	while(Index < pCtx->m_vResults.size())
	{
		CUIRect RowRect;
		List.HSplitTop(CardH + Gap, &RowRect, &List);
		RowRect.x += (List.w - RowW) / 2.0f;
		for(int Col = 0; Col < Columns && Index < pCtx->m_vResults.size(); ++Col, ++Index)
		{
			CUIRect Card;
			Card.x = RowRect.x + Col * (CardW + Gap);
			Card.y = RowRect.y;
			Card.w = CardW;
			Card.h = CardH;

			const size_t FamilyIndex = pCtx->m_vResults[Index];
			const fontstore::SFamily &Family = vFamilies[FamilyIndex];

			// 视口剔除：不可见卡片不上报、不绘制、不触发预览入队；
			// 上报内容矩形同时让滚动区获得正确的内容高度。
			if(!s_GridScrollRegion.AddRect(Card))
				continue;

			CButtonContainer &CardButton = pCtx->m_vCardButtons[FamilyIndex];
			const bool Clicked = pUi->DoButtonLogic(&CardButton, 0, &Card, BUTTONFLAG_LEFT) != 0;
			const bool Hot = pUi->HotItem() == &CardButton;

			// 卡片底：与设置卡片同源的面色 + 卡片边框色；悬浮叠加高亮。
			DrawRoundedSurface(pUi, Card, CardVisual.m_SurfaceColor, CardVisual.m_BorderColor, ui_token::radius::BASE, 1.0f);
			if(Hot)
				DrawRoundedSurface(pUi, Card, Style.m_ActiveEntryColor, ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), ui_token::radius::BASE);

			// 可见字体的预览按需入队（大文件同样自动下载）。
			if(Active)
				fontstore::EnsurePreviewQueued(pStorage, FamilyIndex);

			CUIRect Inner, SubsetsRect, BottomRect;
			Card.Margin(6.0f, &Inner);
			Inner.HSplitBottom(LineSize, &Inner, &BottomRect);
			Inner.HSplitBottom(11.0f, &Inner, &SubsetsRect);

			// 脚本覆盖徽标：小字暗色，展示族覆盖的脚本子集（最多 2 个 + 截断），
			// 帮助非拉丁语言用户快速识别可用字体（日文/韩文/西里尔等）。
			if(!Family.m_vSubsets.empty())
			{
				std::vector<const char *> vpNames;
				vpNames.reserve(Family.m_vSubsets.size());
				for(const std::string &Subset : Family.m_vSubsets)
					if(const char *pName = fontstore::LocalizedSubsetName(Subset))
						vpNames.push_back(pName);
				if(!vpNames.empty())
				{
					char aSubsets[128];
					aSubsets[0] = '\0';
					const size_t Shown = std::min<size_t>(vpNames.size(), 2);
					for(size_t si = 0; si < Shown; ++si)
					{
						if(si > 0)
							str_append(aSubsets, " · ", sizeof(aSubsets));
						str_append(aSubsets, vpNames[si], sizeof(aSubsets));
					}
					if(vpNames.size() > Shown)
					{
						char aMore[24];
						str_format(aMore, sizeof(aMore), " · +%" PRIzu, vpNames.size() - Shown);
						str_append(aSubsets, aMore, sizeof(aSubsets));
					}
					pTextRender->TextColor(pTextRender->DefaultTextColor().WithAlpha(0.55f));
					pUi->DoLabel(&SubsetsRect, aSubsets, SmallFontSize, TEXTALIGN_ML);
					pTextRender->TextColor(pTextRender->DefaultTextColor());
				}
			}

			// 预览区：暗底方框（与字体卡预览同款：黑 30% 半透明、无边框），
			// 内容（族名 + 固定样例两行 [ + 状态提示]）在暗底内垂直居中，
			// 消除顶部对齐留下的底部空白；缺字部分沿回退链渲染。
			std::string PreviewConfig;
			const bool FaceReady = (Family.m_Installed || Family.m_PreviewReady) && pTextRender->QmFontFamilyDefaultConfig(Family.m_Name.c_str(), PreviewConfig);
			const char *pHint = nullptr;
			if(!FaceReady)
			{
				const fontstore::SInstallJob *pAnyJob = fontstore::FindInstallJob(FamilyIndex);
				if(Family.m_PreviewFailed)
					pHint = Localize("Preview unavailable");
				else if(pAnyJob == nullptr)
					pHint = Localize("Preview loading…");
			}
			DrawRoundedSurface(pUi, Inner, ColorRGBA(0.0f, 0.0f, 0.0f, 0.30f), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), ui_token::radius::BASE);
			CUIRect Preview;
			Inner.Margin(5.0f, &Preview);
			const float ContentH = pHint != nullptr ? 55.0f : 41.0f;
			const float PreviewY = Preview.y + std::max(0.0f, (Preview.h - ContentH) * 0.5f);
			if(FaceReady)
				pTextRender->SetFontPreviewFace(PreviewConfig.c_str());
			pTextRender->TextColor(pTextRender->DefaultTextColor().WithAlpha(FaceReady ? 1.0f : 0.45f));
			pTextRender->Text(Preview.x, PreviewY, 12.0f, Family.m_Name.c_str(), Preview.w);
			// 固定样例文案：拉丁+数字 与 CJK（汉字/假名）各一行，直观展示该
			// 字体的实际字形效果；未就绪的族同样渲染（压暗），视觉密度一致。
			pTextRender->Text(Preview.x, PreviewY + 16.0f, 10.0f, "Aa Bb Gg 0123456789", Preview.w);
			pTextRender->Text(Preview.x, PreviewY + 30.0f, 10.0f, "永久 字体 テスト", Preview.w);
			if(pHint != nullptr)
				pTextRender->Text(Preview.x, PreviewY + 44.0f, 9.0f, pHint, Preview.w);
			pTextRender->TextColor(pTextRender->DefaultTextColor());
			if(FaceReady)
				pTextRender->SetFontPreviewFace(nullptr);

			// 底部行：族名（普通字体，始终可读）+ 右侧状态。
			char aFamilyClamped[64];
			str_copy(aFamilyClamped, Family.m_Name.c_str(), sizeof(aFamilyClamped));
			pUi->DoLabel(&BottomRect, aFamilyClamped, FontSize, TEXTALIGN_ML);
			CUIRect StatusRect;
			BottomRect.VSplitRight(64.0f, &BottomRect, &StatusRect);
			const fontstore::SInstallJob *pJob = fontstore::FindInstallJob(FamilyIndex);
			if(Family.m_Installed)
			{
				pTextRender->TextColor(pTextRender->DefaultTextColor().WithAlpha(0.6f));
				pUi->DoLabel(&StatusRect, Localize("Installed"), FontSize, TEXTALIGN_MR);
				pTextRender->TextColor(pTextRender->DefaultTextColor());
			}
			else if(pJob != nullptr && !pJob->m_Failed && pJob->m_pRequest != nullptr)
			{
				const size_t TotalFiles = std::max<size_t>(Family.m_vEntryIndices.size(), 1);
				const float DoneFiles = (float)pJob->m_FileCursor + (pJob->m_pRequest ? pJob->m_pRequest->Progress() / 100.0f : 0.0f);
				char aProgress[32];
				str_format(aProgress, sizeof(aProgress), "%d%%", (int)(DoneFiles * 100.0f / (float)TotalFiles));
				pUi->DoLabel(&StatusRect, aProgress, FontSize, TEXTALIGN_MR);
			}
			else if(pJob != nullptr && pJob->m_Failed)
			{
				pTextRender->TextColor(ColorRGBA(1.0f, 0.4f, 0.4f, 1.0f));
				pUi->DoLabel(&StatusRect, Localize("Retry"), FontSize, TEXTALIGN_MR);
				pTextRender->TextColor(pTextRender->DefaultTextColor());
			}
			else
			{
				pTextRender->TextColor(pTextRender->DefaultTextColor().WithAlpha(0.8f));
				pUi->DoLabel(&StatusRect, Localize("Download"), FontSize, TEXTALIGN_MR);
				pTextRender->TextColor(pTextRender->DefaultTextColor());
			}

			// 点击卡片：安装；失败卡片点击即重试。
			if(Clicked)
			{
				if(pJob != nullptr && pJob->m_Failed)
					fontstore::RetryInstall(pSelf->Http(), pStorage, FamilyIndex);
				else if(!Family.m_Installed && pJob == nullptr)
				{
					fontstore::StartInstall(pSelf->Http(), pStorage, FamilyIndex);
					fontstore::s_LastInstallFamily = (int)FamilyIndex;
				}
			}
		}
	}
	s_GridScrollRegion.End();

	pCtx->m_New = false;
	return CUi::POPUP_KEEP_OPEN;
}

namespace
{
	// 字重下拉条目：字体族的静态样式面（"Family Style" 的样式段）与可变字重
	// 标准档（按轴范围过滤）合并，供字体行右侧的并列字重下拉使用。
	struct SQmFontWeightEntry
	{
		std::string m_Label;
		const std::string *m_pFullStyle = nullptr; // 静态条目指向的完整 "Family Style"
		int m_Value = 400; // 可变条目的数值字重
		bool m_Variable = false;
	};

	void QmBuildFontWeightEntries(const std::vector<std::string> &vStyles, const char *pFamilyDisplay, bool Variable, int VarMin, int VarMax, std::vector<SQmFontWeightEntry> &vEntries)
	{
		vEntries.clear();
		for(const std::string &Style : vStyles)
		{
			SQmFontWeightEntry Entry;
			Entry.m_pFullStyle = &Style;
			const size_t FamilyLen = str_length(pFamilyDisplay);
			if(FamilyLen > 0 && str_startswith_nocase(Style.c_str(), pFamilyDisplay) && Style[FamilyLen] == ' ')
				Entry.m_Label = Style.substr(FamilyLen + 1);
			else
				Entry.m_Label = Style;
			vEntries.push_back(std::move(Entry));
		}
		if(Variable)
		{
			static constexpr int s_aValues[] = {100, 200, 300, 400, 500, 600, 700, 800, 900};
			static constexpr const char *s_apNames[] = {"Thin", "ExtraLight", "Light", "Regular", "Medium", "SemiBold", "Bold", "ExtraBold", "Black"};
			for(size_t i = 0; i < std::size(s_aValues); ++i)
			{
				if(s_aValues[i] < VarMin || s_aValues[i] > VarMax)
					continue;
				// 与静态样式同名的标准档去重（如静态 Regular 面已存在）。
				if(std::any_of(vEntries.begin(), vEntries.end(), [&](const SQmFontWeightEntry &Entry) { return !Entry.m_Variable && str_comp_nocase(Entry.m_Label.c_str(), s_apNames[i]) == 0; }))
					continue;
				SQmFontWeightEntry Entry;
				Entry.m_Label = s_apNames[i];
				Entry.m_Value = s_aValues[i];
				Entry.m_Variable = true;
				vEntries.push_back(std::move(Entry));
			}
		}
		if(vEntries.empty())
		{
			// 字体族不可解析（或中文未设置跟随英文）时的占位条目，选择无效果。
			SQmFontWeightEntry Entry;
			Entry.m_Label = "Regular";
			vEntries.push_back(std::move(Entry));
		}
	}

	// 族与样式均查询真实已加载 face。旧配置别名不会变成新的候选族，
	// 缺失字体保留原配置显示；不在绘制时改写用户配置。
	void QmExtractConfigFamily(ITextRender *pTextRender, const char *pConfig, char *pBuffer, size_t BufferSize, std::string &CanonicalConfig)
	{
		const SQmResolvedFontSelection Selection = QmResolveSettingsFontSelection(pConfig,
			[pTextRender](const char *pName, std::string &Family, std::string &Style) { return pTextRender->QmResolveCustomFont(pName, Family, Style); });
		str_copy(pBuffer, Selection.m_Family.c_str(), BufferSize);
		// 与字体族列表的显示规则一致；配置保留真实名称，匹配忽略旧版分隔符。
		for(char *pChr = pBuffer; *pChr != '\0'; ++pChr)
			if(*pChr == '-')
				*pChr = ' ';
		CanonicalConfig = Selection.m_Config;
	}

	// 计算字重下拉的选中项：配置含样式段时匹配静态条目；族名配置下可变字体
	// 取最接近当前数值的标准档，静态族优先 Regular，否则取首条。
	int QmSelectFontWeightEntry(const std::vector<SQmFontWeightEntry> &vEntries, const char *pConfig, const char *pFamilyDisplay, int CurWeight)
	{
		int Selected = -1;
		for(size_t i = 0; i < vEntries.size(); ++i)
		{
			const SQmFontWeightEntry &Entry = vEntries[i];
			if(Entry.m_pFullStyle && QmFontNamesEqual(pConfig, Entry.m_pFullStyle->c_str()))
				Selected = (int)i;
		}
		if(Selected >= 0)
			return Selected;
		if(QmFontNamesEqual(pConfig, pFamilyDisplay))
		{
			int Best = -1;
			int BestDist = std::numeric_limits<int>::max();
			for(size_t i = 0; i < vEntries.size(); ++i)
			{
				const SQmFontWeightEntry &Entry = vEntries[i];
				if(!Entry.m_Variable)
					continue;
				const int Dist = std::abs(Entry.m_Value - CurWeight);
				if(Dist < BestDist)
				{
					BestDist = Dist;
					Best = (int)i;
				}
			}
			if(Best >= 0)
				return Best;
		}
		for(size_t i = 0; i < vEntries.size(); ++i)
			if(str_comp_nocase(vEntries[i].m_Label.c_str(), "Regular") == 0)
				return (int)i;
		return 0;
	}

	// 可变字重滑杆的节流应用状态：ApplyRender 推进渲染层字重（渲染层按 face 轴
	// 钳制、坐标真变才清图集），FinishHeavy 执行拖动结束后的重量级收尾（布局失效
	// 与窗口重排，拖动中反复调用会造成明显卡顿）。
	struct SQmWeightThrottleState
	{
		int m_AppliedWeight = std::numeric_limits<int>::min(); // 已推进到渲染层的值
		int m_SeenWeight = std::numeric_limits<int>::min(); // 上帧读到的滑杆值
		int m_FinishedWeight = std::numeric_limits<int>::min(); // 已执行收尾的值
		float m_LastApplyTime = -10.0f;
		float m_LastChangeTime = 0.0f;
	};

	void QmTickVariableWeightThrottle(SQmWeightThrottleState &State, int Weight, float Now, const std::function<void(int)> &ApplyRender, const std::function<void()> &FinishHeavy)
	{
		constexpr float ApplyInterval = 0.10f; // 拖动中最小应用间隔
		constexpr float SettleDelay = 0.15f; // 值稳定该时长视为停手
		if(Weight != State.m_SeenWeight)
		{
			State.m_SeenWeight = Weight;
			State.m_LastChangeTime = Now;
		}
		if(State.m_AppliedWeight == std::numeric_limits<int>::min())
			State.m_AppliedWeight = Weight;
		if(State.m_FinishedWeight == std::numeric_limits<int>::min())
			State.m_FinishedWeight = Weight;
		if(Weight != State.m_AppliedWeight &&
			(Now - State.m_LastApplyTime >= ApplyInterval || Now - State.m_LastChangeTime >= SettleDelay))
		{
			ApplyRender(Weight);
			State.m_AppliedWeight = Weight;
			State.m_LastApplyTime = Now;
		}
		if(Weight != State.m_FinishedWeight && Now - State.m_LastChangeTime >= SettleDelay)
		{
			FinishHeavy();
			State.m_FinishedWeight = Weight;
		}
	}
} // namespace

float CMenus::LayoutTClientThemeCacheSection(CUIRect &CurrentColumn, bool Render)
{
	CUIRect Label, Button, TmpLabel;
	const float SavedY = CurrentColumn.y;
	CUIRect BoxRect = CurrentColumn;
	CurrentColumn.HSplitTop(Margin, nullptr, &CurrentColumn);
	BoxRect = CurrentColumn;
	CurrentColumn.HSplitTop(HeadlineHeight, Render ? &Label : &TmpLabel, &CurrentColumn);
	if(Render)
	{
		CUIElement &TitleElement = SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-visual-font-title");
		DoSettingsLabelStreamed(TitleElement, &Label, Localize("Font"), HeadlineFontSize, TEXTALIGN_ML, TClientFixedLabelProperties(HeadlineFontSize, Label.w));
	}
	CurrentColumn.HSplitTop(MarginSmall, nullptr, &CurrentColumn);
	CTClientSettingsRowAllocator Rows(CurrentColumn);

	Button = Rows.Next();
	// 英文族可变字重判定：可变族不放字重下拉，改由下方专用滑杆行承载任意
	// 字重值（轴真实范围）。判定在测量与渲染两趟一致，驱动卡片高度过渡动画。
	char aLatinFamily[256];
	std::string LatinCanonicalConfig;
	QmExtractConfigFamily(TextRender(), g_Config.m_QmCustomFont, aLatinFamily, sizeof(aLatinFamily), LatinCanonicalConfig);
	int LatinVarMin = 100, LatinVarMax = 900;
	std::string LatinFamilyDefaultConfig;
	const bool LatinFamilyAvailable = TextRender()->QmFontFamilyDefaultConfig(aLatinFamily, LatinFamilyDefaultConfig);
	const bool LatinVariable = LatinFamilyAvailable && TextRender()->CustomFontWeightRange(LatinFamilyDefaultConfig.c_str(), LatinVarMin, LatinVarMax);
	if(Render)
	{
		Button.VSplitLeft(100.0f, &Label, &Button);
		CUIElement &CustomFontElement = SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-custom-font-label");
		DoSettingsLabelStreamed(CustomFontElement, &Label, Localize("English font:"), FontSize, TEXTALIGN_ML, TClientFixedLabelProperties(FontSize, Label.w));
		static CSettingsFontSelection s_FontSelection;
		static CUi::SDropDownState s_FontDropDownState;
		static CScrollRegion s_FontDropDownScrollRegion;
		s_FontDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_FontDropDownScrollRegion;
		s_FontDropDownState.m_SelectionPopupContext.m_SpecialFontRenderMode = true;
		s_FontDropDownState.m_SelectionPopupContext.m_FontFaceAvailabilityCheck = true;
		s_FontDropDownState.m_SelectionPopupContext.m_FontFamilySelection = true;
		const auto &CustomFaces = *TextRender()->GetCustomFaces();
		static size_t s_FontPrewarmIndex = 0;
		if(s_FontSelection.Update(CustomFaces, aLatinFamily, nullptr, Localize("Default")))
			s_FontPrewarmIndex = 0;
		const auto &s_FontDropDownNamesOwned = s_FontSelection.Families();
		const auto &s_FontDropDownNames = s_FontSelection.Names();
		const int FontSelectedOld = s_FontSelection.Selected();
		CUIRect FontDirectory;
		Button.VSplitRight(20.0f, &Button, &FontDirectory);
		Button.VSplitRight(MarginSmall, &Button, nullptr);
		// 字重子选项与族名下拉并列（右侧固定宽）：无需单独文案，下拉文本即样式名。
		// 可变族不放字重下拉（下方滑杆行承载），族下拉吃满行宽。
		CUIRect WeightButton;
		if(!LatinVariable)
		{
			Button.VSplitRight(110.0f, &Button, &WeightButton);
			Button.VSplitRight(MarginSmall, &Button, nullptr);
		}
		const int FontSelectedNew = DoSettingsDropDown(&Button, FontSelectedOld, s_FontDropDownNames.data(), s_FontDropDownNames.size(), s_FontDropDownState, {}, g_Config.m_QmCustomFont);
		std::string LatinSelectedConfig;
		if(FontSelectedOld != FontSelectedNew && s_FontSelection.IsFamilySelection(FontSelectedNew) && TextRender()->QmFontFamilyDefaultConfig(s_FontDropDownNames[FontSelectedNew], LatinSelectedConfig) && LatinSelectedConfig.size() < sizeof(g_Config.m_QmCustomFont))
		{
			str_copy(g_Config.m_QmCustomFont, LatinSelectedConfig.c_str());
			InvalidateTClientSettingsRuntimeCacheSections(ESettingsCacheDirtyReason::FONT);
			TextRender()->SetCustomFace(g_Config.m_QmCustomFont);
			InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::FONT_CHANGED);
			GameClient()->OnWindowResize();
			GameClient()->Editor()->OnWindowResize();
			GameClient()->m_MapImages.SetTextureScale(101);
			GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
		}
		static CButtonContainer s_FontDirectoryId;
		if(Ui()->DoButton_QmIcon(&s_FontDirectoryId, EQmIcon::FOLDER, FONT_ICON_FOLDER, 0, &FontDirectory, IGraphics::CORNER_ALL))
		{
			char aBuf[IO_MAX_PATH_LENGTH];
			if(qm_font_download::CompleteDirectoryPath(*Storage(), aBuf, sizeof(aBuf)))
				Client()->ViewFile(aBuf);
		}
		// 预热字体预览字形：弹层关闭时每帧只切换一个候选字体，把该字体标签
		// 的字形以全透明提前光栅化进图集。否则弹层首帧要集中为所有可见候选
		// 光栅化并可能触发图集扩容，造成可感知的主线程卡顿（鼠标卡住）。
		if(!Ui()->IsPopupOpen(&s_FontDropDownState.m_SelectionPopupContext) && s_FontPrewarmIndex < s_FontDropDownNamesOwned.size())
		{
			const std::string &PrewarmFace = s_FontDropDownNamesOwned[s_FontPrewarmIndex];
			std::string PrewarmConfig;
			const bool PrewarmAvailable = TextRender()->QmFontFamilyDefaultConfig(PrewarmFace.c_str(), PrewarmConfig);
			TextRender()->SetFontPreviewFace(PrewarmAvailable ? PrewarmConfig.c_str() : nullptr);
			TextRender()->TextColor(TextRender()->DefaultTextColor().WithAlpha(0.0f));
			TextRender()->Text(Button.x, Button.y, FontSize, PrewarmFace.c_str(), -1.0f);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetFontPreviewFace(nullptr);
			++s_FontPrewarmIndex;
		}
		// 英文字体的字重子选项：静态样式面下拉（可变族改由下方滑杆行承载）。
		if(!LatinVariable)
		{
			static std::vector<std::string> s_LatinStylesOwned;
			static std::vector<SQmFontWeightEntry> s_LatinWeightEntries;
			static std::vector<const char *> s_LatinWeightLabels;
			static CUi::SDropDownState s_LatinWeightState;
			static CScrollRegion s_LatinWeightScrollRegion;
			s_LatinWeightState.m_SelectionPopupContext.m_pScrollRegion = &s_LatinWeightScrollRegion;
			s_LatinWeightState.m_SelectionPopupContext.m_SpecialFontRenderMode = true;
			s_LatinWeightState.m_SelectionPopupContext.m_FontFaceAvailabilityCheck = true;
			s_LatinStylesOwned = *TextRender()->GetCustomFontStyles(aLatinFamily);
			QmBuildFontWeightEntries(s_LatinStylesOwned, aLatinFamily, LatinVariable, LatinVarMin, LatinVarMax, s_LatinWeightEntries);
			s_LatinWeightLabels.clear();
			s_LatinWeightLabels.reserve(s_LatinWeightEntries.size());
			for(const SQmFontWeightEntry &Entry : s_LatinWeightEntries)
				s_LatinWeightLabels.push_back(Entry.m_Label.c_str());
			const int LatinWeightSelected = QmSelectFontWeightEntry(s_LatinWeightEntries, LatinCanonicalConfig.c_str(), aLatinFamily, g_Config.m_QmCustomFontWeight);
			CUi::SDropDownProperties LatinWeightProperties;
			LatinWeightProperties.m_Enabled = !s_LatinStylesOwned.empty();
			const int LatinWeightNew = DoSettingsDropDown(&WeightButton, LatinWeightSelected, s_LatinWeightLabels.data(), s_LatinWeightLabels.size(), s_LatinWeightState, LatinWeightProperties, &g_Config.m_QmCustomFontWeight);
			if(LatinWeightNew >= 0 && (size_t)LatinWeightNew < s_LatinWeightEntries.size() && LatinWeightNew != LatinWeightSelected)
			{
				const SQmFontWeightEntry &Entry = s_LatinWeightEntries[LatinWeightNew];
				bool Changed = false;
				if(Entry.m_Variable)
				{
					std::string VariableConfig;
					if(QmResolveVariableFontSelection(aLatinFamily, g_Config.m_QmCustomFont, sizeof(g_Config.m_QmCustomFont), [this](const char *pFamily, std::string &Config) { return TextRender()->QmFontFamilyDefaultConfig(pFamily, Config); }, VariableConfig))
					{
						str_copy(g_Config.m_QmCustomFont, VariableConfig.c_str());
						Changed = true;
					}
					if(g_Config.m_QmCustomFontWeight != Entry.m_Value)
					{
						g_Config.m_QmCustomFontWeight = Entry.m_Value;
						TextRender()->SetCustomFontWeight(Entry.m_Value);
						Changed = true;
					}
				}
				else if(Entry.m_pFullStyle && str_comp_nocase(g_Config.m_QmCustomFont, Entry.m_pFullStyle->c_str()) != 0)
				{
					str_copy(g_Config.m_QmCustomFont, Entry.m_pFullStyle->c_str());
					Changed = true;
				}
				if(Changed)
				{
					TextRender()->SetCustomFace(g_Config.m_QmCustomFont);
					InvalidateTClientSettingsRuntimeCacheSections(ESettingsCacheDirtyReason::FONT);
					InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::FONT_CHANGED);
					GameClient()->OnWindowResize();
				}
			}
		}
	}
	// 英文可变字重滑杆行：仅当前英文族为可变字体时新增，卡片高度过渡由设置
	// 卡片外壳按测量变化自动承载。拖动实时预览（节流推进渲染层，渲染层坐标
	// 真变才清图集），停手稳定后归一配置为族名并做布局收尾。
	if(LatinVariable)
		Button = Rows.Next();
	if(Render && LatinVariable)
	{
		Button.VSplitLeft(100.0f, &Label, &Button);
		CUIElement &LatinWeightElement = SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-latin-weight-label");
		DoSettingsLabelStreamed(LatinWeightElement, &Label, Localize("Font weight:"), FontSize, TEXTALIGN_ML, TClientFixedLabelProperties(FontSize, Label.w));
		static int s_LatinWeightSliderId;
		RenderQmSettingsSliderWithValueInput(&s_LatinWeightSliderId, Button, &g_Config.m_QmCustomFontWeight, LatinVarMin, LatinVarMax, "", false);
		static SQmWeightThrottleState s_LatinWeightThrottle;
		QmTickVariableWeightThrottle(s_LatinWeightThrottle, g_Config.m_QmCustomFontWeight, Client()->GlobalTime(), [this](int Weight) { TextRender()->SetCustomFontWeight(Weight); }, [&]() {
				// 权重应用于真实 face；收尾只使用族 API 的可往返配置，保留消歧所需样式。
				std::string VariableConfig;
					if(QmResolveVariableFontSelection(aLatinFamily, g_Config.m_QmCustomFont, sizeof(g_Config.m_QmCustomFont), [this](const char *pFamily, std::string &Config) { return TextRender()->QmFontFamilyDefaultConfig(pFamily, Config); }, VariableConfig))
					{
						str_copy(g_Config.m_QmCustomFont, VariableConfig.c_str());
					TextRender()->SetCustomFace(g_Config.m_QmCustomFont);
				}
				InvalidateTClientSettingsRuntimeCacheSections(ESettingsCacheDirtyReason::FONT);
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::FONT_CHANGED);
				GameClient()->OnWindowResize(); });
	}
	// QmClient: CJK 字体行（中日韩共用槽位，码点分类含假名/谚文/汉字）——
	// 只影响 CJK 字形，留空跟随英文字体（原回退链）；日语用户建议选
	// Noto Sans JP、韩语用户选 Noto Sans KR 以获得地道字形。右侧并列独立的
	// 字重下拉（CJK 字重只作用于中文分类面）。
	Button = Rows.Next();
	// CJK 族可变字重判定：可变族不放字重下拉，改由下方专用滑杆行承载任意
	// 字重值。CJK 未设置（跟随英文）时视为不可变，维持占位下拉不变。
	char aCjkFamily[256];
	std::string CjkCanonicalConfig;
	QmExtractConfigFamily(TextRender(), g_Config.m_QmCustomFontCjk, aCjkFamily, sizeof(aCjkFamily), CjkCanonicalConfig);
	int CjkVarMin = 100, CjkVarMax = 900;
	std::string CjkFamilyDefaultConfig;
	const bool CjkFamilyAvailable = TextRender()->QmFontFamilyDefaultConfig(aCjkFamily, CjkFamilyDefaultConfig);
	const bool CjkVariable = CjkFamilyAvailable && TextRender()->CustomFontWeightRange(CjkFamilyDefaultConfig.c_str(), CjkVarMin, CjkVarMax);
	if(Render)
	{
		Button.VSplitLeft(100.0f, &Label, &Button);
		CUIElement &CjkFontElement = SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-cjk-font-label");
		DoSettingsLabelStreamed(CjkFontElement, &Label, Localize("Chinese font:"), FontSize, TEXTALIGN_ML, TClientFixedLabelProperties(FontSize, Label.w));
		static std::vector<std::string> s_CjkFacesSource;
		static std::string s_CjkConfigSource;
		static std::vector<std::string> s_CjkEligibleFamilies;
		static CSettingsFontSelection s_CjkSelection;
		static CUi::SDropDownState s_CjkDropDownState;
		static CScrollRegion s_CjkDropDownScrollRegion;
		s_CjkDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_CjkDropDownScrollRegion;
		s_CjkDropDownState.m_SelectionPopupContext.m_SpecialFontRenderMode = true;
		s_CjkDropDownState.m_SelectionPopupContext.m_FontFaceAvailabilityCheck = true;
		s_CjkDropDownState.m_SelectionPopupContext.m_FontFamilySelection = true;
		const auto &CustomFaces = *TextRender()->GetCustomFaces();
		if(s_CjkFacesSource != CustomFaces || s_CjkConfigSource != g_Config.m_QmCustomFontCjk)
		{
			s_CjkFacesSource = CustomFaces;
			s_CjkConfigSource = g_Config.m_QmCustomFontCjk;
			// CJK 槽只收含 CJK 字形的 face：纯拉丁字体选进该槽没有意义（无字形
			// 可渲染，等同回退默认链）。当前配置值例外保留，避免已选项从列表
			// 消失后下拉显示错位。
			s_CjkEligibleFamilies.clear();
			for(const auto &FaceName : CustomFaces)
			{
				const bool IsCurrentConfig = QmFontNamesEqual(aCjkFamily, FaceName.c_str());
				std::string FamilyDefaultConfig;
				const bool FamilyAvailable = TextRender()->QmFontFamilyDefaultConfig(FaceName.c_str(), FamilyDefaultConfig);
				if(IsCurrentConfig || (FamilyAvailable && TextRender()->QmFaceHasCjk(FamilyDefaultConfig.c_str())))
					s_CjkEligibleFamilies.push_back(FaceName);
			}
		}
		s_CjkSelection.Update(s_CjkEligibleFamilies, aCjkFamily, Localize("(Follow English font)"), Localize("Default"));
		const auto &s_CjkDropDownNamesOwned = s_CjkSelection.Families();
		const auto &s_CjkDropDownNames = s_CjkSelection.Names();
		CUIRect CjkWeightButton;
		if(!CjkVariable)
		{
			Button.VSplitRight(110.0f, &Button, &CjkWeightButton);
			Button.VSplitRight(MarginSmall, &Button, nullptr);
		}
		const int CjkSelectedOld = s_CjkSelection.Selected();
		const int CjkSelectedNew = DoSettingsDropDown(&Button, CjkSelectedOld, s_CjkDropDownNames.data(), s_CjkDropDownNames.size(), s_CjkDropDownState, {}, g_Config.m_QmCustomFontCjk);
		std::string CjkSelectedConfig;
		const bool CjkSelectionAvailable = CjkSelectedNew == 0 || (CjkSelectedNew > 0 && (size_t)CjkSelectedNew <= s_CjkDropDownNamesOwned.size() && TextRender()->QmFontFamilyDefaultConfig(s_CjkDropDownNamesOwned[CjkSelectedNew - 1].c_str(), CjkSelectedConfig) && CjkSelectedConfig.size() < sizeof(g_Config.m_QmCustomFontCjk));
		if(CjkSelectedNew != CjkSelectedOld && CjkSelectionAvailable)
		{
			if(CjkSelectedNew == 0)
				g_Config.m_QmCustomFontCjk[0] = '\0';
			else
				str_copy(g_Config.m_QmCustomFontCjk, CjkSelectedConfig.c_str());
			InvalidateTClientSettingsRuntimeCacheSections(ESettingsCacheDirtyReason::FONT);
			TextRender()->SetCustomFaceCjk(g_Config.m_QmCustomFontCjk);
			InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::FONT_CHANGED);
			GameClient()->OnWindowResize();
		}
		// 中文字体的字重子选项（中文未设置时为占位，选择无效果；可变族改由
		// 下方滑杆行承载）。
		if(!CjkVariable)
		{
			static std::vector<std::string> s_CjkStylesOwned;
			static std::vector<SQmFontWeightEntry> s_CjkWeightEntries;
			static std::vector<const char *> s_CjkWeightLabels;
			static CUi::SDropDownState s_CjkWeightState;
			static CScrollRegion s_CjkWeightScrollRegion;
			s_CjkWeightState.m_SelectionPopupContext.m_pScrollRegion = &s_CjkWeightScrollRegion;
			s_CjkWeightState.m_SelectionPopupContext.m_SpecialFontRenderMode = true;
			s_CjkWeightState.m_SelectionPopupContext.m_FontFaceAvailabilityCheck = true;
			s_CjkStylesOwned = *TextRender()->GetCustomFontStyles(aCjkFamily);
			QmBuildFontWeightEntries(s_CjkStylesOwned, aCjkFamily, CjkVariable, CjkVarMin, CjkVarMax, s_CjkWeightEntries);
			s_CjkWeightLabels.clear();
			s_CjkWeightLabels.reserve(s_CjkWeightEntries.size());
			for(const SQmFontWeightEntry &Entry : s_CjkWeightEntries)
				s_CjkWeightLabels.push_back(Entry.m_Label.c_str());
			const int CjkWeightSelected = QmSelectFontWeightEntry(s_CjkWeightEntries, CjkCanonicalConfig.c_str(), aCjkFamily, g_Config.m_QmCustomFontWeightCjk);
			CUi::SDropDownProperties CjkWeightProperties;
			CjkWeightProperties.m_Enabled = !s_CjkStylesOwned.empty();
			const int CjkWeightNew = DoSettingsDropDown(&CjkWeightButton, CjkWeightSelected, s_CjkWeightLabels.data(), s_CjkWeightLabels.size(), s_CjkWeightState, CjkWeightProperties, &g_Config.m_QmCustomFontWeightCjk);
			if(CjkWeightNew >= 0 && (size_t)CjkWeightNew < s_CjkWeightEntries.size() && CjkWeightNew != CjkWeightSelected)
			{
				const SQmFontWeightEntry &Entry = s_CjkWeightEntries[CjkWeightNew];
				bool Changed = false;
				if(Entry.m_Variable && aCjkFamily[0] != '\0')
				{
					std::string VariableConfig;
					if(QmResolveVariableFontSelection(aCjkFamily, g_Config.m_QmCustomFontCjk, sizeof(g_Config.m_QmCustomFontCjk), [this](const char *pFamily, std::string &Config) { return TextRender()->QmFontFamilyDefaultConfig(pFamily, Config); }, VariableConfig))
					{
						str_copy(g_Config.m_QmCustomFontCjk, VariableConfig.c_str());
						Changed = true;
					}
					if(g_Config.m_QmCustomFontWeightCjk != Entry.m_Value)
					{
						g_Config.m_QmCustomFontWeightCjk = Entry.m_Value;
						TextRender()->SetCustomFontWeightCjk(Entry.m_Value);
						Changed = true;
					}
				}
				else if(Entry.m_pFullStyle && str_comp_nocase(g_Config.m_QmCustomFontCjk, Entry.m_pFullStyle->c_str()) != 0)
				{
					str_copy(g_Config.m_QmCustomFontCjk, Entry.m_pFullStyle->c_str());
					Changed = true;
				}
				if(Changed)
				{
					TextRender()->SetCustomFaceCjk(g_Config.m_QmCustomFontCjk);
					InvalidateTClientSettingsRuntimeCacheSections(ESettingsCacheDirtyReason::FONT);
					InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::FONT_CHANGED);
					GameClient()->OnWindowResize();
				}
			}
		}
	}
	// CJK 可变字重滑杆行：仅当前 CJK 族为可变字体时新增（CJK 未设置不出现）。
	// 拖动实时预览与停手收尾逻辑与英文滑杆行一致。
	if(CjkVariable)
		Button = Rows.Next();
	if(Render && CjkVariable)
	{
		Button.VSplitLeft(100.0f, &Label, &Button);
		CUIElement &CjkWeightElement = SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-cjk-weight-label");
		DoSettingsLabelStreamed(CjkWeightElement, &Label, Localize("CJK font weight:"), FontSize, TEXTALIGN_ML, TClientFixedLabelProperties(FontSize, Label.w));
		static int s_CjkWeightSliderId;
		RenderQmSettingsSliderWithValueInput(&s_CjkWeightSliderId, Button, &g_Config.m_QmCustomFontWeightCjk, CjkVarMin, CjkVarMax, "", false);
		static SQmWeightThrottleState s_CjkWeightThrottle;
		QmTickVariableWeightThrottle(s_CjkWeightThrottle, g_Config.m_QmCustomFontWeightCjk, Client()->GlobalTime(), [this](int Weight) { TextRender()->SetCustomFontWeightCjk(Weight); }, [&]() {
				// 归一为族名：可变字重只对族 face 生效，样式段配置会挡住轴调节。
				std::string VariableConfig;
					if(QmResolveVariableFontSelection(aCjkFamily, g_Config.m_QmCustomFontCjk, sizeof(g_Config.m_QmCustomFontCjk), [this](const char *pFamily, std::string &Config) { return TextRender()->QmFontFamilyDefaultConfig(pFamily, Config); }, VariableConfig))
					{
						str_copy(g_Config.m_QmCustomFontCjk, VariableConfig.c_str());
					TextRender()->SetCustomFaceCjk(g_Config.m_QmCustomFontCjk);
				}
				InvalidateTClientSettingsRuntimeCacheSections(ESettingsCacheDirtyReason::FONT);
				InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::FONT_CHANGED);
				GameClient()->OnWindowResize(); });
	}
	// QmClient: 图标符号行 —— 星号、心形、几何形状等符号字形与私用区图标。
	Button = Rows.Next();
	if(Render)
	{
		Button.VSplitLeft(100.0f, &Label, &Button);
		CUIElement &IconsFontElement = SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-icons-font-label");
		DoSettingsLabelStreamed(IconsFontElement, &Label, Localize("Icon symbols:"), FontSize, TEXTALIGN_ML, TClientFixedLabelProperties(FontSize, Label.w));
		static CSettingsFontSelection s_IconsSelection;
		static CUi::SDropDownState s_IconsDropDownState;
		static CScrollRegion s_IconsDropDownScrollRegion;
		s_IconsDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_IconsDropDownScrollRegion;
		s_IconsDropDownState.m_SelectionPopupContext.m_SpecialFontRenderMode = true;
		s_IconsDropDownState.m_SelectionPopupContext.m_FontFaceAvailabilityCheck = true;
		s_IconsDropDownState.m_SelectionPopupContext.m_FontFamilySelection = true;
		const auto &CustomFaces = *TextRender()->GetCustomFaces();
		char aIconsFamily[256];
		std::string IconsCanonicalConfig;
		QmExtractConfigFamily(TextRender(), g_Config.m_QmCustomFontIcons, aIconsFamily, sizeof(aIconsFamily), IconsCanonicalConfig);
		s_IconsSelection.Update(CustomFaces, aIconsFamily, Localize("(Follow Chinese font)"), Localize("Default"));
		const auto &s_IconsDropDownNamesOwned = s_IconsSelection.Families();
		const auto &s_IconsDropDownNames = s_IconsSelection.Names();
		const int IconsSelectedOld = s_IconsSelection.Selected();
		const int IconsSelectedNew = DoSettingsDropDown(&Button, IconsSelectedOld, s_IconsDropDownNames.data(), s_IconsDropDownNames.size(), s_IconsDropDownState, {}, g_Config.m_QmCustomFontIcons);
		std::string IconsSelectedConfig;
		const bool IconsSelectionAvailable = IconsSelectedNew == 0 || (IconsSelectedNew > 0 && (size_t)IconsSelectedNew <= CustomFaces.size() && TextRender()->QmFontFamilyDefaultConfig(s_IconsDropDownNamesOwned[IconsSelectedNew - 1].c_str(), IconsSelectedConfig) && IconsSelectedConfig.size() < sizeof(g_Config.m_QmCustomFontIcons));
		if(IconsSelectedNew != IconsSelectedOld && IconsSelectionAvailable)
		{
			if(IconsSelectedNew == 0)
				g_Config.m_QmCustomFontIcons[0] = '\0';
			else
				str_copy(g_Config.m_QmCustomFontIcons, IconsSelectedConfig.c_str());
			InvalidateTClientSettingsRuntimeCacheSections(ESettingsCacheDirtyReason::FONT);
			TextRender()->SetCustomFaceIcons(g_Config.m_QmCustomFontIcons);
			InvalidateSettingsRuntimeCaches(ESettingsInvalidationReason::FONT_CHANGED);
			GameClient()->OnWindowResize();
		}
	}
	// QmClient: 预览区 —— 「预览」标签 + 方框（背景+边框），内部三行分别展示
	// 英文、中文与图标符号字形的实际效果（走当前配置的字体回退链）。
	{
		const float PreviewBoxH = LineSize * 3.0f + MarginSmall * 2.0f + 6.0f;
		Button = Rows.Next(PreviewBoxH);
		if(Render)
		{
			Button.VSplitLeft(100.0f, &Label, &Button);
			CUIElement &PreviewElement = SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-font-preview-label");
			DoSettingsLabelStreamed(PreviewElement, &Label, Localize("Preview"), FontSize, TEXTALIGN_ML, TClientFixedLabelProperties(FontSize, Label.w));
			// 预览框与激光/头衔等 QmClient 预览行同款：暗色半透明底、无高亮边框。
			DrawRoundedSurface(Ui(), Button, ColorRGBA(0.0f, 0.0f, 0.0f, 0.3f), ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f), ui_token::radius::BASE);
			CUIRect Inner;
			Button.Margin(MarginSmall + 2.0f, &Inner);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->Text(Inner.x, Inner.y, FontSize, "The quick brown fox 0123", -1.0f);
			TextRender()->Text(Inner.x, Inner.y + LineSize, FontSize, "中文字体预览，笔画清晰锐利", -1.0f);
			// 图标行符号全部落在分类图标码点范围内（qm_font_category.h），
			// 行宽宽裕，尽量覆盖常见字形分类。
			TextRender()->Text(Inner.x, Inner.y + LineSize * 2.0f, FontSize, "★ ☆ ♥ ♦ ♣ ♠ ✓ ✗ ♪ ♫ ● ○ ■ □ ▲ ▼ → ←", -1.0f);
		}
	}
	// QmClient: 字体商店 —— 点击打开可搜索的大弹层（workshop 风格卡片网格）。
	{
		// 目录加载（测量趟也需要，保证行数与渲染趟一致）。
		const std::vector<fontstore::SFamily> &vStoreFamilies = fontstore::Families(Storage());
		Button = Rows.Next();
		if(Render)
		{
			Button.VSplitLeft(100.0f, &Label, &Button);
			CUIElement &StoreElement = SettingsTextElement(SETTINGS_TCLIENT, m_TClientSettingsTab, "tclient-font-store-label");
			DoSettingsLabelStreamed(StoreElement, &Label, Localize("Font store:"), FontSize, TEXTALIGN_ML, TClientFixedLabelProperties(FontSize, Label.w));

			// 状态位（右侧）：最近一次安装任务的进度 / 失败重试 / 已安装。
			CUIRect StoreStatus;
			Button.VSplitRight(64.0f, &Button, &StoreStatus);
			Button.VSplitRight(MarginSmall, &Button, nullptr);

			static CButtonContainer s_StoreBrowseId;
			if(DoButton_Menu(&s_StoreBrowseId, Localize("Browse font store…"), 0, &Button))
			{
				s_FontStorePopupCtx.m_pMenus = this;
				s_FontStorePopupCtx.m_New = true;
				fontstore::ClearPreviewQueue();
				const SPopupMenuProperties PopupProps = ui_widget::SecondaryPanelProperties();
				const CUIRect PanelRect = ResolveSettingsSecondaryPanelRect(*Ui()->Screen());
				const float PopupWidth = PanelRect.w;
				const float PopupHeight = PanelRect.h;
				Ui()->DoPopupMenu(&s_FontStorePopupId, 0.0f, 0.0f, PopupWidth, PopupHeight, &s_FontStorePopupCtx, PopupFontStore, PopupProps);
			}
			GameClient()->m_Tooltips.DoToolTip(&s_StoreBrowseId, &Button, Localize("Download fonts from the internet instead of shipping them with the client"));

			// 弹层打开时状态机由弹层自身每帧推进（字体卡滚出屏幕也不会中断）；
			// 弹层关闭后在这里兜底推进安装任务（状态位重试按钮发起的任务）。
			if(!Ui()->IsPopupOpen(&s_FontStorePopupId))
				fontstore::Update(Http(), Storage(), TextRender(), false);

			// 状态位渲染：进度 / 重试 / 已安装。
			const int LastInstall = fontstore::s_LastInstallFamily;
			if(LastInstall >= 0 && (size_t)LastInstall < vStoreFamilies.size())
			{
				const fontstore::SFamily &LastFamily = vStoreFamilies[(size_t)LastInstall];
				const fontstore::SInstallJob *pJob = fontstore::FindInstallJob((size_t)LastInstall);
				if(pJob != nullptr && !pJob->m_Failed && pJob->m_pRequest != nullptr)
				{
					// 下载中：状态位显示整族进度文本，不可交互。
					const size_t TotalFiles = std::max<size_t>(LastFamily.m_vEntryIndices.size(), 1);
					const float DoneFiles = (float)pJob->m_FileCursor + (pJob->m_pRequest ? pJob->m_pRequest->Progress() / 100.0f : 0.0f);
					char aProgress[32];
					str_format(aProgress, sizeof(aProgress), "%d%%", (int)(DoneFiles * 100.0f / (float)TotalFiles));
					Ui()->DoLabel(&StoreStatus, aProgress, FontSize, TEXTALIGN_MC);
				}
				else if(pJob != nullptr && pJob->m_Failed)
				{
					static CButtonContainer s_StoreRetryId;
					if(DoButton_Menu(&s_StoreRetryId, Localize("Retry"), 0, &StoreStatus))
					{
						fontstore::RetryInstall(Http(), Storage(), (size_t)LastInstall);
						fontstore::s_LastInstallFamily = LastInstall;
					}
				}
				else if(LastFamily.m_Installed)
				{
					Ui()->DoLabel(&StoreStatus, Localize("Installed"), FontSize, TEXTALIGN_MC);
				}
			}
		}
	}
	BoxRect.h = CurrentColumn.y - BoxRect.y;
	return CurrentColumn.y - SavedY;
}

SSettingsSection CMenus::BuildTClientThemeCacheSection()
{
	SSettingsSection S;
	S.m_pName = "Font";
	ConfigureSettingsCardSection(S, Localizable("Font"), "tclient:font", [this](CUIRect &Col, bool Render) -> float { return LayoutTClientThemeCacheSection(Col, Render); }, Margin);
	S.m_DependencyConfigInts = {&g_Config.m_QmCustomFontWeight};
	return S;
}
SSettingsSection CMenus::BuildTClientCursorCacheSection()
{
	SSettingsSection S;
	S.m_pName = "Visual: Cursor";
	ConfigureSettingsCardSection(S, Localizable("Visual: Cursor"), "tclient:cursor", [this](CUIRect &Col, bool Render) -> float {
		CUIRect Label, Button, Tmp;
		const float SavedY = Col.y;
		Col.HSplitTop(Margin, nullptr, &Col);
		Col.HSplitTop(HeadlineHeight, Render ? &Label : &Tmp, &Col);
		if(Render)
			DoSettingsMenuLabel(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-cursor-title", &Label, Localize("Visual: Cursor"), HeadlineFontSize, TEXTALIGN_ML);
		Col.HSplitTop(MarginSmall, nullptr, &Col);
		CTClientSettingsRowAllocator Rows(Col);
		Button = Rows.Next();
		if(Render)
			DoSettingsScrollbarOption(SETTINGS_TCLIENT, m_TClientSettingsTab, m_TClientSettingsTab, "tclient-cursor-scale", &g_Config.m_QmCursorScale, &g_Config.m_QmCursorScale, &Button, Localize("Ingame cursor scale"), 0, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
		return Col.y - SavedY; }, Margin);
	S.m_DependencyConfigInts = {&g_Config.m_QmCursorScale};
	return S;
}