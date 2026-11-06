#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_AUDIO_PACK_CANDIDATES_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_AUDIO_PACK_CANDIDATES_H

#include <base/str.h>
#include <base/system.h>

#include <engine/storage.h>

#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace qm_audio_pack
{
	struct SCandidateEntry
	{
		std::string m_Path;
		std::string m_DisplayName;
		bool m_IsCurrentFile = false;
		bool m_IsCurrentPackFile = false;
	};

	struct SScanRoot
	{
		const char *m_pScanRoot;
		const char *m_pOutputPrefix;
	};

	inline constexpr std::array<SScanRoot, 2> CandidateScanRoots()
	{
		return {{
			{"audio", "audio"},
			{"data/audio", "audio"},
		}};
	}

	inline bool TryBuildCandidatePath(const char *pOutputPrefix, const char *pRelativePath, std::string &OutPath)
	{
		OutPath.clear();

		const char *pResolvedRelativePath = pRelativePath != nullptr ? pRelativePath : "";
		while(*pResolvedRelativePath == '/')
			++pResolvedRelativePath;

		if(pResolvedRelativePath[0] == '\0' || !str_endswith(pResolvedRelativePath, ".wv"))
			return false;

		const char *pResolvedOutputPrefix = pOutputPrefix != nullptr && pOutputPrefix[0] != '\0' ? pOutputPrefix : "audio";
		OutPath = pResolvedOutputPrefix;
		OutPath += "/";
		OutPath += pResolvedRelativePath;
		return true;
	}

	inline std::vector<SCandidateEntry> BuildCandidateEntries(const std::vector<std::string> &vPaths, const char *pPackName, const char *pCurrentPath)
	{
		std::vector<SCandidateEntry> vEntries;
		vEntries.reserve(vPaths.size());

		std::unordered_set<std::string> vSeenPaths;
		const char *pResolvedPackName = pPackName != nullptr && pPackName[0] != '\0' ? pPackName : "default";
		const char *pResolvedCurrentPath = pCurrentPath != nullptr ? pCurrentPath : "";

		char aCurrentPackPrefix[IO_MAX_PATH_LENGTH];
		str_format(aCurrentPackPrefix, sizeof(aCurrentPackPrefix), "audio/%s/", pResolvedPackName);

		for(const std::string &Path : vPaths)
		{
			if(!str_endswith(Path.c_str(), ".wv"))
				continue;
			if(!vSeenPaths.insert(Path).second)
				continue;

			SCandidateEntry Entry;
			Entry.m_Path = Path;

			const char *pDisplayName = Path.c_str();
			if(str_startswith(pDisplayName, "audio/"))
				pDisplayName += str_length("audio/");
			Entry.m_DisplayName = pDisplayName;
			Entry.m_IsCurrentFile = pResolvedCurrentPath[0] != '\0' && str_comp(Path.c_str(), pResolvedCurrentPath) == 0;
			Entry.m_IsCurrentPackFile = str_startswith(Path.c_str(), aCurrentPackPrefix) != nullptr;
			vEntries.push_back(std::move(Entry));
		}

		std::sort(vEntries.begin(), vEntries.end(), [](const SCandidateEntry &Left, const SCandidateEntry &Right) {
			if(Left.m_IsCurrentFile != Right.m_IsCurrentFile)
				return Left.m_IsCurrentFile > Right.m_IsCurrentFile;
			if(Left.m_IsCurrentPackFile != Right.m_IsCurrentPackFile)
				return Left.m_IsCurrentPackFile > Right.m_IsCurrentPackFile;
			return str_comp_nocase(Left.m_DisplayName.c_str(), Right.m_DisplayName.c_str()) < 0;
		});

		return vEntries;
	}

	inline int FindCandidateEntryIndex(const std::vector<SCandidateEntry> &vEntries, const char *pPath)
	{
		if(pPath == nullptr || pPath[0] == '\0')
			return -1;

		for(int Index = 0; Index < (int)vEntries.size(); ++Index)
		{
			if(str_comp(vEntries[Index].m_Path.c_str(), pPath) == 0)
				return Index;
		}
		return -1;
	}

	using FBuildEntries = std::vector<SCandidateEntry> (*)(const std::vector<std::string> &, const char *, const char *);

	struct SPreparedCandidates
	{
		std::vector<SCandidateEntry> m_Entries;
		int m_SelectedIndex = -1;
	};

	inline SPreparedCandidates PrepareCandidates(const std::vector<std::string> &Paths, const std::vector<SCandidateEntry> &OldEntries,
		int OldSelection, const char *pPackName, const char *pCurrentPath, FBuildEntries pfnBuildEntries)
	{
		std::string SelectedPath;
		if(OldSelection >= 0 && static_cast<size_t>(OldSelection) < OldEntries.size())
			SelectedPath = OldEntries[OldSelection].m_Path;
		SPreparedCandidates Prepared;
		Prepared.m_Entries = pfnBuildEntries(Paths, pPackName, pCurrentPath);
		Prepared.m_SelectedIndex = FindCandidateEntryIndex(Prepared.m_Entries, pCurrentPath);
		if(Prepared.m_SelectedIndex < 0 && !SelectedPath.empty())
			Prepared.m_SelectedIndex = FindCandidateEntryIndex(Prepared.m_Entries, SelectedPath.c_str());
		if(Prepared.m_SelectedIndex < 0 && !Prepared.m_Entries.empty())
			Prepared.m_SelectedIndex = 0;
		return Prepared;
	}

	inline bool TryRebuildCandidates(const std::vector<std::string> &Paths, std::vector<SCandidateEntry> &Entries, int &Selection,
		const char *pPackName, const char *pCurrentPath, FBuildEntries pfnBuildEntries = BuildCandidateEntries) noexcept
	{
		try
		{
			auto Prepared = PrepareCandidates(Paths, Entries, Selection, pPackName, pCurrentPath, pfnBuildEntries);
			Entries.swap(Prepared.m_Entries);
			Selection = Prepared.m_SelectedIndex;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	// 扫描、路径和显示准备都在候选中进行；成功后只做无分配swap及索引赋值。
	template<typename TScan>
	bool TryRefreshCandidates(std::vector<std::string> &Paths, std::vector<SCandidateEntry> &Entries, int &Selection,
		const char *pPackName, const char *pCurrentPath, TScan &&Scan, FBuildEntries pfnBuildEntries = BuildCandidateEntries) noexcept
	{
		try
		{
			std::set<std::string> Scanned;
			if(!std::forward<TScan>(Scan)(Scanned))
				return false;
			std::vector<std::string> CandidatePaths(Scanned.begin(), Scanned.end());
			auto Prepared = PrepareCandidates(CandidatePaths, Entries, Selection, pPackName, pCurrentPath, pfnBuildEntries);
			Paths.swap(CandidatePaths);
			Entries.swap(Prepared.m_Entries);
			Selection = Prepared.m_SelectedIndex;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	struct SScanDirectory
	{
		const char *m_pScanRoot;
		const char *m_pOutputPrefix;
		std::string m_RelativePath;
	};

	// 枚举和实际存储根由调用方提供；遍历本身不递归进入目录回调。
	template<typename TStorage>
	class CCandidateScan
	{
		TStorage &m_Storage;
		std::vector<SScanDirectory> m_vPending;
		std::set<std::string> m_Paths;

		struct SContext
		{
			CCandidateScan &m_Scan;
			const SScanDirectory &m_Directory;
			std::unordered_set<std::string> m_SeenNames;
			size_t m_DirectoryLength = 0;
			bool m_Failed = false;
		};

		static int Visit(const CFsFileInfo *pInfo, int IsDir, int, void *pUser) noexcept
		{
			auto &Context = *static_cast<SContext *>(pUser);
			try
			{
				const char *pName = pInfo->m_pName;
				// 与TYPE_ALL一致：先确定存储优先级，再应用隐藏目录或路径过滤。
				if(!Context.m_SeenNames.emplace(pName).second)
					return 0;
				if(str_comp(pName, ".") == 0 || str_comp(pName, "..") == 0 || (IsDir && pName[0] == '.'))
					return 0;
				const auto &Directory = Context.m_Directory;
				const size_t RelativeLength = Directory.m_RelativePath.size() + (Directory.m_RelativePath.empty() ? 0 : 1) + str_length(pName);
				const size_t PhysicalLength = Context.m_DirectoryLength + 1 + str_length(pName);
				// 目录预留原生枚举的 /* 和终止符；文件也必须完整放入元数据路径缓冲。
				if(PhysicalLength + (IsDir ? 3 : 1) > IO_MAX_PATH_LENGTH ||
					str_length(Directory.m_pOutputPrefix) + 1 + RelativeLength + 1 > IO_MAX_PATH_LENGTH)
					return 0;
				std::string Relative = Directory.m_RelativePath;
				if(!Relative.empty())
					Relative += '/';
				Relative += pName;
				if(IsDir)
					Context.m_Scan.m_vPending.push_back({Directory.m_pScanRoot, Directory.m_pOutputPrefix, std::move(Relative)});
				else if(str_endswith(pName, ".wv"))
					Context.m_Scan.m_Paths.emplace(std::string(Directory.m_pOutputPrefix) + '/' + Relative);
				return 0;
			}
			catch(...)
			{
				// 不把本模块的分配异常越过原生枚举回调，整批结果仍不发布。
				Context.m_Failed = true;
				return 1;
			}
		}

	public:
		explicit CCandidateScan(TStorage &Storage) :
			m_Storage(Storage) {}

		template<typename TRoots>
		bool Scan(const TRoots &Roots, std::set<std::string> &Out) noexcept
		{
			try
			{
				m_vPending.clear();
				m_Paths.clear();
				for(const auto &Root : Roots)
					m_vPending.push_back({Root.m_pScanRoot, Root.m_pOutputPrefix, {}});
				while(!m_vPending.empty())
				{
					const SScanDirectory Directory = std::move(m_vPending.back());
					m_vPending.pop_back();
					std::string ScanPath = Directory.m_pScanRoot;
					if(!Directory.m_RelativePath.empty())
						ScanPath += '/' + Directory.m_RelativePath;
					SContext Context{*this, Directory, {}};
					for(int Type = IStorage::TYPE_SAVE; Type < m_Storage.NumPaths(); ++Type)
					{
						char aDirectory[IO_MAX_PATH_LENGTH] = "";
						m_Storage.GetCompletePath(Type, ScanPath.c_str(), aDirectory, sizeof(aDirectory));
						Context.m_DirectoryLength = str_length(aDirectory);
						if(!aDirectory[0] || Context.m_DirectoryLength + 3 > IO_MAX_PATH_LENGTH)
							continue;
						// 逐存储根枚举，回调内去重，避免TYPE_ALL在回调外分配去重节点。
						m_Storage.ListDirectoryInfo(Type, ScanPath.c_str(), Visit, &Context);
						if(Context.m_Failed)
							return false;
					}
				}
				Out.swap(m_Paths);
				return true;
			}
			catch(...)
			{
				return false;
			}
		}
	};
}

#endif
