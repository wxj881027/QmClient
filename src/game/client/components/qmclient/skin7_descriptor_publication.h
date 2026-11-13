#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_DESCRIPTOR_PUBLICATION_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SKIN7_DESCRIPTOR_PUBLICATION_H

#include "skin7_info_job.h"

#include <base/str.h>

#include <algorithm>
#include <type_traits>
#include <utility>
#include <vector>

namespace qm_skin7_descriptor
{
	enum class EError
	{
		NONE,
		PART_OBJECT,
		FILENAME_STRING,
	};
	struct SStatus
	{
		EError m_Error = EError::NONE;
		int m_Part = -1;
	};

	// 诊断失败不阻断解析或发布；正常返回后不得再有可能抛出的发布步骤。
	// Lookup只借用已稳定的部件，空名称指针表示使用该部件的默认项。
	template<typename TSkin, typename TAllocator, typename TLookup, typename TDiagnose>
	bool Publish(std::vector<TSkin, TAllocator> &Skins, const char *pName, int Flags, bool Special,
		const json_value &Info, const char *const (&PartNames)[protocol7::NUM_SKINPARTS],
		const char *const (&Components)[4], TLookup &&Lookup, TDiagnose &&Diagnose)
	{
		static_assert(std::is_nothrow_copy_constructible_v<TSkin> && std::is_nothrow_move_assignable_v<TSkin>);
		TSkin Skin;
		str_copy(Skin.m_aName, pName);
		Skin.m_Flags = Flags;
		SStatus Status;
		const json_value &Start = Info["skin"];
		for(int PartIndex = 0; PartIndex < protocol7::NUM_SKINPARTS; ++PartIndex)
		{
			Skin.m_aUseCustomColors[PartIndex] = 0;
			Skin.m_aPartColors[PartIndex] = (PartIndex == protocol7::SKINPART_MARKING ? 0xFF000000u : 0u) + 0x00FF80u;
			const json_value &Part = Start[PartNames[PartIndex]];
			if(Part.type == json_none)
			{
				Skin.m_apParts[PartIndex] = Lookup(PartIndex, nullptr, Special);
				continue;
			}
			if(Part.type != json_object)
			{
				Status = {EError::PART_OBJECT, PartIndex};
				break;
			}
			const json_value &Filename = Part["filename"];
			if(Filename.type != json_string)
			{
				Status = {EError::FILENAME_STRING, PartIndex};
				break;
			}
			Skin.m_apParts[PartIndex] = Lookup(PartIndex, static_cast<const char *>(Filename), Special);
			const json_value &Color = Part["custom_colors"];
			const bool Custom = Color.type == json_string ? str_comp(static_cast<const char *>(Color), "true") == 0 : Color.type == json_boolean && Color.u.boolean;
			Skin.m_aUseCustomColors[PartIndex] = Custom;
			if(!Custom)
				continue;
			for(int i = 0; i < 4; ++i)
			{
				if(i == 3 && PartIndex != protocol7::SKINPART_MARKING)
					continue;
				const json_value &Value = Part[Components[i]];
				// 与缺失/错误类型相同：异常分量保留默认字节，不污染相邻分量。
				if(Value.type != json_integer || Value.u.integer < 0 || Value.u.integer > 255)
					continue;
				const int Shift = i == 3 ? 24 : (2 - i) * 8;
				const unsigned Mask = 0xffu << Shift;
				Skin.m_aPartColors[PartIndex] = (Skin.m_aPartColors[PartIndex] & ~Mask) | (static_cast<unsigned>(Value.u.integer) << Shift);
			}
		}
		try
		{
			Diagnose(Status);
		}
		catch(...)
		{
		}
		if(Status.m_Error != EError::NONE)
			return false;
		Skins.insert(std::lower_bound(Skins.begin(), Skins.end(), Skin), Skin);
		return true;
	}
}

// 只由主线程消费已结束扫描；无效描述沿原路径跳过。
// Publish抛出必须表示尚未发布，保留JSON和游标供后续帧重试。
template<typename TPublish>
bool QmTryFinalizeNextSkin7(CQmSkin7InfoJob &Job, size_t &Next, TPublish &&Publish) noexcept
{
	if(Next >= Job.Entries().size())
		return false;
	try
	{
		std::forward<TPublish>(Publish)(Job.Entries()[Next]);
		Job.ReleaseEntry(Next);
		++Next;
		return true;
	}
	catch(...)
	{
		return false;
	}
}

#endif
