#ifndef TEST_SUPPORT_QRC_TEST_FIXTURE_H
#define TEST_SUPPORT_QRC_TEST_FIXTURE_H

#include <cstdint>
#include <vector>

namespace QrcTestFixture
{
	// 由 Python 验证脚本(tmp/verify_qrc.py 算法)构造的固定向量:
	// 明文(XML 包裹 rlrc) -> zlib -> 3DES 加密 -> QMC1 XOR(全局偏移) -> 魔数头。
	// 明文:
	//   <?xml version="1.0" encoding="utf-8"?>
	//   <QrcInfos>
	//   <LyricInfo LyricCount="1">
	//   <Lyric_1 LyricType="1" LyricContent="[ti:测试歌曲]
	//   [ar:测试歌手]
	//   [0,4390]停(0,274)下(274,274)脚(548,274)步(822,274)吧(1096,274)
	//   [4390,2000]第二(4390,500)行(4890,600)歌(5490,700)词(6190,300)"/>
	//   </LyricInfo>
	//   </QrcInfos>
	inline constexpr char ENCRYPTED_RLRC_HEX[] =
		"9825b0ace3028368e8fc6c6e123cd43e97c7e195d1c1797a8163073d3aaea503778bf71928c92e810fb5acee6a06e752e8b378b9a58c5c04f6e262135f54cf16da5b938a440d187557cd781f8c237b0f63556bd9845001bf08e2af678cd9bbc9bbe1416f22325ab3ac570d4b44dc1d25d95f9966c4531604e3b3347ff66ee5392f9f822fc958e761b578e6439f6a77b017c68b3d14f2470374f502c0d9db630b3cabf1d55d4a3d4314e91051b6ed19bdea1218c73e41f0a8bcc85db0909ecb29e9d58f9c71380d125623ced5164e393e598a3816a7938efab653c84b4ad54bfdacfb385d873bbb33433a97163787ac7eec0f1687aed9d4d1a1aa22689539da06391ddcc3533f77d6206ee4";

	inline std::vector<uint8_t> FromHex(const char *pH)
	{
		std::vector<uint8_t> Result;
		while(pH[0] != '\0' && pH[1] != '\0')
		{
			auto Digit = [](char C) -> int {
				if(C >= '0' && C <= '9')
					return C - '0';
				if(C >= 'a' && C <= 'f')
					return C - 'a' + 10;
				if(C >= 'A' && C <= 'F')
					return C - 'A' + 10;
				return 0;
			};
			Result.push_back((uint8_t)((Digit(pH[0]) << 4) | Digit(pH[1])));
			pH += 2;
		}
		return Result;
	}
}

#endif
