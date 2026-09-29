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

	// 仅由明确的启用/恢复操作调用；函数再次显示修改目标和备份说明。
	int ConfigureKugou(bool Restore, std::string *pMessage);
}

#endif
