#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_QM_SPONSOR_AUTHORS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_QM_SPONSOR_AUTHORS_H

#include <array>

namespace QmSponsorAuthors
{
	struct SAuthor
	{
		const char *m_pTextId;
		const char *m_pName;
		// 作者指定皮肤，缺失时回退到 default。
		const char *m_pSkin;
	};

	const std::array<SAuthor, 3> &Authors();
	float RowsHeight(float TeeSize, float LineSpacing, float LabelHeight);
}

#endif
