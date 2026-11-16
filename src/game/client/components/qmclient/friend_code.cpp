#include "friend_code.h"

#include <base/system.h>

#include <zlib.h>

#include <algorithm>
#include <set>
#include <utility>

namespace qm_friend_code
{
	namespace
	{
		constexpr std::string_view PREFIX = "QMF1:";

		// 定长计数与长度前缀均使用字节编码，不依赖平台结构体布局。
		void WriteCount(std::string &Bytes, size_t Value)
		{
			Bytes.push_back(static_cast<char>(Value & 0xff));
			Bytes.push_back(static_cast<char>((Value >> 8) & 0xff));
		}

		void WriteText(std::string &Bytes, std::string_view Text)
		{
			Bytes.push_back(static_cast<char>(Text.size()));
			Bytes.append(Text);
		}

		class CReader
		{
			std::string_view m_Bytes;

		public:
			explicit CReader(std::string_view Bytes) :
				m_Bytes(Bytes) {}
			bool Byte(unsigned &Value)
			{
				if(m_Bytes.empty())
					return false;
				Value = static_cast<unsigned char>(m_Bytes.front());
				m_Bytes.remove_prefix(1);
				return true;
			}
			bool Count(unsigned &Value)
			{
				unsigned Low, High;
				if(!Byte(Low) || !Byte(High))
					return false;
				Value = Low | (High << 8);
				return true;
			}
			bool Text(std::string &Value, size_t Capacity)
			{
				unsigned Length;
				if(!Byte(Length) || Length >= Capacity || Length > m_Bytes.size())
					return false;
				Value.assign(m_Bytes.substr(0, Length));
				m_Bytes.remove_prefix(Length);
				return Value.find('\0') == std::string::npos && str_utf8_check(Value.c_str());
			}
			template<size_t N>
			bool Text(char (&aText)[N])
			{
				std::string Value;
				if(!Text(Value, N))
					return false;
				str_copy(aText, Value.c_str());
				return true;
			}
			bool Done() const { return m_Bytes.empty(); }
		};
	}

	CFriendList Capture(const IFriends &Friends)
	{
		CFriendList Result;
		for(int Index = 0; Index < Friends.NumCategories(); ++Index)
			Result.m_vCategories.emplace_back(Friends.GetCategory(Index));
		for(int Index = 0; Index < Friends.NumFriends(); ++Index)
			Result.m_vFriends.push_back(*Friends.GetFriend(Index));
		return Result;
	}

	bool Encode(const CFriendList &List, std::string &Code)
	{
		if(!IFriends::ValidateList(List))
			return false;
		std::string Raw;
		WriteCount(Raw, List.m_vCategories.size());
		for(const auto &Category : List.m_vCategories)
			WriteText(Raw, Category);
		WriteCount(Raw, List.m_vFriends.size());
		for(const auto &Friend : List.m_vFriends)
		{
			WriteText(Raw, Friend.m_aName);
			WriteText(Raw, Friend.m_aClan);
			const auto Category = std::find(List.m_vCategories.begin(), List.m_vCategories.end(), Friend.m_aCategory);
			Raw.push_back(static_cast<char>(Category - List.m_vCategories.begin()));
			WriteText(Raw, Friend.m_aNote);
			Raw.push_back(Friend.m_Pinned ? 1 : 0);
		}
		uLongf Size = compressBound(static_cast<uLong>(Raw.size()));
		std::vector<unsigned char> vCompressed(Size);
		if(compress2(vCompressed.data(), &Size, reinterpret_cast<const Bytef *>(Raw.data()), static_cast<uLong>(Raw.size()), Z_BEST_COMPRESSION) != Z_OK)
			return false;
		std::vector<char> vBase64((Size + 2) / 3 * 4 + 1);
		str_base64(vBase64.data(), static_cast<int>(vBase64.size()), vCompressed.data(), static_cast<int>(Size));
		Code = std::string(PREFIX) + vBase64.data();
		return true;
	}

	bool Decode(std::string_view Code, CFriendList &List)
	{
		if(Code.size() > MAX_CODE_BYTES)
			return false;
		std::string Compact;
		Compact.reserve(Code.size());
		for(char Ch : Code)
		{
			if(Ch == '\0')
				return false;
			if(Ch != ' ' && Ch != '\r' && Ch != '\n' && Ch != '\t')
				Compact.push_back(Ch);
		}
		if(!std::string_view(Compact).starts_with(PREFIX))
			return false;
		std::vector<unsigned char> vCompressed(Compact.size());
		const int Size = str_base64_decode(vCompressed.data(), static_cast<int>(vCompressed.size()), Compact.c_str() + PREFIX.size());
		if(Size <= 0)
			return false;
		std::string Raw(MAX_RAW_BYTES, '\0');
		z_stream Stream{};
		Stream.next_in = vCompressed.data();
		Stream.avail_in = Size;
		Stream.next_out = reinterpret_cast<Bytef *>(Raw.data());
		Stream.avail_out = static_cast<uInt>(Raw.size());
		if(inflateInit(&Stream) != Z_OK)
			return false;
		const int Result = inflate(&Stream, Z_FINISH);
		const bool Complete = Result == Z_STREAM_END && Stream.avail_in == 0;
		Raw.resize(Stream.total_out);
		inflateEnd(&Stream);
		if(!Complete)
			return false;

		CReader Reader(Raw);
		CFriendList Parsed;
		unsigned Count;
		if(!Reader.Count(Count) || Count > IFriends::MAX_FRIEND_CATEGORIES)
			return false;
		Parsed.m_vCategories.resize(Count);
		for(auto &Category : Parsed.m_vCategories)
			if(!Reader.Text(Category, IFriends::MAX_FRIEND_CATEGORY_LENGTH))
				return false;
		if(!Reader.Count(Count) || Count > IFriends::MAX_FRIENDS)
			return false;
		Parsed.m_vFriends.resize(Count);
		for(auto &Friend : Parsed.m_vFriends)
		{
			unsigned Category, Pinned;
			if(!Reader.Text(Friend.m_aName) || !Reader.Text(Friend.m_aClan) || !Reader.Byte(Category) || Category >= Parsed.m_vCategories.size() || !Reader.Text(Friend.m_aNote) || !Reader.Byte(Pinned) || Pinned > 1)
				return false;
			str_copy(Friend.m_aCategory, Parsed.m_vCategories[Category].c_str());
			Friend.m_Pinned = Pinned != 0;
		}
		if(!Reader.Done() || !IFriends::ValidateList(Parsed))
			return false;
		List = std::move(Parsed);
		return true;
	}

	bool Import(IFriends &Friends, const CFriendList &Incoming, EImportMode Mode)
	{
		if(!IFriends::ValidateList(Incoming))
			return false;
		if(Mode == EImportMode::REPLACE)
			return Friends.ReplaceAll(Incoming);
		if(Mode != EImportMode::MERGE)
			return false;
		CFriendList Merged = Capture(Friends);
		std::vector<std::string> vCategoryNames;
		for(const auto &Category : Incoming.m_vCategories)
		{
			const auto Existing = std::find_if(Merged.m_vCategories.begin(), Merged.m_vCategories.end(), [&](const auto &Name) { return str_comp_nocase(Name.c_str(), Category.c_str()) == 0; });
			if(Existing == Merged.m_vCategories.end())
			{
				Merged.m_vCategories.push_back(Category);
				vCategoryNames.push_back(Category);
			}
			else
				vCategoryNames.push_back(*Existing);
		}
		std::set<std::pair<std::string, std::string>> Identities;
		for(const auto &Friend : Merged.m_vFriends)
			Identities.emplace(Friend.m_aName, Friend.m_aClan);
		for(auto Friend : Incoming.m_vFriends)
		{
			if(!Identities.emplace(Friend.m_aName, Friend.m_aClan).second)
				continue;
			const auto Category = std::find(Incoming.m_vCategories.begin(), Incoming.m_vCategories.end(), Friend.m_aCategory);
			str_copy(Friend.m_aCategory, vCategoryNames[Category - Incoming.m_vCategories.begin()].c_str());
			Merged.m_vFriends.push_back(Friend);
		}
		return Friends.ReplaceAll(Merged);
	}
}
