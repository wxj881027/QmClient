#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_VOTE_OPTION_PACKET_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_VOTE_OPTION_PACKET_H

#include <engine/shared/packer.h>

// Reader使用GetString/Error接口；只消费有效前缀，首个解包错误后不再读。
template<typename TReader, typename TConsume>
int ReadQmVoteOptions(TReader &Reader, int NumOptions, TConsume &&Consume)
{
	int Parsed = 0;
	for(; Parsed < NumOptions; ++Parsed)
	{
		const char *pDescription = Reader.GetString(CUnpacker::SANITIZE_CC);
		if(Reader.Error())
			break;
		Consume(Parsed, pDescription);
	}
	return Parsed;
}

#endif
