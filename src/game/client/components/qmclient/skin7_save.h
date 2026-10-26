#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_SAVE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_SAVE_H

#include <base/str.h>
#include <base/system.h>

#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <generated/protocol7.h>

#include <array>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace qm_skin7_save
{
	struct SPart
	{
		// 同步保存期间借用配置或调用方的名称，不跨帧保留。
		const char *m_pFilename = nullptr;
		bool m_CustomColors = false;
		unsigned m_Color = 0;
	};
	using TParts = std::array<SPart, protocol7::NUM_SKINPARTS>;

	inline std::string Encode(const TParts &Parts)
	{
		static const char *const s_apNames[] = {"body", "marking", "decoration", "hands", "feet", "eyes"};
		static const char *const s_apComponents[] = {"hue", "sat", "lgt", "alp"};
		static_assert(std::size(s_apNames) == protocol7::NUM_SKINPARTS);
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Writer.WriteAttribute("skin");
		Writer.BeginObject();
		for(size_t i = 0; i < Parts.size(); ++i)
		{
			const auto &Part = Parts[i];
			if(!Part.m_pFilename || !Part.m_pFilename[0])
				continue;
			Writer.WriteAttribute(s_apNames[i]);
			Writer.BeginObject();
			Writer.WriteAttribute("filename");
			Writer.WriteStrValue(Part.m_pFilename);
			Writer.WriteAttribute("custom_colors");
			Writer.WriteBoolValue(Part.m_CustomColors);
			if(Part.m_CustomColors)
			{
				for(int c = 0; c < 3; ++c)
				{
					Writer.WriteAttribute(s_apComponents[c]);
					Writer.WriteIntValue((Part.m_Color >> ((2 - c) * 8)) & 0xff);
				}
				if(i == protocol7::SKINPART_MARKING)
				{
					Writer.WriteAttribute(s_apComponents[3]);
					Writer.WriteIntValue((Part.m_Color >> 24) & 0xff);
				}
			}
			Writer.EndObject();
		}
		Writer.EndObject();
		Writer.EndObject();
		return Writer.GetOutputString();
	}

	class CFileOperations
	{
	public:
		virtual ~CFileOperations() = default;
		virtual unsigned Write(IOHANDLE File, const void *pData, unsigned Size) { return io_write(File, pData, Size); }
		virtual int Sync(IOHANDLE File) { return io_sync(File); }
		virtual int Error(IOHANDLE File) { return io_error(File); }
		virtual int Close(IOHANDLE File) noexcept { return io_close(File); }
		// false或异常表示尚未替换，不允许先删除旧目标。
		virtual bool Publish(IStorage &Storage, const char *pTemporary, const char *pTarget)
		{
			return Storage.RenameFile(pTemporary, pTarget, IStorage::TYPE_SAVE, false);
		}
	};

	inline std::string TemporaryPath(const std::string &Target)
	{
		return Target + ".qm-save." + std::to_string(pid()) + ".tmp";
	}

	struct SFileCloser
	{
		CFileOperations *m_pOperations;
		void operator()(void *pFile) const noexcept { m_pOperations->Close(pFile); }
	};
	struct STemporaryFile
	{
		IStorage &m_Storage;
		const std::string &m_Path;
		bool m_Owned = false;
		~STemporaryFile()
		{
			if(m_Owned)
			{
				try
				{
					m_Storage.RemoveFile(m_Path.c_str(), IStorage::TYPE_SAVE);
				}
				catch(...)
				{
				}
			}
		}
	};

	inline bool Save(IStorage *pStorage, const char *pName, const TParts &Parts, CFileOperations *pOperations = nullptr) noexcept
	{
		try
		{
			if(!pStorage || !pName || !str_valid_filename(pName))
				return false;
			const std::string Target = "skins7/" + std::string(pName) + ".json";
			const std::string Temporary = TemporaryPath(Target);
			char aRoot[IO_MAX_PATH_LENGTH] = "";
			pStorage->GetCompletePath(IStorage::TYPE_SAVE, "", aRoot, sizeof(aRoot));
			if(!aRoot[0] || str_length(aRoot) + Temporary.size() + 1 >= IO_MAX_PATH_LENGTH ||
				pStorage->FileExists(Temporary.c_str(), IStorage::TYPE_SAVE) || pStorage->FolderExists(Temporary.c_str(), IStorage::TYPE_SAVE))
				return false;
			// 序列化先完成，不能让writer构造/扩容失败截断任何文件。
			const std::string Json = Encode(Parts);
			if(Json.size() > std::numeric_limits<unsigned>::max())
				return false;
			CFileOperations Default;
			auto &Operations = pOperations ? *pOperations : Default;
			STemporaryFile Candidate{*pStorage, Temporary};
			std::unique_ptr<void, SFileCloser> File(pStorage->OpenFile(Temporary.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE), {&Operations});
			if(!File)
				return false;
			Candidate.m_Owned = true;
			const bool Written = Operations.Write(File.get(), Json.data(), static_cast<unsigned>(Json.size())) == Json.size();
			const bool Synced = Operations.Sync(File.get()) == 0;
			const bool Clean = Operations.Error(File.get()) == 0;
			const bool Closed = Operations.Close(File.release()) == 0;
			if(!Written || !Synced || !Clean || !Closed || !Operations.Publish(*pStorage, Temporary.c_str(), Target.c_str()))
				return false;
			Candidate.m_Owned = false;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}

	template<typename TSkins, typename TPrepare>
	bool SaveAndPublish(IStorage *pStorage, const char *pName, const TParts &Parts, TSkins &Skins, TPrepare &&Prepare, CFileOperations *pOperations = nullptr) noexcept
	{
		try
		{
			TSkins Candidate = Skins;
			std::forward<TPrepare>(Prepare)(Candidate);
			static_assert(noexcept(Skins.swap(Candidate)));
			if(!Save(pStorage, pName, Parts, pOperations))
				return false;
			Skins.swap(Candidate);
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
}

#endif
