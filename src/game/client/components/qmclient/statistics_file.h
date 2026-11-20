#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_STATISTICS_FILE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_STATISTICS_FILE_H

#include "qm_json_fields.h"

#include <base/mem.h>
#include <base/system.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

enum class EQmStatisticsFileLoadResult
{
	NOT_FOUND,
	VALID,
	INVALID,
};

constexpr unsigned QM_STATISTICS_MAX_FILE_BYTES = 64u * 1024 * 1024;
constexpr unsigned long QM_STATISTICS_MAX_JSON_MEMORY = 256ul * 1024 * 1024;

// 保持现有JSON输出，达到正文预算前停止扩展；调用方沿保存失败路径保留dirty。
class CQmStatisticsStringWriter final : public CJsonWriter
{
	std::string m_Output;
	size_t m_MaxBytes;
	bool m_RetrievedOutput = false;

	void WriteInternal(const char *pText, int Length = -1) override
	{
		dbg_assert(!m_RetrievedOutput, "Writer output has already been retrieved");
		const size_t Size = Length < 0 ? std::strlen(pText) : static_cast<size_t>(Length);
		if(Size > m_MaxBytes - m_Output.size())
			throw std::length_error("statistics snapshot exceeds size budget");
		m_Output.append(pText, Size);
	}

public:
	explicit CQmStatisticsStringWriter(size_t MaxBytes = QM_STATISTICS_MAX_FILE_BYTES) :
		m_MaxBytes(MaxBytes < QM_STATISTICS_MAX_FILE_BYTES ? MaxBytes : QM_STATISTICS_MAX_FILE_BYTES) {}
	std::string GetOutputString()
	{
		WriteInternal("\n");
		m_RetrievedOutput = true;
		return std::move(m_Output);
	}
};

// 读写共用预算和文档入口校验，失败不移交解析树。
inline json_value *QmParseStatisticsDocument(const char *pData, size_t Size) noexcept
try
{
	if(!pData || Size == 0 || Size > QM_STATISTICS_MAX_FILE_BYTES || mem_has_null(pData, Size))
		return nullptr;
	json_settings Settings{};
	Settings.max_memory = QM_STATISTICS_MAX_JSON_MEMORY;
	std::unique_ptr<json_value, decltype(&json_value_free)> Root(JsonParseEx(&Settings, pData, Size, nullptr), json_value_free);
	if(!Root || Root->type != json_object || qm_json::Field(Root.get(), "local")->type != json_object || qm_json::Field(Root.get(), "remote")->type != json_object)
		return nullptr;
	return Root.release();
}
catch(...)
{
	return nullptr;
}

// 保存使用十进制字符串，读取也接受原有非负JSON数值；异常输入不改输出。
inline bool QmStatisticsNonNegativeInt64(const json_value *pValue, int64_t &Out) noexcept
{
	if(!pValue || pValue->type != json_string)
		return qm_json::NonNegativeInt64(pValue, Out);
	if(!qm_json::Text(pValue, false) || pValue->u.string.length > 19)
		return false;
	const int64_t Value = str_toint64_base(pValue->u.string.ptr);
	if(Value < 0)
		return false;
	char aCanonical[32];
	str_format(aCanonical, sizeof(aCanonical), "%lld", static_cast<long long>(Value));
	if(str_comp(aCanonical, pValue->u.string.ptr) != 0)
		return false;
	Out = Value;
	return true;
}

// 读入缓冲由调用方边界提供，始终使用free兼容内存；异常后也由本作用域回收。
template<typename TRead>
EQmStatisticsFileLoadResult QmLoadStatisticsFile(json_value **ppRoot, TRead &&Read) noexcept
{
	if(ppRoot)
		*ppRoot = nullptr;
	if(!ppRoot)
		return EQmStatisticsFileLoadResult::INVALID;
	struct SReadBuffer
	{
		void *m_pData = nullptr;
		~SReadBuffer() { free(m_pData); }
	} Buffer;
	try
	{
		unsigned Size = 0;
		const auto Result = std::forward<TRead>(Read)(&Buffer.m_pData, &Size);
		if(Result == IStorage::EReadFileResult::NOT_FOUND)
			return EQmStatisticsFileLoadResult::NOT_FOUND;
		if(Result != IStorage::EReadFileResult::SUCCESS)
			return EQmStatisticsFileLoadResult::INVALID;
		json_value *pRoot = QmParseStatisticsDocument(static_cast<const char *>(Buffer.m_pData), Size);
		if(!pRoot)
			return EQmStatisticsFileLoadResult::INVALID;
		*ppRoot = pRoot;
		return EQmStatisticsFileLoadResult::VALID;
	}
	catch(...)
	{
		return EQmStatisticsFileLoadResult::INVALID;
	}
}

inline EQmStatisticsFileLoadResult QmLoadStatisticsFile(IStorage *pStorage, const char *pFilename, json_value **ppRoot) noexcept
{
	if(!pStorage || !pFilename || pFilename[0] == '\0')
	{
		if(ppRoot)
			*ppRoot = nullptr;
		return EQmStatisticsFileLoadResult::INVALID;
	}
	return QmLoadStatisticsFile(ppRoot, [pStorage, pFilename](void **ppData, unsigned *pSize) {
		return pStorage->ReadFileCheckedBounded(pFilename, IStorage::TYPE_SAVE, ppData, pSize, QM_STATISTICS_MAX_FILE_BYTES);
	});
}

#endif
