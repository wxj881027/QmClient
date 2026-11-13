#ifndef ENGINE_SHARED_STUN_H
#define ENGINE_SHARED_STUN_H
#include <cstddef>

struct NETADDR;

class CStunData
{
public:
	unsigned char m_aSecret[12];
};

size_t StunMessagePrepare(unsigned char *pBuffer, size_t BufferSize, CStunData *pData);
// 已生成的事务编号用于确定性编码；普通调用继续使用上面的随机准备入口。
size_t StunMessagePrepare(unsigned char *pBuffer, size_t BufferSize, const CStunData &Data);
// true表示未识别/损坏，输出保持false/空地址；false表示完整响应，pSuccess区分成功/错误。
// 调用方另行核对UDP来源，pData只绑定本次事务编号。
bool StunMessageParse(const unsigned char *pMessage, size_t MessageSize, const CStunData *pData, bool *pSuccess, NETADDR *pAddr);
#endif // ENGINE_SHARED_STUN_H
