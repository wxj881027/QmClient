#include <base/system.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/skin_source_registry.h>

#include <cstdlib>

void CSkinSourceRegistry::Load(IStorage *pStorage)
{
	m_Sources.clear();
	m_Dirty = false;
	if(pStorage == nullptr)
		return;

	void *pFileData = nullptr;
	unsigned FileSize = 0;
	if(!pStorage->ReadFile(FILE, IStorage::TYPE_SAVE, &pFileData, &FileSize))
		return;

	json_value *pRoot = JsonParse(static_cast<const char *>(pFileData), FileSize);
	free(pFileData);
	if(pRoot == nullptr)
		return;

	const json_value &Sources = (*pRoot)["sources"];
	if(Sources.type == json_object)
	{
		for(unsigned Index = 0; Index < Sources.u.object.length; ++Index)
		{
			const json_value *pValue = Sources.u.object.values[Index].value;
			if(pValue == nullptr || pValue->type != json_string)
				continue;
			const ESkinSource Source = SkinSourceFromName(json_string_get(pValue));
			if(Source == ESkinSource::UNKNOWN || Source == ESkinSource::LOCAL)
				continue;
			m_Sources[CSkinNameIndex::Key(Sources.u.object.values[Index].name)] = Source;
		}
	}
	json_value_free(pRoot);
}

bool CSkinSourceRegistry::Save(IStorage *pStorage)
{
	if(pStorage == nullptr || (!m_Dirty && pStorage->FileExists(FILE, IStorage::TYPE_SAVE)))
		return true;

	char aTemp[IO_MAX_PATH_LENGTH];
	IStorage::FormatTmpPath(aTemp, sizeof(aTemp), FILE);
	IOHANDLE File = pStorage->OpenFile(aTemp, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File == nullptr)
		return false;

	bool Written = false;
	{
		CJsonFileWriter Writer(File);
		Writer.BeginObject();
		Writer.WriteAttribute("version");
		Writer.WriteIntValue(1);
		Writer.WriteAttribute("sources");
		Writer.BeginObject();
		for(const auto &[Key, Source] : m_Sources)
		{
			Writer.WriteAttribute(Key.c_str());
			Writer.WriteStrValue(SkinSourceName(Source));
		}
		Writer.EndObject();
		Writer.EndObject();
		Written = Writer.Finish();
	}
	if(!Written)
	{
		pStorage->RemoveFile(aTemp, IStorage::TYPE_SAVE);
		return false;
	}

	char aBackup[IO_MAX_PATH_LENGTH];
	if(!IStorage::ReplaceFileSafely(pStorage, aTemp, FILE, aBackup, sizeof(aBackup)))
	{
		pStorage->RemoveFile(aTemp, IStorage::TYPE_SAVE);
		return false;
	}
	m_Dirty = false;
	return true;
}

void CSkinSourceRegistry::Clear()
{
	m_Sources.clear();
	m_Dirty = true;
}

std::optional<ESkinSource> CSkinSourceRegistry::Find(const char *pName) const
{
	if(pName == nullptr)
		return std::nullopt;
	const auto It = m_Sources.find(CSkinNameIndex::Key(pName));
	return It == m_Sources.end() ? std::nullopt : std::make_optional(It->second);
}

bool CSkinSourceRegistry::Set(const char *pName, ESkinSource Source)
{
	if(pName == nullptr || pName[0] == '\0' || Source == ESkinSource::UNKNOWN || Source == ESkinSource::LOCAL)
		return false;
	const std::string Key = CSkinNameIndex::Key(pName);
	const auto It = m_Sources.find(Key);
	if(It != m_Sources.end() && It->second == Source)
		return false;
	m_Sources[Key] = Source;
	m_Dirty = true;
	return true;
}
