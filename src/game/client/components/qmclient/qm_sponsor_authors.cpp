#include "qm_sponsor_authors.h"

#include <base/math.h>

const std::array<QmSponsorAuthors::SAuthor, 3> &QmSponsorAuthors::Authors()
{
	static constexpr std::array<SAuthor, 3> s_aAuthors = {{
		{"qmclient-community-author-xuanmeng", "璇梦", "qwqdog_mie"},
		{"qmclient-community-author-dyl", "DYL", "10Nanami_glow"},
		{"qmclient-community-author-xiari", "夏日", "Miemiemiea"},
	}};
	return s_aAuthors;
}

float QmSponsorAuthors::RowsHeight(float TeeSize, float LineSpacing, float LabelHeight)
{
	return maximum(0.0f, TeeSize) + maximum(0.0f, LineSpacing) + maximum(0.0f, LabelHeight);
}
