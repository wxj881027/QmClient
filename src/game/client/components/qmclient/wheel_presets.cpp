#include "wheel_presets.h"

#include "bind_editor.h"

#include <base/str.h>

#include <algorithm>
#include <cctype>
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
		constexpr const char *s_apAssetCategoryNames[NUM_ASSETS] = {
			"Entities", "Game", "Emoticons", "Particles", "HUD", "Extras", "Arrow", "GUI cursor", "Strong / Weak"};

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
				return Number >= -999 && Number <= 999;
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

		bool IsWhitespace(char Character)
		{
			return std::isspace(static_cast<unsigned char>(Character)) != 0;
		}

		std::string_view Trim(std::string_view Text)
		{
			while(!Text.empty() && IsWhitespace(Text.front()))
				Text.remove_prefix(1);
			while(!Text.empty() && IsWhitespace(Text.back()))
				Text.remove_suffix(1);
			return Text;
		}

		// 旧脚本按行执行，而 SplitCommands 只认分号。逐行去掉注释并折成分号，
		// 保留引号里的 #，避免把战队名或语录误当注释。
		std::string NormalizeLegacyConfig(std::string_view Text)
		{
			std::string Result;
			Result.reserve(Text.size());
			std::string Line;
			Line.reserve(256);
			bool InQuotes = false;
			bool EscapeNext = false;
			bool InComment = false;
			const auto FinishLine = [&]() {
				const std::string_view Trimmed = Trim(Line);
				if(!Trimmed.empty())
				{
					Result.append(Trimmed);
					Result.push_back(';');
				}
				Line.clear();
				InQuotes = false;
				EscapeNext = false;
				InComment = false;
			};
			for(size_t Index = 0; Index < Text.size(); ++Index)
			{
				const char Character = Text[Index];
				if(Character == '\r')
					continue;
				if(Character == '\n')
				{
					FinishLine();
					continue;
				}
				if(InComment)
					continue;
				if(EscapeNext)
				{
					Line.push_back(Character);
					EscapeNext = false;
					continue;
				}
				if(Character == '\\')
				{
					Line.push_back(Character);
					EscapeNext = true;
					continue;
				}
				if(Character == '"')
				{
					InQuotes = !InQuotes;
					Line.push_back(Character);
					continue;
				}
				if(Character == '#' && !InQuotes)
				{
					InComment = true;
					continue;
				}
				Line.push_back(Character);
			}
			FinishLine();
			return Result;
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

	const char *AssetCategoryName(int Index)
	{
		return Index >= 0 && Index < NUM_ASSETS ? s_apAssetCategoryNames[Index] : nullptr;
	}

	qm_bind_menu::EMenu MenuForType(EType Type)
	{
		switch(Type)
		{
		case EType::NAME:
		case EType::IDENTITY: return qm_bind_menu::EMenu::IDENTITY;
		case EType::CHAT: return qm_bind_menu::EMenu::CHAT;
		case EType::ASSETS: return qm_bind_menu::EMenu::ASSETS;
		default: return qm_bind_menu::EMenu::CUSTOM;
		}
	}

	std::optional<qm_bind_menu::EMenu> MenuForCommand(std::string_view Command)
	{
		const auto Preset = ParseCommand(Command);
		if(!Preset)
			return std::nullopt;
		return MenuForType(Preset->m_Type);
	}

	bool BuildCommand(const SPreset &Preset, std::string &Command)
	{
		std::vector<std::string> vCommands;
		const auto Add = [&](const char *pName, const std::string &Value) {
			vCommands.emplace_back(std::string(pName) + " " + qm_bind_editor::QuoteArgument(Value));
		};
		const bool IncludeIdentity = Preset.m_Type == EType::NAME || Preset.m_Type == EType::IDENTITY;
		const bool IncludeAssets = Preset.m_Type == EType::ASSETS;
		const bool IncludeChat = Preset.m_Type == EType::CHAT;
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
		if(CategoryCount != 1)
			return std::nullopt;
		if(HasChat)
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

	std::optional<SPreset> ParseLegacyConfig(std::string_view Text)
	{
		if(Text.size() >= COMMAND_CAPACITY * 16)
			return std::nullopt;
		const std::string Normalized = NormalizeLegacyConfig(Text);
		const auto Commands = qm_bind_editor::SplitCommands(Normalized);
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
				continue;
			if(Name == "say" || Name == "say_team")
			{
				if(HasChat || Value.empty() || !ValidText(Value, 256))
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
					if(Result.m_aTees[Tee].m_aValues[Field] || !ValidField(Field, Value))
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
				if(!Result.m_aAssets[Index].empty() || Value.empty() || !ValidText(Value, 50))
					return std::nullopt;
				HasAssets = true;
				Result.m_aAssets[Index] = Value;
				Found = true;
			}
		}

		if(!HasIdentity && !HasChat && !HasAssets)
			return std::nullopt;
		// 旧脚本可能把身份、资源和语录写在同一个文件里；预览优先展示身份，
		// 但执行时仍使用原 exec 命令，不会丢掉另外两类副作用。
		if(HasIdentity)
			Result.m_Type = HasNonNameIdentity ? EType::IDENTITY : EType::NAME;
		else if(HasChat)
			Result.m_Type = EType::CHAT;
		else
			Result.m_Type = EType::ASSETS;
		return Result;
	}

	std::optional<std::string> ExecPath(std::string_view Command)
	{
		if(Command.size() >= COMMAND_CAPACITY)
			return std::nullopt;
		const auto Commands = qm_bind_editor::SplitCommands(Command);
		if(!Commands.m_Complete || Commands.m_vCommands.size() != 1)
			return std::nullopt;

		std::string_view Text = Trim(Commands.m_vCommands.front());
		if(Text.size() <= 4 || Text.substr(0, 4) != "exec" || !IsWhitespace(Text[4]))
			return std::nullopt;
		Text = Trim(Text.substr(4));
		if(Text.empty())
			return std::nullopt;

		// 旧版配置里的 exec 路径通常直接使用 Windows 单反斜杠。这里不复用
		// ReadAssignment 的成对转义规则，否则这些条目只能落进公共自定义菜单。
		if(Text.front() == '"')
		{
			if(Text.size() < 3 || Text.back() != '"')
				return std::nullopt;
			Text.remove_prefix(1);
			Text.remove_suffix(1);
			if(Text.empty() || Text.find('"') != std::string_view::npos)
				return std::nullopt;
		}
		else if(Text.find_first_of(" \t\r\n;#\"") != std::string_view::npos)
			return std::nullopt;
		if(Text.find_first_of("\r\n") != std::string_view::npos || Text.find('\0') != std::string_view::npos)
			return std::nullopt;
		return std::string(Text);
	}

} // namespace qm_wheel_presets
