/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#ifndef GAME_CLIENT_UI_CARD_PREFERENCES_STORAGE_H
#define GAME_CLIENT_UI_CARD_PREFERENCES_STORAGE_H

#include <string>

class CCardUiModel;
class IStorage;

constexpr const char *CARD_PREFERENCES_PATH = "qmclient/ui-preferences.json";
constexpr int CARD_PREFERENCES_VERSION = 2;

std::string SerializeCardPreferences(const CCardUiModel &Model);
bool ParseCardPreferences(const std::string &Json, CCardUiModel &Model, std::string &Error);
bool LoadCardPreferences(IStorage &Storage, CCardUiModel &Model, std::string &Error);
bool SaveCardPreferences(IStorage &Storage, CCardUiModel &Model, std::string &Error);

#endif
