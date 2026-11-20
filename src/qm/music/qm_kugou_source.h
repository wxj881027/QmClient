#ifndef QM_MUSIC_QM_KUGOU_SOURCE_H
#define QM_MUSIC_QM_KUGOU_SOURCE_H

#include "qm_music_source.h"

#include <memory>

namespace QmMusicHook
{
	class CKugouSource
	{
	public:
		CKugouSource();
		~CKugouSource();
		CKugouSource(const CKugouSource &) = delete;
		CKugouSource &operator=(const CKugouSource &) = delete;
		bool Poll(SPlayback &State);

	private:
		struct SImpl;
		std::unique_ptr<SImpl> m_pImpl;
	};

	struct SKugouConfigureResult
	{
		int m_Code;
		const wchar_t *m_pMessage;
	};

	// 仅由明确的启用/恢复操作调用；结果由调用方在句柄释放后展示。
	SKugouConfigureResult ConfigureKugou(bool Restore);
}

#endif
