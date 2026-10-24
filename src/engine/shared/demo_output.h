#ifndef ENGINE_SHARED_DEMO_OUTPUT_H
#define ENGINE_SHARED_DEMO_OUTPUT_H

#include <base/fs.h>
#include <base/log.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/storage.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

namespace demo_output
{
	// 保留保存根和显式绝对路径的现有语义，不让临时路径绕过相对路径校验。
	inline bool Resolve(IStorage &Storage, const char *pPath, std::string &Absolute)
	{
		if(!pPath || !pPath[0] || str_length(pPath) >= IO_MAX_PATH_LENGTH || !str_valid_filename(fs_filename(pPath)))
			return false;
		const bool Relative = fs_is_relative_path(pPath);
		if(Relative && (pPath[0] == '/' || pPath[0] == '\\' || str_find(pPath, "../") || str_find(pPath, "..\\")))
			return false;
		char aRoot[IO_MAX_PATH_LENGTH];
		if(Relative)
		{
			Storage.GetCompletePath(IStorage::TYPE_SAVE, "", aRoot, sizeof(aRoot));
			if(static_cast<size_t>(str_length(aRoot)) + str_length(pPath) + 1 >= IO_MAX_PATH_LENGTH)
				return false;
		}
		char aFull[IO_MAX_PATH_LENGTH];
		Storage.GetCompletePath(Relative ? IStorage::TYPE_SAVE : IStorage::TYPE_ABSOLUTE, pPath, aFull, sizeof(aFull));
		std::error_code Error;
		const auto Full = std::filesystem::absolute(std::filesystem::u8path(aFull), Error).generic_u8string();
		if(Error || Full.empty() || Full.size() >= IO_MAX_PATH_LENGTH)
			return false;
		Absolute.assign(reinterpret_cast<const char *>(Full.data()), Full.size());
		return true;
	}

	inline bool ValidDestination(IStorage &Storage, const char *pPath)
	{
		std::string Absolute;
		if(!Resolve(Storage, pPath, Absolute))
			return false;
		std::error_code Error;
		const auto Status = std::filesystem::status(std::filesystem::u8path(Absolute), Error);
		return Error == std::errc::no_such_file_or_directory ||
		       (!Error && (!std::filesystem::exists(Status) || std::filesystem::is_regular_file(Status)));
	}

	class COperations
	{
	public:
		enum class ECreate
		{
			CREATED,
			EXISTS,
			FAILED,
		};

		virtual ~COperations() = default;
		virtual ECreate CreateDirectory(const char *pPath)
		{
			std::error_code Error;
			if(std::filesystem::create_directory(std::filesystem::u8path(pPath), Error))
				return ECreate::CREATED;
			return !Error || Error == std::errc::file_exists ? ECreate::EXISTS : ECreate::FAILED;
		}
		virtual bool Publish(const char *pTemporary, const char *pDestination)
		{
			// 同目录替换；Windows 禁止失败后先删除目标再重试。
			return fs_rename(pTemporary, pDestination, false) == 0;
		}
		virtual bool Remove(const char *pPath)
		{
			std::error_code Error;
			std::filesystem::remove(std::filesystem::u8path(pPath), Error);
			return !Error;
		}
	};

	class CTransaction
	{
		inline static std::atomic<uint64_t> ms_Sequence{0};
		COperations *m_pOperations;
		char m_aDestination[IO_MAX_PATH_LENGTH] = "";
		char m_aDirectory[IO_MAX_PATH_LENGTH] = "";
		char m_aTemporary[IO_MAX_PATH_LENGTH] = "";
		bool m_Owned = false;
		bool m_Published = false;

		COperations &Operations()
		{
			static COperations s_Operations;
			return m_pOperations ? *m_pOperations : s_Operations;
		}

	public:
		explicit CTransaction(COperations *pOperations = nullptr) :
			m_pOperations(pOperations) {}
		CTransaction(const CTransaction &) = delete;
		CTransaction &operator=(const CTransaction &) = delete;
		~CTransaction() noexcept
		{
			if(!m_Owned)
				return;
			bool Cleaned = true;
			try
			{
				if(!m_Published)
					Cleaned = Operations().Remove(m_aTemporary);
			}
			catch(...)
			{
				Cleaned = false;
			}
			try
			{
				Cleaned = Operations().Remove(m_aDirectory) && Cleaned;
			}
			catch(...)
			{
				Cleaned = false;
			}
			if(!Cleaned)
			{
				try
				{
					log_warn("demo", "Could not remove a temporary demo output");
				}
				catch(...)
				{
				}
			}
		}

		bool Prepare(IStorage &Storage, const char *pDestination)
		{
			if(m_Owned || !ValidDestination(Storage, pDestination))
				return false;
			std::string Destination;
			if(!Resolve(Storage, pDestination, Destination))
				return false;
			str_copy(m_aDestination, Destination.c_str());
			const auto LastSlash = Destination.find_last_of("/\\");
			const int PrefixLength = LastSlash == std::string::npos ? 0 : static_cast<int>(LastSlash + 1);
			for(int Attempt = 0; Attempt < 64; ++Attempt)
			{
				char aName[64];
				str_format(aName, sizeof(aName), ".qm-demo-%d-%llx", pid(), static_cast<unsigned long long>(ms_Sequence.fetch_add(1, std::memory_order_relaxed)));
				if(static_cast<size_t>(PrefixLength) + str_length(aName) + sizeof("/output.demo") > sizeof(m_aTemporary))
					return false;
				str_format(m_aDirectory, sizeof(m_aDirectory), "%.*s%s", PrefixLength, Destination.c_str(), aName);
				str_format(m_aTemporary, sizeof(m_aTemporary), "%s/output.demo", m_aDirectory);
				const auto Created = Operations().CreateDirectory(m_aDirectory);
				if(Created == COperations::ECreate::CREATED)
				{
					m_Owned = true;
					return true;
				}
				if(Created == COperations::ECreate::FAILED)
					return false;
			}
			return false;
		}

		const char *Filename() const { return m_aTemporary; }
		bool Publish()
		{
			if(!m_Owned || m_Published || !Operations().Publish(m_aTemporary, m_aDestination))
				return false;
			m_Published = true;
			return true;
		}
	};
}

#endif
