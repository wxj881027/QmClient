#include "qm_legacy_config.h"

#include <base/system.h>

#include <algorithm>
#include <iterator>

namespace QmLegacyConfig
{
	namespace
	{
		struct SAlias
		{
			const char *m_pOldName;
			const char *m_pNewName;
		};

		constexpr SAlias s_aAliases[] = {
#define QM_LEGACY_CONFIG_ALIAS(Old, New) {Old, New},
#include "qm_legacy_config_aliases.h"
#undef QM_LEGACY_CONFIG_ALIAS
		};

		const SAlias *FindAlias(const char *pName, bool Canonical)
		{
			if(str_comp_nocase_num(pName, Canonical ? "qm_" : "tc_", 3) != 0)
				return nullptr;
			// 映射只替换 tc_ 前缀，旧名称与新名称的排序一致。
			const auto *pFound = std::lower_bound(std::begin(s_aAliases), std::end(s_aAliases), pName, [Canonical](const SAlias &Alias, const char *pValue) {
				return str_comp_nocase(Canonical ? Alias.m_pNewName : Alias.m_pOldName, pValue) < 0;
			});
			return pFound != std::end(s_aAliases) && str_comp_nocase(Canonical ? pFound->m_pNewName : pFound->m_pOldName, pName) == 0 ? pFound : nullptr;
		}

		struct SToken
		{
			size_t m_Start = 0;
			size_t m_End = 0;
			bool m_Quoted = false;
			bool m_Valid = false;
			std::string m_Value;
		};

		SToken ReadToken(std::string_view Text, size_t Start, bool Rest = false)
		{
			while(Start < Text.size() && str_isspace(Text[Start]))
				++Start;
			SToken Token;
			Token.m_Start = Token.m_End = Start;
			if(Start == Text.size())
				return Token;
			Token.m_Quoted = Text[Start] == '"';
			size_t Pos = Start + (Token.m_Quoted ? 1 : 0);
			for(; Pos < Text.size(); ++Pos)
			{
				const char Value = Text[Pos];
				if(Token.m_Quoted && Value == '"')
				{
					Token.m_End = Pos + 1;
					Token.m_Valid = true;
					return Token;
				}
				if(!Token.m_Quoted && !Rest && str_isspace(Value))
					break;
				if(Token.m_Quoted && Value == '\\' && Pos + 1 < Text.size() && (Text[Pos + 1] == '\\' || Text[Pos + 1] == '"'))
					++Pos;
				Token.m_Value += Text[Pos];
			}
			Token.m_End = Pos;
			Token.m_Valid = !Token.m_Quoted;
			return Token;
		}

		std::string EncodeToken(std::string_view Value, bool Quoted)
		{
			if(!Quoted)
				return std::string(Value);
			std::string Encoded = "\"";
			for(char Character : Value)
			{
				if(Character == '\\' || Character == '"')
					Encoded += '\\';
				Encoded += Character;
			}
			return Encoded + '"';
		}

		std::string MigrateCommands(std::string_view Text, int Depth);

		std::string MigrateSingleCommand(std::string_view Text, int Depth)
		{
			const SToken Command = ReadToken(Text, 0);
			if(!Command.m_Valid || Command.m_Quoted)
				return std::string(Text);
			const char *pCanonical = CanonicalName(Command.m_Value.c_str());
			std::string Result(Text);
			if(pCanonical != Command.m_Value.c_str())
			{
				Result.replace(Command.m_Start, Command.m_End - Command.m_Start, pCanonical);
				return Result;
			}

			const bool ConfigArgument = str_comp_nocase(Command.m_Value.c_str(), "toggle") == 0 ||
				str_comp_nocase(Command.m_Value.c_str(), "+toggle") == 0 ||
				str_comp_nocase(Command.m_Value.c_str(), "+toggle_restore") == 0 ||
				str_comp_nocase(Command.m_Value.c_str(), "reset") == 0;
			const SToken Argument = ReadToken(Text, Command.m_End);
			if(!Argument.m_Valid)
				return Result;
			if(ConfigArgument)
			{
				const char *pTarget = CanonicalName(Argument.m_Value.c_str());
				if(pTarget != Argument.m_Value.c_str())
					Result.replace(Argument.m_Start, Argument.m_End - Argument.m_Start, EncodeToken(pTarget, Argument.m_Quoted));
			}
			else if(str_comp_nocase(Command.m_Value.c_str(), "bind") == 0 && Depth < 32)
			{
				const SToken Nested = ReadToken(Text, Argument.m_End, true);
				if(Nested.m_Valid)
				{
					const std::string Migrated = MigrateCommands(Nested.m_Value, Depth + 1);
					if(Migrated != Nested.m_Value)
						Result.replace(Nested.m_Start, Nested.m_End - Nested.m_Start, EncodeToken(Migrated, Nested.m_Quoted));
				}
			}
			return Result;
		}

		std::string MigrateCommands(std::string_view Text, int Depth)
		{
			std::string Result;
			Result.reserve(Text.size());
			size_t Start = 0;
			bool Quoted = false;
			for(size_t Pos = 0; Pos < Text.size(); ++Pos)
			{
				if(Text[Pos] == '\\' && Pos + 1 < Text.size() && Text[Pos + 1] == '"')
					++Pos;
				else if(Text[Pos] == '"')
					Quoted = !Quoted;
				else if(!Quoted && Text[Pos] == '#')
				{
					Result += MigrateSingleCommand(Text.substr(Start, Pos - Start), Depth);
					Result += Text.substr(Pos);
					return Result;
				}
				else if(!Quoted && Text[Pos] == ';')
				{
					Result += MigrateSingleCommand(Text.substr(Start, Pos - Start), Depth);
					Result += ';';
					Start = Pos + 1;
				}
			}
			Result += MigrateSingleCommand(Text.substr(Start), Depth);
			return Result;
		}
	}

	const char *CanonicalName(const char *pName)
	{
		const SAlias *pAlias = FindAlias(pName, false);
		return pAlias != nullptr ? pAlias->m_pNewName : pName;
	}

	std::string MigrateBindCommand(std::string_view Command)
	{
		return MigrateCommands(Command, 0);
	}

	bool CWritePrecedence::ShouldExecute(const char *pName, bool HasValue, bool FromFile)
	{
		if(!HasValue)
			return true;
		if(const SAlias *pLegacy = FindAlias(pName, false))
			return !FromFile || m_CanonicalWrites.count(pLegacy->m_pNewName) == 0;
		if(const SAlias *pCanonical = FindAlias(pName, true))
			m_CanonicalWrites.insert(pCanonical->m_pNewName);
		return true;
	}
}
