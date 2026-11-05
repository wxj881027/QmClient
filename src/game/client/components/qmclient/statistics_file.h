#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_STATISTICS_FILE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_STATISTICS_FILE_H

#include "qm_json_fields.h"

#include <base/mem.h>
#include <base/system.h>

#include <engine/shared/json.h>
#include <engine/storage.h>

#include <cstdlib>
#include <memory>
#include <utility>

enum class EQmStatisticsFileLoadResult
{
	NOT_FOUND,
	VALID,
	INVALID,
};

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
		if(Result != IStorage::EReadFileResult::SUCCESS || !Buffer.m_pData || mem_has_null(Buffer.m_pData, Size))
			return EQmStatisticsFileLoadResult::INVALID;
		std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParse(static_cast<const char *>(Buffer.m_pData), Size), json_value_free);
		if(!pRoot || pRoot->type != json_object || qm_json::Field(pRoot.get(), "local")->type != json_object || qm_json::Field(pRoot.get(), "remote")->type != json_object)
			return EQmStatisticsFileLoadResult::INVALID;
		*ppRoot = pRoot.release();
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
		return pStorage->ReadFileChecked(pFilename, IStorage::TYPE_SAVE, ppData, pSize);
	});
}

#endif
