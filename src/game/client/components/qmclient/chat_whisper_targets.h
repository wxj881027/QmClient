#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_WHISPER_TARGETS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_CHAT_WHISPER_TARGETS_H

// 私聊目标列表：只保留服务器上的「其他玩家」。
// 手机端从聊天输入行前缀弹出发送对象，主/副连接都算自己，不能出现在待选目标里；
// 名字为空或已不活跃的条目也不列，避免选中后 /w 发不出去。
struct SQmChatWhisperCandidate
{
	int m_ClientId = -1;
	const char *m_pName = nullptr;
	bool m_Active = false;
	bool m_Local = false;
};

struct SQmChatWhisperTarget
{
	int m_ClientId = -1;
	const char *m_pName = nullptr;
};

// 保持输入顺序（服务器按名字排序），不做二次重排。
inline int QmChatCollectWhisperTargets(const SQmChatWhisperCandidate *pCandidates, int NumCandidates, SQmChatWhisperTarget *pOut, int MaxOut)
{
	if(pCandidates == nullptr || pOut == nullptr || MaxOut <= 0)
		return 0;

	int NumTargets = 0;
	for(int i = 0; i < NumCandidates && NumTargets < MaxOut; ++i)
	{
		const SQmChatWhisperCandidate &Candidate = pCandidates[i];
		if(!Candidate.m_Active || Candidate.m_Local)
			continue;
		if(Candidate.m_pName == nullptr || Candidate.m_pName[0] == '\0')
			continue;
		pOut[NumTargets].m_ClientId = Candidate.m_ClientId;
		pOut[NumTargets].m_pName = Candidate.m_pName;
		NumTargets++;
	}
	return NumTargets;
}

#endif
