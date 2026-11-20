#include "music_lyrics_qrc.h"

#include <engine/external/zlib/zlib.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace QmMusicLyrics
{
	namespace
	{
		// QRC 文件头 11 字节魔数。
		constexpr uint8_t QRC_MAGIC[11] = {0x98, 0x25, 0xB0, 0xAC, 0xE3, 0x02, 0x83, 0x68, 0xE8, 0xFC, 0x6C};
		// 3DES 密钥(24 字节,K1/K2/K3 各 8 字节;含终止符共 25 字节)。
		constexpr char QRC_KEY[25] = "!@#)(*$%123ZXC!@!@#)(NHL";

		// QMC1 XOR 密钥表(与 qmc 音频解密同源)。
		constexpr uint8_t QMC1_KEY[128] = {
			0xc3, 0x4a, 0xd6, 0xca, 0x90, 0x67, 0xf7, 0x52, 0xd8, 0xa1, 0x66, 0x62, 0x9f, 0x5b, 0x09, 0x00,
			0xc3, 0x5e, 0x95, 0x23, 0x9f, 0x13, 0x11, 0x7e, 0xd8, 0x92, 0x3f, 0xbc, 0x90, 0xbb, 0x74, 0x0e,
			0xc3, 0x47, 0x74, 0x3d, 0x90, 0xaa, 0x3f, 0x51, 0xd8, 0xf4, 0x11, 0x84, 0x9f, 0xde, 0x95, 0x1d,
			0xc3, 0xc6, 0x09, 0xd5, 0x9f, 0xfa, 0x66, 0xf9, 0xd8, 0xf0, 0xf7, 0xa0, 0x90, 0xa1, 0xd6, 0xf3,
			0xc3, 0xf3, 0xd6, 0xa1, 0x90, 0xa0, 0xf7, 0xf0, 0xd8, 0xf9, 0x66, 0xfa, 0x9f, 0xd5, 0x09, 0xc6,
			0xc3, 0x1d, 0x95, 0xde, 0x9f, 0x84, 0x11, 0xf4, 0xd8, 0x51, 0x3f, 0xaa, 0x90, 0x3d, 0x74, 0x47,
			0xc3, 0x0e, 0x74, 0xbb, 0x90, 0xbc, 0x3f, 0x92, 0xd8, 0x7e, 0x11, 0x13, 0x9f, 0x23, 0x95, 0x5e,
			0xc3, 0x00, 0x09, 0x5b, 0x9f, 0x62, 0x66, 0xa1, 0xd8, 0x52, 0xf7, 0x67, 0x90, 0xca, 0xd6, 0x4a};

		// ---- 自定义 DES/3DES 位运算实现(移植自 QQMusicDecoder DESHelper.cs) ----

		uint32_t BitNum(const uint8_t *pData, int Bit, int Shift)
		{
			return (uint32_t)(((pData[(Bit / 32) * 4 + 3 - (Bit % 32) / 8] >> (7 - Bit % 8)) & 1) << Shift);
		}

		uint32_t BitNumIntr(uint32_t Value, int Bit, int Shift)
		{
			return ((Value >> (31 - Bit)) & 1) << Shift;
		}

		uint32_t BitNumIntl(uint32_t Value, int Bit, int Shift)
		{
			return ((Value << Bit) & 0x80000000U) >> Shift;
		}

		int SboxBit(int Value)
		{
			return (Value & 32) | ((Value & 31) >> 1) | ((Value & 1) << 4);
		}

		// 8 组 S-box。
		constexpr uint8_t SBOX[8][64] = {
			{14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
				0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
				4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
				15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13},
			{15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
				3, 13, 4, 7, 15, 2, 8, 15, 12, 0, 1, 10, 6, 9, 11, 5,
				0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
				13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9},
			{10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
				13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
				13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
				1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12},
			{7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
				13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
				10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
				3, 15, 0, 6, 10, 10, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14},
			{2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
				14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
				4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
				11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3},
			{12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
				10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
				9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
				4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13},
			{4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
				13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
				1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
				6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12},
			{13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
				1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
				7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
				2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11}};

		void InitialPermutation(const uint8_t *pInput, uint32_t *pS0, uint32_t *pS1)
		{
			static const int aS0Bits[32] = {57, 49, 41, 33, 25, 17, 9, 1, 59, 51, 43, 35, 27, 19, 11, 3, 61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7};
			static const int aS1Bits[32] = {56, 48, 40, 32, 24, 16, 8, 0, 58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4, 62, 54, 46, 38, 30, 22, 14, 6};
			uint32_t S0 = 0;
			uint32_t S1 = 0;
			for(int i = 0; i < 32; ++i)
			{
				S0 |= BitNum(pInput, aS0Bits[i], 31 - i);
				S1 |= BitNum(pInput, aS1Bits[i], 31 - i);
			}
			*pS0 = S0;
			*pS1 = S1;
		}

		void InversePermutation(uint32_t S0, uint32_t S1, uint8_t *pData)
		{
			pData[3] = (uint8_t)(BitNumIntr(S1, 7, 7) | BitNumIntr(S0, 7, 6) | BitNumIntr(S1, 15, 5) | BitNumIntr(S0, 15, 4) | BitNumIntr(S1, 23, 3) | BitNumIntr(S0, 23, 2) | BitNumIntr(S1, 31, 1) | BitNumIntr(S0, 31, 0));
			pData[2] = (uint8_t)(BitNumIntr(S1, 6, 7) | BitNumIntr(S0, 6, 6) | BitNumIntr(S1, 14, 5) | BitNumIntr(S0, 14, 4) | BitNumIntr(S1, 22, 3) | BitNumIntr(S0, 22, 2) | BitNumIntr(S1, 30, 1) | BitNumIntr(S0, 30, 0));
			pData[1] = (uint8_t)(BitNumIntr(S1, 5, 7) | BitNumIntr(S0, 5, 6) | BitNumIntr(S1, 13, 5) | BitNumIntr(S0, 13, 4) | BitNumIntr(S1, 21, 3) | BitNumIntr(S0, 21, 2) | BitNumIntr(S1, 29, 1) | BitNumIntr(S0, 29, 0));
			pData[0] = (uint8_t)(BitNumIntr(S1, 4, 7) | BitNumIntr(S0, 4, 6) | BitNumIntr(S1, 12, 5) | BitNumIntr(S0, 12, 4) | BitNumIntr(S1, 20, 3) | BitNumIntr(S0, 20, 2) | BitNumIntr(S1, 28, 1) | BitNumIntr(S0, 28, 0));
			pData[7] = (uint8_t)(BitNumIntr(S1, 3, 7) | BitNumIntr(S0, 3, 6) | BitNumIntr(S1, 11, 5) | BitNumIntr(S0, 11, 4) | BitNumIntr(S1, 19, 3) | BitNumIntr(S0, 19, 2) | BitNumIntr(S1, 27, 1) | BitNumIntr(S0, 27, 0));
			pData[6] = (uint8_t)(BitNumIntr(S1, 2, 7) | BitNumIntr(S0, 2, 6) | BitNumIntr(S1, 10, 5) | BitNumIntr(S0, 10, 4) | BitNumIntr(S1, 18, 3) | BitNumIntr(S0, 18, 2) | BitNumIntr(S1, 26, 1) | BitNumIntr(S0, 26, 0));
			pData[5] = (uint8_t)(BitNumIntr(S1, 1, 7) | BitNumIntr(S0, 1, 6) | BitNumIntr(S1, 9, 5) | BitNumIntr(S0, 9, 4) | BitNumIntr(S1, 17, 3) | BitNumIntr(S0, 17, 2) | BitNumIntr(S1, 25, 1) | BitNumIntr(S0, 25, 0));
			pData[4] = (uint8_t)(BitNumIntr(S1, 0, 7) | BitNumIntr(S0, 0, 6) | BitNumIntr(S1, 8, 5) | BitNumIntr(S0, 8, 4) | BitNumIntr(S1, 16, 3) | BitNumIntr(S0, 16, 2) | BitNumIntr(S1, 24, 1) | BitNumIntr(S0, 24, 0));
		}

		uint32_t DesF(uint32_t State, const uint8_t *pKey)
		{
			const uint32_t T1 = BitNumIntl(State, 31, 0) | ((State & 0xF0000000U) >> 1) | BitNumIntl(State, 4, 5) | BitNumIntl(State, 3, 6) | ((State & 0x0F000000U) >> 3) | BitNumIntl(State, 8, 11) | BitNumIntl(State, 7, 12) | ((State & 0x00F00000U) >> 5) | BitNumIntl(State, 12, 17) | BitNumIntl(State, 11, 18) | ((State & 0x000F0000U) >> 7) | BitNumIntl(State, 16, 23);
			const uint32_t T2 = BitNumIntl(State, 15, 0) | ((State & 0x0000F000U) << 15) | BitNumIntl(State, 20, 5) | BitNumIntl(State, 19, 6) | ((State & 0x00000F00U) << 13) | BitNumIntl(State, 24, 11) | BitNumIntl(State, 23, 12) | ((State & 0x000000F0U) << 11) | BitNumIntl(State, 28, 17) | BitNumIntl(State, 27, 18) | ((State & 0x0000000FU) << 9) | BitNumIntl(State, 0, 23);
			const int Lrg[6] = {
				(int)(((T1 >> 24) & 0xFF) ^ pKey[0]),
				(int)(((T1 >> 16) & 0xFF) ^ pKey[1]),
				(int)(((T1 >> 8) & 0xFF) ^ pKey[2]),
				(int)(((T2 >> 24) & 0xFF) ^ pKey[3]),
				(int)(((T2 >> 16) & 0xFF) ^ pKey[4]),
				(int)(((T2 >> 8) & 0xFF) ^ pKey[5])};
			const uint32_t Sboxed =
				((uint32_t)SBOX[0][SboxBit(Lrg[0] >> 2)] << 28) |
				((uint32_t)SBOX[1][SboxBit(((Lrg[0] & 0x03) << 4) | (Lrg[1] >> 4))] << 24) |
				((uint32_t)SBOX[2][SboxBit(((Lrg[1] & 0x0F) << 2) | (Lrg[2] >> 6))] << 20) |
				((uint32_t)SBOX[3][SboxBit(Lrg[2] & 0x3F)] << 16) |
				((uint32_t)SBOX[4][SboxBit(Lrg[3] >> 2)] << 12) |
				((uint32_t)SBOX[5][SboxBit(((Lrg[3] & 0x03) << 4) | (Lrg[4] >> 4))] << 8) |
				((uint32_t)SBOX[6][SboxBit(((Lrg[4] & 0x0F) << 2) | (Lrg[5] >> 6))] << 4) |
				(uint32_t)SBOX[7][SboxBit(Lrg[5] & 0x3F)];
			return (BitNumIntl(Sboxed, 15, 0) | BitNumIntl(Sboxed, 6, 1) | BitNumIntl(Sboxed, 19, 2) | BitNumIntl(Sboxed, 20, 3) | BitNumIntl(Sboxed, 28, 4) | BitNumIntl(Sboxed, 11, 5) | BitNumIntl(Sboxed, 27, 6) | BitNumIntl(Sboxed, 16, 7) | BitNumIntl(Sboxed, 0, 8) | BitNumIntl(Sboxed, 14, 9) | BitNumIntl(Sboxed, 22, 10) | BitNumIntl(Sboxed, 25, 11) | BitNumIntl(Sboxed, 4, 12) | BitNumIntl(Sboxed, 17, 13) | BitNumIntl(Sboxed, 30, 14) | BitNumIntl(Sboxed, 9, 15) | BitNumIntl(Sboxed, 1, 16) | BitNumIntl(Sboxed, 7, 17) | BitNumIntl(Sboxed, 23, 18) | BitNumIntl(Sboxed, 13, 19) | BitNumIntl(Sboxed, 31, 20) | BitNumIntl(Sboxed, 26, 21) | BitNumIntl(Sboxed, 2, 22) | BitNumIntl(Sboxed, 8, 23) | BitNumIntl(Sboxed, 18, 24) | BitNumIntl(Sboxed, 12, 25) | BitNumIntl(Sboxed, 29, 26) | BitNumIntl(Sboxed, 5, 27) | BitNumIntl(Sboxed, 21, 28) | BitNumIntl(Sboxed, 10, 29) | BitNumIntl(Sboxed, 3, 30) | BitNumIntl(Sboxed, 24, 31)) & 0xFFFFFFFFU;
		}

		void DesCrypt(const uint8_t *pInput, const uint8_t *pKey16x6, uint8_t *pOutput)
		{
			uint32_t S0 = 0;
			uint32_t S1 = 0;
			InitialPermutation(pInput, &S0, &S1);
			for(int Round = 0; Round < 15; ++Round)
			{
				const uint32_t PrevS1 = S1;
				S1 = DesF(S1, pKey16x6 + Round * 6) ^ S0;
				S0 = PrevS1;
			}
			S0 = DesF(S1, pKey16x6 + 15 * 6) ^ S0;
			InversePermutation(S0, S1, pOutput);
		}

		constexpr uint8_t KEY_SHIFT[16] = {1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1};
		constexpr uint8_t KEY_PERM_C[28] = {56, 48, 40, 32, 24, 16, 8, 0, 57, 49, 41, 33, 25, 17, 9, 1, 58, 50, 42, 34, 26, 18, 10, 2, 59, 51, 43, 35};
		constexpr uint8_t KEY_PERM_D[28] = {62, 54, 46, 38, 30, 22, 14, 6, 61, 53, 45, 37, 29, 21, 13, 5, 60, 52, 44, 36, 28, 20, 12, 4, 27, 19, 11, 3};
		constexpr uint8_t KEY_COMP[48] = {13, 16, 10, 23, 0, 4, 2, 27, 14, 5, 20, 9, 22, 18, 11, 3, 25, 7, 15, 6, 26, 19, 12, 1, 40, 51, 30, 36, 46, 54, 29, 39, 50, 44, 32, 47, 43, 48, 38, 55, 33, 52, 45, 41, 49, 35, 28, 31};

		// 生成 16 轮子密钥,每轮 6 字节。Encrypt=true 时正向,否则反向。
		void DesKeySchedule(const uint8_t *pKey8, bool Encrypt, uint8_t *pSchedule16x6)
		{
			uint32_t C = 0;
			uint32_t D = 0;
			for(int i = 0; i < 28; ++i)
			{
				C |= BitNum(pKey8, KEY_PERM_C[i], 31 - i);
				D |= BitNum(pKey8, KEY_PERM_D[i], 31 - i);
			}
			for(int Round = 0; Round < 16; ++Round)
			{
				C = ((C << KEY_SHIFT[Round]) | (C >> (28 - KEY_SHIFT[Round]))) & 0xFFFFFFF0U;
				D = ((D << KEY_SHIFT[Round]) | (D >> (28 - KEY_SHIFT[Round]))) & 0xFFFFFFF0U;
				const int Togen = Encrypt ? Round : 15 - Round;
				uint8_t *pKey = pSchedule16x6 + Togen * 6;
				std::memset(pKey, 0, 6);
				for(int j = 0; j < 24; ++j)
					pKey[j / 8] |= (uint8_t)BitNumIntr(C, KEY_COMP[j], 7 - (j % 8));
				for(int j = 24; j < 48; ++j)
					pKey[j / 8] |= (uint8_t)BitNumIntr(D, KEY_COMP[j] - 27, 7 - (j % 8));
			}
		}

		void TripleDesCryptBlock(const uint8_t *pInput, const uint8_t *pKey24, bool Encrypt, uint8_t *pOutput)
		{
			// 3DES-EDE:加密 = E(K1) D(K2) E(K3);解密 = D(K3) E(K2) D(K1)。
			uint8_t aSchedules[3][16 * 6];
			if(Encrypt)
			{
				DesKeySchedule(pKey24 + 0, true, aSchedules[0]);
				DesKeySchedule(pKey24 + 8, false, aSchedules[1]);
				DesKeySchedule(pKey24 + 16, true, aSchedules[2]);
			}
			else
			{
				DesKeySchedule(pKey24 + 16, false, aSchedules[0]);
				DesKeySchedule(pKey24 + 8, true, aSchedules[1]);
				DesKeySchedule(pKey24 + 0, false, aSchedules[2]);
			}
			uint8_t aBlock[8];
			std::memcpy(aBlock, pInput, 8);
			for(int Pass = 0; Pass < 3; ++Pass)
			{
				uint8_t aOut[8];
				DesCrypt(aBlock, aSchedules[Pass], aOut);
				std::memcpy(aBlock, aOut, 8);
			}
			std::memcpy(pOutput, aBlock, 8);
		}

		void Qmc1Xor(std::vector<uint8_t> &Data)
		{
			for(size_t i = 0; i < Data.size(); ++i)
				Data[i] ^= i > 0x7FFF ? QMC1_KEY[(i % 0x7FFF) & 0x7F] : QMC1_KEY[i & 0x7F];
		}

		bool Inflate(const std::vector<uint8_t> &Input, std::string *pOut)
		{
			if(pOut == nullptr || Input.empty())
				return false;
			z_stream Stream{};
			Stream.next_in = const_cast<Bytef *>(Input.data());
			Stream.avail_in = (uInt)std::min<size_t>(Input.size(), UINT_MAX);
			if(inflateInit(&Stream) != Z_OK)
				return false;
			std::unique_ptr<z_stream, decltype(&inflateEnd)> StreamOwner(&Stream, inflateEnd);
			std::vector<uint8_t> Output;
			Output.resize(std::max<size_t>(Input.size() * 4 + 1024, 16384));
			// next_out 必须从缓冲起点开始,否则 total_out 与缓冲位置错位。
			Stream.next_out = Output.data();
			Stream.avail_out = (uInt)Output.size();
			bool Success = false;
			for(;;)
			{
				const size_t Used = Output.size() - Stream.avail_out;
				if(Stream.avail_out == 0)
				{
					Output.resize(Output.size() * 2);
					Stream.next_out = Output.data() + Used;
					Stream.avail_out = (uInt)(Output.size() - Used);
				}
				const int Ret = inflate(&Stream, Z_NO_FLUSH);
				if(Ret == Z_STREAM_END)
				{
					Success = true;
					break;
				}
				if(Ret != Z_OK && Ret != Z_BUF_ERROR)
					break;
				if(Stream.avail_in == 0 && Ret == Z_BUF_ERROR)
					break;
			}
			StreamOwner.reset();
			if(!Success)
				return false;
			pOut->assign((const char *)Output.data(), Stream.total_out);
			if(pOut->size() >= 3 && (uint8_t)(*pOut)[0] == 0xEF && (uint8_t)(*pOut)[1] == 0xBB && (uint8_t)(*pOut)[2] == 0xBF)
				pOut->erase(0, 3);
			return true;
		}

		std::string_view Trim(std::string_view Value)
		{
			while(!Value.empty() && std::isspace((unsigned char)Value.front()) != 0)
				Value.remove_prefix(1);
			while(!Value.empty() && std::isspace((unsigned char)Value.back()) != 0)
				Value.remove_suffix(1);
			return Value;
		}

		bool ParseUnsigned(std::string_view Value, int64_t *pOut)
		{
			if(pOut == nullptr || Value.empty())
				return false;
			int64_t Number = 0;
			const auto Result = std::from_chars(Value.data(), Value.data() + Value.size(), Number);
			if(Result.ec != std::errc{} || Result.ptr != Value.data() + Value.size() || Number < 0)
				return false;
			*pOut = Number;
			return true;
		}
	}

	bool QrcTripleDesDecrypt(const uint8_t *pData, size_t Size, const uint8_t *pKey24, std::vector<uint8_t> *pOut)
	{
		if(pOut == nullptr || pData == nullptr || pKey24 == nullptr || Size == 0)
			return false;
		pOut->clear();
		pOut->reserve((Size + 7) / 8 * 8);
		for(size_t Offset = 0; Offset < Size; Offset += 8)
		{
			uint8_t aBlock[8] = {};
			const size_t Chunk = std::min<size_t>(8, Size - Offset);
			std::memcpy(aBlock, pData + Offset, Chunk);
			uint8_t aOut[8];
			TripleDesCryptBlock(aBlock, pKey24, false, aOut);
			pOut->insert(pOut->end(), aOut, aOut + 8);
		}
		return true;
	}

	bool DecryptQrc(std::string_view Data, std::string *pOutText, std::string *pError)
	{
		if(pOutText == nullptr)
			return false;
		pOutText->clear();
		if(Data.size() < 11 || std::memcmp(Data.data(), QRC_MAGIC, 11) != 0)
		{
			if(pError)
				*pError = "bad qrc magic";
			return false;
		}
		// QMC1 XOR 的密钥索引按全局文件偏移计算(魔数部分也参与,但随后被丢弃)。
		std::vector<uint8_t> Body(Data.begin(), Data.end());
		Qmc1Xor(Body);
		Body.erase(Body.begin(), Body.begin() + 11);
		std::vector<uint8_t> Decrypted;
		if(!QrcTripleDesDecrypt(Body.data(), Body.size(), (const uint8_t *)QRC_KEY, &Decrypted))
		{
			if(pError)
				*pError = "qrc 3des failed";
			return false;
		}
		if(!Inflate(Decrypted, pOutText))
		{
			if(pError)
				*pError = "qrc inflate failed";
			return false;
		}
		return true;
	}

	bool ExtractQrcLyricContent(std::string_view Xml, std::string *pOut, std::string *pError)
	{
		if(pOut == nullptr)
			return false;
		pOut->clear();
		constexpr std::string_view Key = "LyricContent=\"";
		const size_t Start = Xml.find(Key);
		if(Start == std::string_view::npos)
		{
			if(pError)
				*pError = "LyricContent not found";
			return false;
		}
		const size_t ContentStart = Start + Key.size();
		const size_t End = Xml.find('"', ContentStart);
		if(End == std::string_view::npos)
		{
			if(pError)
				*pError = "LyricContent unterminated";
			return false;
		}
		const std::string_view Content = Xml.substr(ContentStart, End - ContentStart);
		// 反转义常见 XML 实体。
		for(size_t Offset = 0; Offset < Content.size();)
		{
			if(Content[Offset] != '&')
			{
				pOut->push_back(Content[Offset]);
				++Offset;
				continue;
			}
			const size_t Semicolon = Content.find(';', Offset + 1);
			if(Semicolon == std::string_view::npos || Semicolon - Offset > 8)
			{
				pOut->push_back(Content[Offset]);
				++Offset;
				continue;
			}
			const std::string_view Entity = Content.substr(Offset + 1, Semicolon - Offset - 1);
			if(Entity == "amp")
				pOut->push_back('&');
			else if(Entity == "lt")
				pOut->push_back('<');
			else if(Entity == "gt")
				pOut->push_back('>');
			else if(Entity == "quot")
				pOut->push_back('"');
			else if(Entity == "apos")
				pOut->push_back('\'');
			else if(!Entity.empty() && Entity[0] == '#')
			{
				int Code = 0;
				const auto Result = std::from_chars(Entity.data() + 1, Entity.data() + Entity.size(), Code);
				if(Result.ec == std::errc{} && Code > 0 && Code <= 0x10FFFF)
				{
					if(Code <= 0x7F)
						pOut->push_back((char)Code);
					else if(Code <= 0x7FF)
					{
						pOut->push_back((char)(0xC0 | (Code >> 6)));
						pOut->push_back((char)(0x80 | (Code & 0x3F)));
					}
					else if(Code <= 0xFFFF)
					{
						pOut->push_back((char)(0xE0 | (Code >> 12)));
						pOut->push_back((char)(0x80 | ((Code >> 6) & 0x3F)));
						pOut->push_back((char)(0x80 | (Code & 0x3F)));
					}
					else
					{
						pOut->push_back((char)(0xF0 | (Code >> 18)));
						pOut->push_back((char)(0x80 | ((Code >> 12) & 0x3F)));
						pOut->push_back((char)(0x80 | ((Code >> 6) & 0x3F)));
						pOut->push_back((char)(0x80 | (Code & 0x3F)));
					}
				}
				else
					pOut->push_back(Content[Offset]);
			}
			else
			{
				pOut->push_back(Content[Offset]);
				Offset = Semicolon;
				continue;
			}
			Offset = Semicolon + 1;
		}
		return !pOut->empty();
	}

	bool ParseQrcRlrc(std::string_view Text, NeteaseLyrics::STimeline *pOut, std::string *pError)
	{
		if(pOut == nullptr)
			return false;
		pOut->Clear();
		if(!NeteaseLyrics::IsValidUtf8(Text))
		{
			if(pError)
				*pError = "invalid UTF-8";
			return false;
		}
		std::vector<NeteaseLyrics::SLine> vLines;
		size_t Offset = 0;
		while(Offset <= Text.size())
		{
			const size_t Newline = Text.find('\n', Offset);
			std::string_view Line = Text.substr(Offset, Newline == std::string_view::npos ? Text.size() - Offset : Newline - Offset);
			if(!Line.empty() && Line.back() == '\r')
				Line.remove_suffix(1);
			Line = Trim(Line);
			if(Line.size() >= 3 && (uint8_t)Line[0] == 0xEF && (uint8_t)Line[1] == 0xBB && (uint8_t)Line[2] == 0xBF)
				Line.remove_prefix(3);
			// 跳过 [ti:]/[ar:]/[al:]/[offset:]/[kana:] 等元数据行(无数字起始时间)。
			if(Line.empty() || Line.front() != '[')
			{
				if(Newline == std::string_view::npos)
					break;
				Offset = Newline + 1;
				continue;
			}
			const size_t BracketEnd = Line.find(']');
			if(BracketEnd == std::string_view::npos)
			{
				if(Newline == std::string_view::npos)
					break;
				Offset = Newline + 1;
				continue;
			}
			const std::string_view Inside = Line.substr(1, BracketEnd - 1);
			const size_t Comma = Inside.find(',');
			if(Comma == std::string_view::npos)
			{
				// 元数据行如 [ti:xxx],跳过。
				if(Newline == std::string_view::npos)
					break;
				Offset = Newline + 1;
				continue;
			}
			int64_t StartMs = 0;
			int64_t DurationMs = 0;
			if(!ParseUnsigned(Trim(Inside.substr(0, Comma)), &StartMs) || !ParseUnsigned(Trim(Inside.substr(Comma + 1)), &DurationMs))
			{
				if(Newline == std::string_view::npos)
					break;
				Offset = Newline + 1;
				continue;
			}
			NeteaseLyrics::SLine Parsed;
			Parsed.m_StartMs = StartMs;
			Parsed.m_EndMs = DurationMs > 0 && StartMs <= std::numeric_limits<int64_t>::max() - DurationMs ? StartMs + DurationMs : -1;
			std::string_view Body = Line.substr(BracketEnd + 1);
			std::string PlainText;
			bool Malformed = false;
			size_t BodyOffset = 0;
			while(BodyOffset < Body.size())
			{
				const size_t Marker = Body.find('(', BodyOffset);
				if(Marker == std::string_view::npos)
				{
					PlainText.append(Body.substr(BodyOffset));
					break;
				}
				const std::string WordText(Body.substr(BodyOffset, Marker - BodyOffset));
				PlainText += WordText;
				const size_t PairClose = Body.find(')', Marker + 1);
				if(PairClose == std::string_view::npos)
				{
					Malformed = true;
					break;
				}
				int64_t WordOffsetMs = 0;
				int64_t WordDurationMs = 0;
				const std::string_view Pair = Body.substr(Marker + 1, PairClose - Marker - 1);
				const size_t PairComma = Pair.find(',');
				if(PairComma == std::string_view::npos || !ParseUnsigned(Trim(Pair.substr(0, PairComma)), &WordOffsetMs) || !ParseUnsigned(Trim(Pair.substr(PairComma + 1)), &WordDurationMs))
				{
					Malformed = true;
					break;
				}
				// QRC 时间标记跟在对应词文后，时间戳是绝对值（首词通常等于行起点）。
				if(WordDurationMs > 0 && WordOffsetMs >= 0)
				{
					if(WordOffsetMs <= std::numeric_limits<int64_t>::max() - WordDurationMs)
						Parsed.m_vWords.push_back({WordOffsetMs, WordOffsetMs + WordDurationMs, WordText});
				}
				BodyOffset = PairClose + 1;
			}
			if(!Malformed)
			{
				Parsed.m_Text = std::move(PlainText);
				if(!Parsed.m_Text.empty())
					vLines.push_back(std::move(Parsed));
			}
			if(Newline == std::string_view::npos)
				break;
			Offset = Newline + 1;
		}
		if(vLines.empty())
		{
			if(pError)
				*pError = "no valid timed lyric lines";
			return false;
		}
		NeteaseLyrics::FinalizeTimelineLines(vLines);
		pOut->m_vLines = std::move(vLines);
		pOut->m_HasTiming = true;
		return true;
	}

	bool ParseQrcData(std::string_view Data, SLyricsData *pOut, std::string *pError)
	{
		if(pOut == nullptr)
			return false;
		SLyricsData Result;
		std::string Xml;
		if(!DecryptQrc(Data, &Xml, pError))
			return false;
		std::string Content;
		if(!ExtractQrcLyricContent(Xml, &Content, pError))
			return false;
		if(!ParseQrcRlrc(Content, &Result.m_Timeline, pError))
			return false;
		*pOut = std::move(Result);
		return true;
	}
} // namespace QmMusicLyrics
