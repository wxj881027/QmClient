#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_FONT_STORE_CATALOG_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_FONT_STORE_CATALOG_H

#include "font_download_storage.h"

#include <base/log.h>

#include <engine/engine.h>
#include <engine/shared/jobs.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

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

	inline bool IsValidFontFileName(const std::string &File)
	{
		return qm_font_download::ValidFilename(File);
	}

	// 仓库相对路径会拼进下载 URL 与落盘路径：锁死形态，禁止任何穿越写法。
	inline bool IsValidRepoPath(const std::string &Path)
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
	template<typename TEntryAllocator, typename TFamilyAllocator>
	void LoadCatalog(IStorage *pStorage, std::vector<SEntry, TEntryAllocator> &vEntries, std::vector<SFamily, TFamilyAllocator> &vFamilies)
	{
		void *pCatalogData = nullptr;
		unsigned CatalogSize = 0;
		// TYPE_ALL 会先搜用户目录再搜数据目录：用户侧同名目录可整体覆盖随包目录。
		const bool Read = pStorage->ReadFile("fonts/fonts_store/font_catalog.json", IStorage::TYPE_ALL, &pCatalogData, &CatalogSize);
		const std::unique_ptr<void, decltype(&free)> pFile(pCatalogData, free);
		if(!Read)
			return;

		const std::unique_ptr<json_value, decltype(&json_value_free)> pJson(json_parse((json_char *)pFile.get(), CatalogSize), json_value_free);
		// 同一对候选完成后才替换调用方状态，异常不能留下先写入的族索引。
		std::vector<SEntry, TEntryAllocator> vCandidateEntries(vEntries, vEntries.get_allocator());
		std::vector<SFamily, TFamilyAllocator> vCandidateFamilies(vFamilies, vFamilies.get_allocator());
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
						Family.m_vEntryIndices.push_back(vCandidateEntries.size());
						vCandidateFamilies.push_back(std::move(Family));
						It = FamilyIndex.emplace(LowerFamily, vCandidateFamilies.size() - 1).first;
					}
					else
						vCandidateFamilies[It->second].m_vEntryIndices.push_back(vCandidateEntries.size());
					vCandidateEntries.push_back(std::move(Parsed));
				}
			}
		}
		else
		{
			log_warn("fontstore", "Failed to parse font catalog data/fonts/fonts_store/font_catalog.json");
		}
		log_info("fontstore", "Font catalog loaded with %" PRIzu " files in %" PRIzu " families", vCandidateEntries.size(), vCandidateFamilies.size());
		vEntries.swap(vCandidateEntries);
		vFamilies.swap(vCandidateFamilies);
	}

	struct SCatalog
	{
		std::vector<SEntry> m_vEntries;
		std::vector<SFamily> m_vFamilies;
		bool m_Loaded = false;
	};

	// worker 的结果独立于弹层，关闭/重开不会重新解析或发布半成品。
	class CCatalogLoadJob : public IJob
	{
		IStorage *m_pStorage;
		SCatalog m_Result;
		bool m_Published = false;
		void Run() override
		{
			SCatalog Candidate;
			LoadCatalog(m_pStorage, Candidate.m_vEntries, Candidate.m_vFamilies);
			for(SFamily &Family : Candidate.m_vFamilies)
				Family.m_Installed = std::all_of(Family.m_vEntryIndices.begin(), Family.m_vEntryIndices.end(), [&](size_t Index) {
					return qm_font_download::Installed(*m_pStorage, Candidate.m_vEntries[Index].m_File);
				});
			Candidate.m_Loaded = true;
			m_Result = std::move(Candidate);
		}

	public:
		explicit CCatalogLoadJob(IStorage *pStorage) :
			m_pStorage(pStorage) {}

		bool TakeResult(SCatalog &Result)
		{
			if(State() != STATE_DONE || m_Published)
				return false;
			Result = std::move(m_Result);
			m_Published = true;
			return true;
		}
	};
	// 只有引擎确认接收后才发布等待身份，拒绝或构造分配失败可沿原入口再试。
	inline bool TryQueueCatalogLoadJob(std::shared_ptr<CCatalogLoadJob> &pPending, IEngine &Engine, IStorage *pStorage)
	{
		if(pPending)
			return false;
		try
		{
			auto pCandidate = std::make_shared<CCatalogLoadJob>(pStorage);
			if(!Engine.TryAddJob(pCandidate))
				return false;
			pPending = std::move(pCandidate);
			return true;
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
	}

}

#endif
