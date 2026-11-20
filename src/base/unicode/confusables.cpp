#include "confusables.h"

#include <base/str.h>

#include <algorithm>
#include <cstddef>

static int str_utf8_skeleton(int ch, const int **skeleton, int *skeleton_len)
{
	const int32_t *end = decomp_chars + NUM_DECOMPS;
	const int32_t *match = std::lower_bound(decomp_chars, end, ch);
	if(match != end && *match == ch)
	{
		const DECOMP_SLICE &slice = decomp_slices[match - decomp_chars];
		*skeleton = &decomp_data[slice.offset];
		*skeleton_len = decomp_lengths[slice.length];
		return 1;
	}
	*skeleton = nullptr;
	*skeleton_len = 1;
	return 0;
}

struct SKELETON
{
	const int *skeleton;
	int skeleton_len;
	const char *str;
};

static void str_utf8_skeleton_begin(struct SKELETON *skel, const char *str)
{
	skel->skeleton = nullptr;
	skel->skeleton_len = 0;
	skel->str = str;
}

static int str_utf8_skeleton_next(struct SKELETON *skel)
{
	int ch = 0;
	while(skel->skeleton_len == 0)
	{
		ch = str_utf8_decode(&skel->str);
		if(ch == 0)
		{
			return 0;
		}
		str_utf8_skeleton(ch, &skel->skeleton, &skel->skeleton_len);
	}
	skel->skeleton_len--;
	if(skel->skeleton != nullptr)
	{
		ch = *skel->skeleton;
		skel->skeleton++;
	}
	return ch;
}

int str_utf8_to_skeleton(const char *str, int *buf, int buf_len)
{
	int i;
	struct SKELETON skel;
	str_utf8_skeleton_begin(&skel, str);
	for(i = 0; i < buf_len; i++)
	{
		int ch = str_utf8_skeleton_next(&skel);
		if(ch == 0)
		{
			break;
		}
		buf[i] = ch;
	}
	return i;
}

int str_utf8_comp_confusable(const char *str1, const char *str2)
{
	struct SKELETON skel1;
	struct SKELETON skel2;

	str_utf8_skeleton_begin(&skel1, str1);
	str_utf8_skeleton_begin(&skel2, str2);

	while(true)
	{
		int ch1 = str_utf8_skeleton_next(&skel1);
		int ch2 = str_utf8_skeleton_next(&skel2);

		if(ch1 == 0 || ch2 == 0)
			return ch1 != ch2;

		if(ch1 != ch2)
			return 1;
	}
}
