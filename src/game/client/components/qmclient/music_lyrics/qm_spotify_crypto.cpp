#include "qm_spotify_crypto.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace QmSpotifyCrypto
{
	namespace
	{
		// 标准 SHA-1(FIPS 180-1)。仅在进程内用于 TOTP 计算。
		struct CSha1
		{
			uint32_t m_aH[5];
			uint64_t m_LengthBits;
			unsigned char m_aBlock[64];
			size_t m_BlockUsed;

			void Init()
			{
				m_aH[0] = 0x67452301u;
				m_aH[1] = 0xEFCDAB89u;
				m_aH[2] = 0x98BADCFEu;
				m_aH[3] = 0x10325476u;
				m_aH[4] = 0xC3D2E1F0u;
				m_LengthBits = 0;
				m_BlockUsed = 0;
			}

			static uint32_t Rotl(uint32_t Value, unsigned int Bits)
			{
				return (Value << Bits) | (Value >> (32 - Bits));
			}

			void ProcessBlock(const unsigned char *pBlock)
			{
				uint32_t aW[80];
				for(unsigned int i = 0; i < 16; ++i)
				{
					aW[i] = ((uint32_t)pBlock[i * 4] << 24) | ((uint32_t)pBlock[i * 4 + 1] << 16) |
						((uint32_t)pBlock[i * 4 + 2] << 8) | (uint32_t)pBlock[i * 4 + 3];
				}
				for(unsigned int i = 16; i < 80; ++i)
					aW[i] = Rotl(aW[i - 3] ^ aW[i - 8] ^ aW[i - 14] ^ aW[i - 16], 1);

				uint32_t a = m_aH[0], b = m_aH[1], c = m_aH[2], d = m_aH[3], e = m_aH[4];
				for(unsigned int i = 0; i < 80; ++i)
				{
					uint32_t f;
					uint32_t k;
					if(i < 20)
					{
						f = (b & c) | (~b & d);
						k = 0x5A827999u;
					}
					else if(i < 40)
					{
						f = b ^ c ^ d;
						k = 0x6ED9EBA1u;
					}
					else if(i < 60)
					{
						f = (b & c) | (b & d) | (c & d);
						k = 0x8F1BBCDCu;
					}
					else
					{
						f = b ^ c ^ d;
						k = 0xCA62C1D6u;
					}
					const uint32_t Temp = Rotl(a, 5) + f + e + k + aW[i];
					e = d;
					d = c;
					c = Rotl(b, 30);
					b = a;
					a = Temp;
				}
				m_aH[0] += a;
				m_aH[1] += b;
				m_aH[2] += c;
				m_aH[3] += d;
				m_aH[4] += e;
			}

			void Update(const unsigned char *pData, size_t DataLen)
			{
				m_LengthBits += (uint64_t)DataLen * 8;
				while(DataLen > 0)
				{
					const size_t ToCopy = std::min(sizeof(m_aBlock) - m_BlockUsed, DataLen);
					memcpy(m_aBlock + m_BlockUsed, pData, ToCopy);
					m_BlockUsed += ToCopy;
					pData += ToCopy;
					DataLen -= ToCopy;
					if(m_BlockUsed == sizeof(m_aBlock))
					{
						ProcessBlock(m_aBlock);
						m_BlockUsed = 0;
					}
				}
			}

			void Final(unsigned char aDigest[20])
			{
				const uint64_t LengthBits = m_LengthBits;
				const unsigned char Padding = 0x80;
				Update(&Padding, 1);
				const unsigned char aZero[8] = {0, 0, 0, 0, 0, 0, 0, 0};
				while(m_BlockUsed != 56)
					Update(aZero, 1);
				unsigned char aLength[8];
				for(unsigned int i = 0; i < 8; ++i)
					aLength[i] = (unsigned char)(LengthBits >> (56 - i * 8));
				Update(aLength, 8);
				for(unsigned int i = 0; i < 5; ++i)
				{
					aDigest[i * 4] = (unsigned char)(m_aH[i] >> 24);
					aDigest[i * 4 + 1] = (unsigned char)(m_aH[i] >> 16);
					aDigest[i * 4 + 2] = (unsigned char)(m_aH[i] >> 8);
					aDigest[i * 4 + 3] = (unsigned char)m_aH[i];
				}
			}
		};

		std::string Sha1(const unsigned char *pData, size_t DataLen)
		{
			CSha1 Ctx;
			Ctx.Init();
			Ctx.Update(pData, DataLen);
			unsigned char aDigest[20];
			Ctx.Final(aDigest);
			return std::string((const char *)aDigest, sizeof(aDigest));
		}
	} // namespace

	std::string HmacSha1(const unsigned char *pKey, size_t KeyLen, const unsigned char *pData, size_t DataLen)
	{
		// RFC 2104:key 超过块长先做 SHA-1 压缩,再左右填充。
		unsigned char aKey[64] = {0};
		if(KeyLen > sizeof(aKey))
		{
			const std::string Hashed = Sha1(pKey, KeyLen);
			memcpy(aKey, Hashed.data(), Hashed.size());
		}
		else
		{
			memcpy(aKey, pKey, KeyLen);
		}

		unsigned char aInnerPad[64];
		unsigned char aOuterPad[64];
		for(size_t i = 0; i < sizeof(aKey); ++i)
		{
			aInnerPad[i] = aKey[i] ^ 0x36;
			aOuterPad[i] = aKey[i] ^ 0x5C;
		}

		CSha1 Inner;
		Inner.Init();
		Inner.Update(aInnerPad, sizeof(aInnerPad));
		Inner.Update(pData, DataLen);
		unsigned char aInnerDigest[20];
		Inner.Final(aInnerDigest);

		CSha1 Outer;
		Outer.Init();
		Outer.Update(aOuterPad, sizeof(aOuterPad));
		Outer.Update(aInnerDigest, sizeof(aInnerDigest));
		unsigned char aOuterDigest[20];
		Outer.Final(aOuterDigest);
		return std::string((const char *)aOuterDigest, sizeof(aOuterDigest));
	}
} // namespace QmSpotifyCrypto
