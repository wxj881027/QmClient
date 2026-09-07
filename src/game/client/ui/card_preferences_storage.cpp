/* (c) QmClient contributors. See licence.txt in the root of the distribution. */
#include "card_preferences_storage.h"

#include "card_ui_model.h"

#include <base/io.h>
#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <filesystem>
#include <limits>
#include <memory>
#include <unordered_set>

namespace
{
constexpr size_t MAX_FILE_SIZE = 1024 * 1024;
constexpr unsigned MAX_CARDS = 4096;

bool Fail(std::string &Error, const char *pMessage)
{
	Error = pMessage;
	return false;
}

bool HasUniqueFields(const json_value &Object, unsigned ExpectedCount)
{
	if(Object.type != json_object || Object.u.object.length != ExpectedCount)
		return false;
	std::unordered_set<std::string> Seen;
	for(const auto &Field : Object.u.object)
	{
		const std::string Name(Field.name, Field.name_length);
		if(Name.find('\0') != std::string::npos || !Seen.insert(Name).second)
			return false;
	}
	return true;
}
}

std::string SerializeCardPreferences(const CCardUiModel &Model)
{
	CJsonStringWriter Writer;
	Writer.BeginObject();
	Writer.WriteAttribute("version");
	Writer.WriteIntValue(CARD_PREFERENCES_VERSION);
	Writer.WriteAttribute("cards");
	Writer.BeginArray();
	for(const auto &Entry : Model.ExportPreferences())
	{
		Writer.BeginObject();
		Writer.WriteAttribute("id");
		Writer.WriteStrValue(Entry.first.c_str());
		Writer.WriteAttribute("visible");
		Writer.WriteBoolValue(Entry.second.m_Visible);
		Writer.WriteAttribute("collapsed");
		Writer.WriteBoolValue(Entry.second.m_Collapsed);
		Writer.EndObject();
	}
	Writer.EndArray();
	Writer.WriteAttribute("placements");
	Writer.BeginArray();
	for(const SCardOrderEntry &Entry : Model.OrderModel().Entries())
	{
		Writer.BeginObject();
		Writer.WriteAttribute("id");
		Writer.WriteStrValue(Entry.m_Id.c_str());
		Writer.WriteAttribute("page");
		Writer.WriteStrValue(Entry.m_PageId.c_str());
		Writer.WriteAttribute("column");
		Writer.WriteIntValue(static_cast<int>(Entry.m_Column));
		Writer.WriteAttribute("order");
		Writer.WriteIntValue(Entry.m_Order);
		Writer.EndObject();
	}
	Writer.EndArray();
	Writer.WriteAttribute("view");
	Writer.BeginObject();
	Writer.WriteAttribute("mode");
	Writer.WriteIntValue(Model.ViewPreferences().m_Mode);
	Writer.WriteAttribute("light");
	Writer.WriteBoolValue(Model.ViewPreferences().m_LightTheme);
	Writer.WriteAttribute("animations");
	Writer.WriteBoolValue(Model.ViewPreferences().m_Animations);
	Writer.EndObject();
	Writer.EndObject();
	return Writer.GetOutputString();
}

bool ParseCardPreferences(const std::string &Json, CCardUiModel &Model, std::string &Error)
{
	Error.clear();
	if(Json.size() > MAX_FILE_SIZE)
		return Fail(Error, "card preferences exceed the size limit");
	json_settings Settings{};
	Settings.max_memory = 8 * MAX_FILE_SIZE;
	char aParseError[json_error_max]{};
	const std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(
		JsonParseEx(&Settings, Json.data(), Json.size(), aParseError), json_value_free);
	if(!pRoot)
		return Fail(Error, "invalid card preferences JSON");
	if(!HasUniqueFields(*pRoot, 2) && !HasUniqueFields(*pRoot, 4))
		return Fail(Error, "invalid card preferences root fields");
	const json_value &Version = *json_object_get(pRoot.get(), "version");
	const json_value &Cards = *json_object_get(pRoot.get(), "cards");
	if(Version.type != json_integer || (Version.u.integer != 1 && Version.u.integer != CARD_PREFERENCES_VERSION))
		return Fail(Error, "unsupported card preferences version");
	if(!HasUniqueFields(*pRoot, Version.u.integer == 1 ? 2 : 4))
		return Fail(Error, "invalid card preferences version fields");
	if(Cards.type != json_array || Cards.u.array.length > MAX_CARDS)
		return Fail(Error, "invalid card preferences array");

	std::vector<std::pair<std::string, SCardUiPreferences>> vPreferences;
	const CCardOrderModel Defaults = Model.Registry().BuildDefaultOrderModel();
	std::unordered_set<std::string> Seen;
	for(const json_value *pCard : Cards.u.array)
	{
		if(!HasUniqueFields(*pCard, Version.u.integer == 1 ? 4 : 3))
			return Fail(Error, "invalid card preference fields");
		const json_value &Id = *json_object_get(pCard, "id");
		const json_value &Visible = *json_object_get(pCard, "visible");
		const json_value &Collapsed = *json_object_get(pCard, "collapsed");
		const json_value &Order = *json_object_get(pCard, "order");
		if(Id.type != json_string || Id.u.string.length == 0 || Id.u.string.length > 128 ||
			Visible.type != json_boolean || Collapsed.type != json_boolean ||
			(Version.u.integer == 1 && (Order.type != json_integer || Order.u.integer < 0 || Order.u.integer > std::numeric_limits<int>::max())))
			return Fail(Error, "invalid card preference value");
		const std::string CardId(Id.u.string.ptr, Id.u.string.length);
		if(CardId.find('\0') != std::string::npos || !Seen.insert(CardId).second)
			return Fail(Error, "duplicate or invalid card preference id");
		// 未注册的旧卡片不参与当前模型；文件加载不立即回写。
		if(const auto *pDefault = Defaults.Find(CardId))
			vPreferences.emplace_back(CardId, SCardUiPreferences{Visible.u.boolean != 0, Collapsed.u.boolean != 0, Version.u.integer == 1 ? static_cast<int>(Order.u.integer) : pDefault->m_Order});
	}
	std::vector<SCardOrderEntry> vPlacements;
	SCardViewPreferences ViewPreferences;
	Seen.clear();
	if(Version.u.integer == CARD_PREFERENCES_VERSION)
	{
		const json_value &View = *json_object_get(pRoot.get(), "view");
		if(!HasUniqueFields(View, 3))
			return Fail(Error, "invalid card view fields");
		const auto &Mode = *json_object_get(&View, "mode");
		const auto &Light = *json_object_get(&View, "light");
		const auto &Animations = *json_object_get(&View, "animations");
		if(Mode.type != json_integer || Mode.u.integer < 0 || Mode.u.integer > 2 || Light.type != json_boolean || Animations.type != json_boolean)
			return Fail(Error, "invalid card view value");
		ViewPreferences = {static_cast<int>(Mode.u.integer), Light.u.boolean != 0, Animations.u.boolean != 0};
		const json_value &Placements = *json_object_get(pRoot.get(), "placements");
		if(Placements.type != json_array || Placements.u.array.length > MAX_CARDS)
			return Fail(Error, "invalid card placements array");
		for(const json_value *pPlacement : Placements.u.array)
		{
			if(!HasUniqueFields(*pPlacement, 4))
				return Fail(Error, "invalid card placement fields");
			const auto &Id = *json_object_get(pPlacement, "id");
			const auto &Page = *json_object_get(pPlacement, "page");
			const auto &Column = *json_object_get(pPlacement, "column");
			const auto &Order = *json_object_get(pPlacement, "order");
			if(Id.type != json_string || Id.u.string.length == 0 || Id.u.string.length > 128 ||
				Page.type != json_string || Page.u.string.length == 0 || Page.u.string.length > 128 ||
				Column.type != json_integer || Column.u.integer < 0 || Column.u.integer > 2 ||
				Order.type != json_integer || Order.u.integer < 0 || Order.u.integer > std::numeric_limits<int>::max())
				return Fail(Error, "invalid card placement value");
			const std::string CardId(Id.u.string.ptr, Id.u.string.length);
			const std::string PageId(Page.u.string.ptr, Page.u.string.length);
			if(CardId.find('\0') != std::string::npos || PageId.find('\0') != std::string::npos || !Seen.insert(CardId).second)
				return Fail(Error, "duplicate or invalid card placement id");
			if(Model.Registry().FindCard(CardId) && Model.Registry().FindPage(PageId))
				vPlacements.push_back({CardId, PageId, static_cast<ECardColumn>(Column.u.integer), static_cast<int>(Order.u.integer)});
		}
	}
	// 先验证完整文档，再一次性交换，坏文件不能覆盖现有偏好。
	if(!Model.ReplaceState(vPreferences, vPlacements))
		return Fail(Error, "card preferences import failed");
	Model.SetViewPreferences(ViewPreferences);
	Model.ClearDirty();
	return true;
}

bool LoadCardPreferences(IStorage &Storage, CCardUiModel &Model, std::string &Error)
{
	Error.clear();
	if(!Storage.FileExists(CARD_PREFERENCES_PATH, IStorage::TYPE_SAVE))
		return true;
	IOHANDLE File = Storage.OpenFile(CARD_PREFERENCES_PATH, IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
		return Fail(Error, "could not open card preferences");
	const int64_t Length = io_length(File);
	if(Length < 0 || Length > static_cast<int64_t>(MAX_FILE_SIZE))
	{
		io_close(File);
		return Fail(Error, "invalid card preferences file size");
	}
	std::string Json(static_cast<size_t>(Length), '\0');
	const bool ReadOk = io_read(File, Json.data(), static_cast<unsigned>(Json.size())) == Json.size() && io_error(File) == 0;
	const bool Closed = io_close(File) == 0;
	if(!ReadOk || !Closed)
		return Fail(Error, "could not read card preferences");
	return ParseCardPreferences(Json, Model, Error);
}

bool SaveCardPreferences(IStorage &Storage, CCardUiModel &Model, std::string &Error)
{
	Error.clear();
	if(!Model.IsDirty())
		return true;
	if(!Storage.FolderExists("qmclient", IStorage::TYPE_SAVE) && !Storage.CreateFolder("qmclient", IStorage::TYPE_SAVE))
		return Fail(Error, "could not create card preferences directory");
	const std::string Json = SerializeCardPreferences(Model);
	if(Json.size() > MAX_FILE_SIZE || Model.OrderModel().Entries().size() > MAX_CARDS)
		return Fail(Error, "card preferences exceed the size limit");
	char aTemporaryPath[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aTemporaryPath, sizeof(aTemporaryPath), CARD_PREFERENCES_PATH);
	IOHANDLE File = Storage.OpenFile(aTemporaryPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return Fail(Error, "could not open temporary card preferences");
	const bool Written = io_write(File, Json.data(), static_cast<unsigned>(Json.size())) == Json.size() && io_sync(File) == 0 && io_error(File) == 0;
	const bool Closed = io_close(File) == 0;
	if(!Written || !Closed)
	{
		Storage.RemoveFile(aTemporaryPath, IStorage::TYPE_SAVE);
		return Fail(Error, "could not commit card preferences");
	}
	char aSource[IO_MAX_PATH_LENGTH];
	char aTarget[IO_MAX_PATH_LENGTH];
	Storage.GetCompletePath(IStorage::TYPE_SAVE, aTemporaryPath, aSource, sizeof(aSource));
	Storage.GetCompletePath(IStorage::TYPE_SAVE, CARD_PREFERENCES_PATH, aTarget, sizeof(aTarget));
	// 官方 fs_rename 在 Windows 会先删除占用中的目标再重试；偏好保存不可采用该回退。
	std::error_code RenameError;
	std::filesystem::rename(std::filesystem::u8path(aSource), std::filesystem::u8path(aTarget), RenameError);
	if(RenameError)
	{
		Storage.RemoveFile(aTemporaryPath, IStorage::TYPE_SAVE);
		return Fail(Error, "could not commit card preferences");
	}
	Model.ClearDirty();
	return true;
}
