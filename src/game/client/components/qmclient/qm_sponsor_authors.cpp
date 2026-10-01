#include "qm_sponsor_authors.h"

#include <base/math.h>

const std::array<QmSponsorAuthors::SAuthor, 3> &QmSponsorAuthors::Authors()
{
	static constexpr std::array<SAuthor, 3> s_aAuthors = {{
		{"qmclient-community-author-xuanmeng", "璇梦", "qwqdog_mie", false, ColorRGBA(), ColorRGBA()},
		{"qmclient-community-author-dyl", "DYL", "default_v2", true, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f)},
		{"qmclient-community-author-xiari", "夏日", "Miemiemiea", false, ColorRGBA(), ColorRGBA()},
	}};
	return s_aAuthors;
}

float QmSponsorAuthors::RowsHeight(float TeeSize, float LineSpacing, float LabelHeight)
{
	return maximum(0.0f, TeeSize) + maximum(0.0f, LineSpacing) + maximum(0.0f, LabelHeight);
}
