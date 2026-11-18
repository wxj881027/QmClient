#include "wheel_presets.h"

#include "bind_editor.h"

#include <base/str.h>

#include <algorithm>
#include <charconv>
#include <vector>

namespace qm_wheel_presets
{
	namespace
	{
		constexpr const char *s_aapIdentityCommands[2][NUM_IDENTITY_FIELDS] = {
			{"player_name", "player_clan", "player_skin", "player_use_custom_color", "player_color_body", "player_color_feet", "player_country", "player_default_eyes"},
			{"dummy_name", "dummy_clan", "dummy_skin", "dummy_use_custom_color", "dummy_color_body", "dummy_color_feet", "dummy_country", "dummy_default_eyes"},
		};
		constexpr const char *s_apAssetCommands[NUM_ASSETS] = {
			"cl_assets_entities", "cl_asset_game", "cl_asset_emoticons", "cl_asset_particles", "cl_asset_hud", "cl_asset_extras", "cl_asset_arrow", "cl_asset_gui_cursor", "cl_asset_strong_weak"};

		bool ValidText(const std::string &Text, size_t Capacity)
		{
			return Text.size() < Capacity && str_utf8_check(Text.c_str()) &&
			       std::none_of(Text.begin(), Text.end(), [](unsigned char Ch) { return Ch < 32 || Ch == 127; });
		}

		bool ValidField(int Field, const std::string &Value)
		{
			if(Field <= SKIN)
			{
				constexpr size_t s_aCapacities[] = {16, 12, 24};
				return ValidText(Value, s_aCapacities[Field]) && (Field != SKIN || !Value.empty());
			}
			int Number;
			const auto Parsed = std::from_chars(Value.data(), Value.data() + Value.size(), Number);
			if(Parsed.ec != std::errc() || Parsed.ptr != Value.data() + Value.size())
				return false;
			if(Field == CUSTOM_COLORS)
				return Number == 0 || Number == 1;
			if(Field == COUNTRY)
				return Number >= -999 && Number <= 1000;
			if(Field == EYES)
				return Number >= 0 && Number <= 5;
			return Number >= 0 && Number <= 0xffffff;
		}

		// 只识别本编辑器能完整表达的单参数命令；不改写用户的任意脚本。
		bool ReadAssignment(std::string_view Text, std::string &Name, std::string &Value)
		{
			const auto Space = Text.find(' ');
			if(Space == std::string_view::npos)
				return false;
			Name = Text.substr(0, Space);
			Text.remove_prefix(Space + 1);
			if(Text.empty())
				return false;
			Value.clear();
			if(Text.front() != '"')
			{
				if(Text.find_first_of(" \t\r\n\"\\") != std::string_view::npos)
					return false;
				Value = Text;
				return true;
			}
			for(size_t i = 1; i < Text.size(); ++i)
			{
				char Ch = Text[i];
				if(Ch == '"')
					return i + 1 == Text.size();
				if(Ch == '\\')
				{
					if(++i == Text.size() || (Text[i] != '\\' && Text[i] != '"'))
						return false;
					Ch = Text[i];
				}
				Value.push_back(Ch);
			}
			return false;
		}
	}

	const char *IdentityCommand(int Tee, int Field)
	{
		return Tee >= 0 && Tee < 2 && Field >= 0 && Field < NUM_IDENTITY_FIELDS ? s_aapIdentityCommands[Tee][Field] : nullptr;
	}

	const char *AssetCommand(int Index)
	{
		return Index >= 0 && Index < NUM_ASSETS ? s_apAssetCommands[Index] : nullptr;
	}

	bool BuildCommand(const SPreset &Preset, std::string &Command)
	{
		std::vector<std::string> vCommands;
		const auto Add = [&](const char *pName, const std::string &Value) {
			vCommands.emplace_back(std::string(pName) + " " + qm_bind_editor::QuoteArgument(Value));
		};
		const bool IncludeIdentity = Preset.m_Type == EType::NAME || Preset.m_Type == EType::IDENTITY || Preset.m_Type == EType::PROFILE;
		const bool IncludeAssets = Preset.m_Type == EType::ASSETS || Preset.m_Type == EType::PROFILE;
		const bool IncludeChat = Preset.m_Type == EType::CHAT || Preset.m_Type == EType::PROFILE;
		if(IncludeIdentity)
		{
			for(int Tee = 0; Tee < 2; ++Tee)
			{
				for(int Field = 0; Field < (Preset.m_Type == EType::NAME ? 1 : NUM_IDENTITY_FIELDS); ++Field)
				{
					const auto &Value = Preset.m_aTees[Tee].m_aValues[Field];
					if(!Value)
						continue;
					if(!ValidField(Field, *Value))
						return false;
					Add(IdentityCommand(Tee, Field), *Value);
				}
			}
		}
		if(IncludeAssets)
		{
			for(int Index = 0; Index < NUM_ASSETS; ++Index)
			{
				const auto &Asset = Preset.m_aAssets[Index];
				if(Asset.empty())
					continue;
				if(!ValidText(Asset, 50))
					return false;
				Add(AssetCommand(Index), Asset);
			}
		}
		if(IncludeChat && !Preset.m_Chat.empty())
		{
			if(!ValidText(Preset.m_Chat, 256))
				return false;
			Add(Preset.m_TeamChat ? "say_team" : "say", Preset.m_Chat);
		}
		const std::string Result = qm_bind_editor::JoinCommands(vCommands);
		if(Result.empty() || Result.size() >= COMMAND_CAPACITY)
			return false;
		Command = Result;
		return true;
	}

	std::optional<SPreset> ParseCommand(std::string_view Command)
	{
		if(Command.size() >= COMMAND_CAPACITY)
			return std::nullopt;
		const auto Commands = qm_bind_editor::SplitCommands(Command);
		if(!Commands.m_Complete || Commands.m_vCommands.empty())
			return std::nullopt;
		SPreset Result;
		bool HasIdentity = false;
		bool HasNonNameIdentity = false;
		bool HasChat = false;
		bool HasAssets = false;
		for(const auto &Entry : Commands.m_vCommands)
		{
			std::string Name, Value;
			if(!ReadAssignment(Entry, Name, Value))
				return std::nullopt;
			if(Name == "say" || Name == "say_team")
			{
				if(HasChat || Value.empty())
					return std::nullopt;
				HasChat = true;
				Result.m_Chat = Value;
				Result.m_TeamChat = Name == "say_team";
				continue;
			}
			bool Found = false;
			for(int Tee = 0; Tee < 2 && !Found; ++Tee)
			{
				for(int Field = 0; Field < NUM_IDENTITY_FIELDS; ++Field)
				{
					if(Name != IdentityCommand(Tee, Field))
						continue;
					if(Result.m_aTees[Tee].m_aValues[Field])
						return std::nullopt;
					HasIdentity = true;
					if(Field != NAME)
						HasNonNameIdentity = true;
					Result.m_aTees[Tee].m_aValues[Field] = Value;
					Found = true;
					break;
				}
			}
			for(int Index = 0; Index < NUM_ASSETS && !Found; ++Index)
			{
				if(Name != AssetCommand(Index))
					continue;
				if(!Result.m_aAssets[Index].empty() || Value.empty())
					return std::nullopt;
				HasAssets = true;
				Result.m_aAssets[Index] = Value;
				Found = true;
			}
			if(!Found)
				return std::nullopt;
		}
		const int CategoryCount = HasIdentity + HasChat + HasAssets;
		if(CategoryCount == 0)
			return std::nullopt;
		if(CategoryCount > 1)
			Result.m_Type = EType::PROFILE;
		else if(HasChat)
			Result.m_Type = EType::CHAT;
		else if(HasAssets)
			Result.m_Type = EType::ASSETS;
		else
			Result.m_Type = HasNonNameIdentity ? EType::IDENTITY : EType::NAME;
		std::string Rebuilt;
		if(!BuildCommand(Result, Rebuilt))
			return std::nullopt;
		return Result;
	}

} // namespace qm_wheel_presets
