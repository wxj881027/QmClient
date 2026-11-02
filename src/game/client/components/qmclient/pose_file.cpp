#include "pose_file.h"

#include <base/system.h>

#include <cstring>

namespace qm_pose
{
	namespace
	{
		// 名字/clan/skin 都是「4 个 int32 打包一串字符」：每个字节是 ASCII + 128，
		// 遇到 0 即结束。与 gamecore 的 StrToInts / IntsToStr 同构，但这里不依赖那些
		// 只在 game 模块可见的函数，解析层保持独立、可单测。
		void UnpackChars(const int32_t *pInts, size_t NumInts, char *pOut, size_t OutSize)
		{
			size_t Written = 0;
			for(size_t i = 0; i < NumInts; ++i)
			{
				const uint32_t Packed = (uint32_t)pInts[i];
				for(int Shift = 24; Shift >= 0; Shift -= 8)
				{
					const int Value = (int)((Packed >> Shift) & 0xff) - 128;
					if(Value == 0 || Written + 1 >= OutSize)
					{
						pOut[Written] = '\0';
						return;
					}
					// 按原样保留字节：该格式用「字节 + 128」打包，非 ASCII 字符（中文名）
					// 会在写入端就溢出丢位，读回来必然不是原文。这里不做二次加工，
					// 交给渲染层的 UTF-8 解码按替换字符显示——伪造一个"正确"的名字更糟。
					pOut[Written++] = (char)(Value & 0xff);
				}
			}
			pOut[Written] = '\0';
		}

		int32_t ReadInt32(const unsigned char *pBase, size_t Offset)
		{
			return (int32_t)((uint32_t)pBase[Offset] | ((uint32_t)pBase[Offset + 1] << 8) |
					 ((uint32_t)pBase[Offset + 2] << 16) | ((uint32_t)pBase[Offset + 3] << 24));
		}

		int64_t ReadInt64(const unsigned char *pBase)
		{
			const uint64_t Low = (uint32_t)ReadInt32(pBase, 0);
			const uint64_t High = (uint32_t)ReadInt32(pBase, 4);
			return (int64_t)(Low | (High << 32));
		}

		bool InRange(int Value)
		{
			return Value > -MAX_COORDINATE && Value < MAX_COORDINATE;
		}

		// 字段偏移与 MomentCap 的 pose_merger/index.js 一致：它在生产数据上验证过。
		enum
		{
			OFF_NAME = 0,
			OFF_CLAN = 16,
			OFF_COUNTRY = 28,
			OFF_SKIN = 32,
			OFF_USE_CUSTOM_COLOR = 56,
			OFF_COLOR_BODY = 60,
			OFF_COLOR_FEET = 64,
			// CharacterCore：tick, x, y, velx, vely, angle, direction, jumped,
			// hookedPlayer, hookState, hookTick, hookX, hookY, hookDx, hookDy
			OFF_CORE = 68,
			OFF_EMOTE = 128,
			OFF_WEAPON = 132,
		};
	} // namespace

	const char *LoadResultName(ELoadResult Result)
	{
		switch(Result)
		{
		case ELoadResult::OK: return "ok";
		case ELoadResult::EMPTY: return "empty";
		case ELoadResult::SIZE_MISMATCH: return "size does not match the record count";
		case ELoadResult::TOO_MANY: return "too many poses";
		case ELoadResult::BAD_COORDINATE: return "position out of range";
		}
		return "unknown";
	}

	void ReadPackedString(const int32_t *pInts, size_t NumInts, char *pOut, size_t OutSize)
	{
		if(!pOut || OutSize == 0)
			return;
		UnpackChars(pInts, NumInts, pOut, OutSize);
	}

	ELoadResult Load(const void *pData, size_t Size, std::vector<SPoseTee> &vOut)
	{
		if(!pData || Size < sizeof(int64_t))
			return ELoadResult::EMPTY;

		const auto *pBase = (const unsigned char *)pData;
		const int64_t Count = ReadInt64(pBase);
		if(Count <= 0)
			return ELoadResult::EMPTY;
		if((uint64_t)Count > MAX_POSES)
			return ELoadResult::TOO_MANY;

		// 长度必须严格吻合：这是唯一能挡住「布局变了」的校验，宁可报错也不猜。
		const size_t Expected = sizeof(int64_t) + (size_t)Count * RECORD_SIZE;
		if(Size != Expected)
			return ELoadResult::SIZE_MISMATCH;

		std::vector<SPoseTee> vParsed;
		vParsed.reserve((size_t)Count);
		for(int64_t i = 0; i < Count; ++i)
		{
			const unsigned char *pRecord = pBase + sizeof(int64_t) + (size_t)i * RECORD_SIZE;

			SPoseTee Tee{};
			int32_t aName[4];
			int32_t aClan[3];
			int32_t aSkin[6];
			for(int k = 0; k < 4; ++k)
				aName[k] = ReadInt32(pRecord, OFF_NAME + (size_t)k * 4);
			for(int k = 0; k < 3; ++k)
				aClan[k] = ReadInt32(pRecord, OFF_CLAN + (size_t)k * 4);
			for(int k = 0; k < 6; ++k)
				aSkin[k] = ReadInt32(pRecord, OFF_SKIN + (size_t)k * 4);
			ReadPackedString(aName, 4, Tee.m_aName, sizeof(Tee.m_aName));
			ReadPackedString(aClan, 3, Tee.m_aClan, sizeof(Tee.m_aClan));
			ReadPackedString(aSkin, 6, Tee.m_aSkin, sizeof(Tee.m_aSkin));

			Tee.m_UseCustomColor = ReadInt32(pRecord, OFF_USE_CUSTOM_COLOR) != 0;
			Tee.m_ColorBody = ReadInt32(pRecord, OFF_COLOR_BODY);
			Tee.m_ColorFeet = ReadInt32(pRecord, OFF_COLOR_FEET);

			// CharacterCore：跳过 tick，取 x/y/angle/direction/hookState/hookX/hookY。
			Tee.m_X = ReadInt32(pRecord, OFF_CORE + 4);
			Tee.m_Y = ReadInt32(pRecord, OFF_CORE + 8);
			Tee.m_Angle = ReadInt32(pRecord, OFF_CORE + 20);
			Tee.m_Direction = ReadInt32(pRecord, OFF_CORE + 24);
			Tee.m_HookState = ReadInt32(pRecord, OFF_CORE + 36);
			Tee.m_HookX = ReadInt32(pRecord, OFF_CORE + 44);
			Tee.m_HookY = ReadInt32(pRecord, OFF_CORE + 48);

			Tee.m_Emote = ReadInt32(pRecord, OFF_EMOTE);
			Tee.m_Weapon = ReadInt32(pRecord, OFF_WEAPON);

			if(!InRange(Tee.m_X) || !InRange(Tee.m_Y))
				return ELoadResult::BAD_COORDINATE;

			vParsed.push_back(Tee);
		}

		vOut = std::move(vParsed);
		return ELoadResult::OK;
	}
} // namespace qm_pose
