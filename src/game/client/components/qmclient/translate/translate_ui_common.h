// 翻译设置两处 UI 共用的选项和配置提交规则。
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_UI_COMMON_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_UI_COMMON_H

#include <base/str.h>

#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace NTranslateUi
{
	constexpr int LANGUAGE_COUNT = 10;
	constexpr int SOURCE_LANGUAGE_COUNT = LANGUAGE_COUNT + 1;
	constexpr int BACKEND_COUNT = 5;

	enum EBackendIndex
	{
		BACKEND_LLM = 0,
		BACKEND_TENCENT_CLOUD,
		BACKEND_LIBRETRANSLATE,
		BACKEND_FTAPI,
		BACKEND_MYMEMORY,
	};

	inline const std::array<const char *, LANGUAGE_COUNT> &LanguageNames()
	{
		static const std::array<const char *, LANGUAGE_COUNT> s_Names = {
			"中文", "English", "日本語", "한국어", "繁體中文", "Русский", "Deutsch", "Français", "Español", "Português"};
		return s_Names;
	}

	inline const std::array<const char *, LANGUAGE_COUNT> &LanguageCodes()
	{
		static const std::array<const char *, LANGUAGE_COUNT> s_Codes = {
			"zh", "en", "ja", "ko", "zh-TW", "ru", "de", "fr", "es", "pt"};
		return s_Codes;
	}

	inline std::array<const char *, SOURCE_LANGUAGE_COUNT> SourceLanguageNames()
	{
		std::array<const char *, SOURCE_LANGUAGE_COUNT> Names = {Localize("Auto")};
		for(int i = 0; i < LANGUAGE_COUNT; ++i)
			Names[i + 1] = LanguageNames()[i];
		return Names;
	}

	inline std::array<const char *, SOURCE_LANGUAGE_COUNT> SourceLanguageCodes()
	{
		std::array<const char *, SOURCE_LANGUAGE_COUNT> Codes = {"auto"};
		for(int i = 0; i < LANGUAGE_COUNT; ++i)
			Codes[i + 1] = LanguageCodes()[i];
		return Codes;
	}

	inline std::array<const char *, BACKEND_COUNT> BackendNames()
	{
		return {Localize("LLM API"), Localize("Tencent Cloud"), "LibreTranslate", "FTAPI", Localize("MyMemory (free)")};
	}

	inline const std::array<const char *, BACKEND_COUNT> &BackendCodes()
	{
		static const std::array<const char *, BACKEND_COUNT> s_Codes = {"llm", "tencentcloud", "libretranslate", "ftapi", "mymemory"};
		return s_Codes;
	}

	inline int FindOptionIndex(const char *pValue, const char *const *ppCodes, const size_t Count)
	{
		if(pValue == nullptr || ppCodes == nullptr)
			return -1;
		for(size_t i = 0; i < Count; ++i)
		{
			if(str_comp(pValue, ppCodes[i]) == 0)
				return (int)i;
		}
		return -1;
	}

	inline int FindLanguageIndex(const char *pValue)
	{
		return FindOptionIndex(pValue, LanguageCodes().data(), LanguageCodes().size());
	}

	inline int FindSourceLanguageIndex(const char *pValue)
	{
		const auto Codes = SourceLanguageCodes();
		return FindOptionIndex(pValue, Codes.data(), Codes.size());
	}

	inline int FindBackendIndex(const char *pValue)
	{
		if(pValue == nullptr)
			return -1;
		if(str_comp_nocase(pValue, "腾讯云") == 0)
			return BACKEND_TENCENT_CLOUD;
		for(int i = 0; i < BACKEND_COUNT; ++i)
		{
			if(str_comp_nocase(pValue, BackendCodes()[i]) == 0)
				return i;
		}
		return -1;
	}

	inline int DisplayIndex(const int FoundIndex, const int FallbackIndex, const int Count)
	{
		return FoundIndex >= 0 && FoundIndex < Count ? FoundIndex : std::clamp(FallbackIndex, 0, std::max(0, Count - 1));
	}

	inline int LanguageIndexForDisplay(const char *pValue, const int FallbackIndex = 0)
	{
		return DisplayIndex(FindLanguageIndex(pValue), FallbackIndex, LANGUAGE_COUNT);
	}

	inline int SourceLanguageIndexForDisplay(const char *pValue, const int FallbackIndex = 0)
	{
		return DisplayIndex(FindSourceLanguageIndex(pValue), FallbackIndex, SOURCE_LANGUAGE_COUNT);
	}

	inline int BackendIndexForDisplay(const char *pValue)
	{
		return DisplayIndex(FindBackendIndex(pValue), BACKEND_LLM, BACKEND_COUNT);
	}

	inline bool NormalizeBackend(char *pConfigValue, const size_t ConfigValueSize)
	{
		const int BackendIndex = FindBackendIndex(pConfigValue);
		if(pConfigValue == nullptr || ConfigValueSize == 0 || BackendIndex < 0 || str_comp(pConfigValue, BackendCodes()[BackendIndex]) == 0)
			return false;
		str_copy(pConfigValue, BackendCodes()[BackendIndex], ConfigValueSize);
		return true;
	}

	inline bool CommitSelection(char *pConfigValue, const size_t ConfigValueSize, const char *const *ppCodes, const size_t Count, const int DisplayedIndex, const int NewIndex)
	{
		if(pConfigValue == nullptr || ConfigValueSize == 0 || ppCodes == nullptr || NewIndex < 0 || NewIndex >= (int)Count || NewIndex == DisplayedIndex)
			return false;
		str_copy(pConfigValue, ppCodes[NewIndex], ConfigValueSize);
		return true;
	}

	template<size_t N>
	bool CommitSelection(char *pConfigValue, const size_t ConfigValueSize, const std::array<const char *, N> &Codes, const int DisplayedIndex, const int NewIndex)
	{
		return CommitSelection(pConfigValue, ConfigValueSize, Codes.data(), N, DisplayedIndex, NewIndex);
	}

	inline bool CommitLanguage(char *pConfigValue, const size_t ConfigValueSize, const int DisplayedIndex, const int NewIndex)
	{
		return CommitSelection(pConfigValue, ConfigValueSize, LanguageCodes(), DisplayedIndex, NewIndex);
	}

	inline bool CommitSourceLanguage(char *pConfigValue, const size_t ConfigValueSize, const int DisplayedIndex, const int NewIndex)
	{
		return CommitSelection(pConfigValue, ConfigValueSize, SourceLanguageCodes(), DisplayedIndex, NewIndex);
	}

	inline bool CommitBackend(char *pConfigValue, const size_t ConfigValueSize, const int DisplayedIndex, const int NewIndex)
	{
		return CommitSelection(pConfigValue, ConfigValueSize, BackendCodes(), DisplayedIndex, NewIndex);
	}
}

#endif
