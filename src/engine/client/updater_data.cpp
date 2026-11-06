#include "updater_data.h"

#include <base/fs.h>
#include <base/str.h>
#include <base/system.h>

#include <engine/shared/json.h>
#include <engine/storage.h>

#include <cstring>
#include <memory>
#include <unordered_set>

namespace
{
	constexpr std::string_view CLIENT_FILE = "DDNet.exe";
	constexpr std::string_view CLIENT_URL = "https://github.com/wxj881027/Q1menG_Client/releases/latest/download/DDNet.exe";
	constexpr std::string_view BASE_URL = "https://raw.githubusercontent.com/wxj881027/Q1menG_Client/master/";

	bool IsUnreserved(unsigned char Character)
	{
		return (Character >= 'A' && Character <= 'Z') || (Character >= 'a' && Character <= 'z') ||
		       (Character >= '0' && Character <= '9') || Character == '-' || Character == '_' || Character == '.' || Character == '~' || Character == '/';
	}

	bool Text(const json_value *pValue, std::string_view &Out)
	{
		if(!pValue || pValue->type != json_string || !pValue->u.string.ptr ||
			std::memchr(pValue->u.string.ptr, '\0', pValue->u.string.length) || !str_utf8_check(pValue->u.string.ptr))
			return false;
		Out = {pValue->u.string.ptr, pValue->u.string.length};
		return true;
	}

	// 完整成员名匹配；重复的已消费字段不能选择其中一个继续安装。
	bool Field(const json_value *pObject, std::string_view Name, const json_value *&pOut)
	{
		pOut = nullptr;
		for(unsigned Index = 0; Index < pObject->u.object.length; ++Index)
		{
			const auto &Member = pObject->u.object.values[Index];
			if(std::string_view(Member.name, Member.name_length) != Name)
				continue;
			if(pOut)
				return false;
			pOut = Member.value;
		}
		return true;
	}
}

bool QmLegacyUpdater::IsAllowedPath(std::string_view Path) noexcept
{
	try
	{
		if(Path.empty() || Path.size() >= IO_MAX_PATH_LENGTH || Path.find('\0') != std::string_view::npos || Path.find("..") != std::string_view::npos)
			return false;
		char aPath[IO_MAX_PATH_LENGTH];
		std::memcpy(aPath, Path.data(), Path.size());
		aPath[Path.size()] = '\0';
		if(!str_utf8_check(aPath) || !fs_is_relative_path(aPath))
			return false;
		for(size_t Start = 0; Start < Path.size();)
		{
			const size_t End = Path.find('/', Start);
			const size_t Length = (End == std::string_view::npos ? Path.size() : End) - Start;
			if(Length == 0)
				return false;
			char aSegment[IO_MAX_PATH_LENGTH];
			std::memcpy(aSegment, Path.data() + Start, Length);
			aSegment[Length] = '\0';
			if(!str_valid_filename(aSegment))
				return false;
			if(End == std::string_view::npos)
				return true;
			Start = End + 1;
		}
		return false;
	}
	catch(...)
	{
		return false;
	}
}

bool QmLegacyUpdater::ResolveBinaryPath(std::string_view Root, std::string_view Relative, char *pOut, size_t OutSize) noexcept
{
	if(OutSize == 0 || Root.find('\0') != std::string_view::npos || !IsAllowedPath(Relative) || Root.size() >= OutSize || Relative.size() >= OutSize - Root.size())
		return false;
	if(!Root.empty())
		std::memcpy(pOut, Root.data(), Root.size());
	std::memcpy(pOut + Root.size(), Relative.data(), Relative.size());
	pOut[Root.size() + Relative.size()] = '\0';
	return true;
}

bool QmLegacyUpdater::PrepareRequestPaths(std::string_view File, std::string_view Destination, std::string_view BinaryRoot, size_t TempSuffixLength, SRequestPaths &Out) noexcept
{
	if(Destination.empty())
		Destination = File;
	if(!IsAllowedPath(File) || !IsAllowedPath(Destination))
		return false;
	SRequestPaths Prepared;
	constexpr std::string_view PREFIX = "update/";
	if(Destination.size() >= sizeof(Prepared.m_aDestination) - PREFIX.size())
		return false;
	std::memcpy(Prepared.m_aDestination, PREFIX.data(), PREFIX.size());
	std::memcpy(Prepared.m_aDestination + PREFIX.size(), Destination.data(), Destination.size());
	const size_t DestinationLength = PREFIX.size() + Destination.size();
	Prepared.m_aDestination[DestinationLength] = '\0';
	char aAbsolute[IO_MAX_PATH_LENGTH];
	if(!ResolveBinaryPath(BinaryRoot, Prepared.m_aDestination, aAbsolute, sizeof(aAbsolute)) ||
		TempSuffixLength >= sizeof(aAbsolute) || std::strlen(aAbsolute) >= sizeof(aAbsolute) - TempSuffixLength)
		return false;
	if(File == CLIENT_FILE)
	{
		static_assert(CLIENT_URL.size() < sizeof(Prepared.m_aUrl));
		std::memcpy(Prepared.m_aUrl, CLIENT_URL.data(), CLIENT_URL.size());
	}
	else
	{
		std::memcpy(Prepared.m_aUrl, BASE_URL.data(), BASE_URL.size());
		size_t Position = BASE_URL.size();
		constexpr char HEX[] = "0123456789ABCDEF";
		for(unsigned char Character : File)
		{
			const size_t Bytes = IsUnreserved(Character) ? 1 : 3;
			if(Bytes >= sizeof(Prepared.m_aUrl) - Position)
				return false;
			if(Bytes == 1)
				Prepared.m_aUrl[Position++] = static_cast<char>(Character);
			else
			{
				Prepared.m_aUrl[Position++] = '%';
				Prepared.m_aUrl[Position++] = HEX[Character >> 4];
				Prepared.m_aUrl[Position++] = HEX[Character & 15];
			}
		}
		Prepared.m_aUrl[Position] = '\0';
	}
	Out = Prepared;
	return true;
}

bool QmLegacyUpdater::ParseManifest(std::string_view Content, std::string_view CurrentVersion, SUpdatePlan &Out) noexcept
{
	try
	{
		if(Content.empty() || Content.size() > MAX_MANIFEST_BYTES || Content.find('\0') != std::string_view::npos)
			return false;
		json_settings Settings{};
		Settings.max_memory = MAX_MANIFEST_JSON_MEMORY;
		std::unique_ptr<json_value, decltype(&json_value_free)> Root(JsonParseEx(&Settings, Content.data(), Content.size(), nullptr), json_value_free);
		if(!Root || Root->type != json_array)
			return false;
		SUpdatePlan Prepared;
		std::unordered_set<std::string> Seen;
		size_t Operations = 0;
		for(unsigned Index = 0; Index < Root->u.array.length; ++Index)
		{
			const json_value *pVersion = Root->u.array.values[Index];
			if(pVersion->type != json_object)
				return false;
			const json_value *pValue;
			std::string_view Version;
			if(!Field(pVersion, "version", pValue) || !Text(pValue, Version) || Version.empty())
				return false;
			if(Version == CurrentVersion)
				break;
			for(const auto &[Name, pFlag] : {std::pair{"client", &Prepared.m_ClientUpdate}, std::pair{"server", &Prepared.m_ServerUpdate}})
			{
				if(!Field(pVersion, Name, pValue) || (pValue && pValue->type != json_boolean))
					return false;
				if(pValue && pValue->u.boolean)
					*pFlag = true;
			}
			for(const auto &[Name, Download] : {std::pair{"download", true}, std::pair{"remove", false}})
			{
				if(!Field(pVersion, Name, pValue) || (pValue && pValue->type != json_array))
					return false;
				if(!pValue)
					continue;
				for(unsigned Item = 0; Item < pValue->u.array.length; ++Item)
				{
					std::string_view Path;
					if(++Operations > MAX_MANIFEST_OPERATIONS || !Text(pValue->u.array.values[Item], Path) || !IsAllowedPath(Path))
						return false;
					if(Seen.emplace(Path).second)
						Prepared.m_FileJobs.emplace_front(Path, Download);
				}
			}
		}
		Out.m_FileJobs.swap(Prepared.m_FileJobs);
		Out.m_ClientUpdate = Prepared.m_ClientUpdate;
		Out.m_ServerUpdate = Prepared.m_ServerUpdate;
		return true;
	}
	catch(...)
	{
		return false;
	}
}

bool QmLegacyUpdater::LoadManifest(IStorage *pStorage, const char *pAbsolutePath, std::string_view CurrentVersion, SUpdatePlan &Out) noexcept
{
	try
	{
		if(!pStorage || !pAbsolutePath)
			return false;
		std::unique_ptr<void, decltype(&io_close)> File(pStorage->OpenFile(pAbsolutePath, IOFLAG_READ, IStorage::TYPE_ABSOLUTE), io_close);
		if(!File)
			return false;
		const int64_t Length = io_length(File.get());
		if(Length < 0 || static_cast<uint64_t>(Length) > MAX_MANIFEST_BYTES)
			return false;
		std::string Content(static_cast<size_t>(Length), '\0');
		const unsigned Read = io_read(File.get(), Content.data(), Content.size());
		char Extra;
		if(Read != Content.size() || io_read(File.get(), &Extra, 1) != 0 || io_error(File.get()))
			return false;
		if(io_close(File.release()) != 0)
			return false;
		return ParseManifest(Content, CurrentVersion, Out);
	}
	catch(...)
	{
		return false;
	}
}
