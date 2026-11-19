#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_JOBS_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_TRANSLATE_TRANSLATE_JOBS_H

#include "translate_backend.h"

#include <base/borrowed_allocator.h>

#include <functional>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct STranslateJob
{
	std::unique_ptr<ITranslateBackend> m_pBackend;
	std::shared_ptr<CTranslateResponse> m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	int m_LineIndex = -1;
	unsigned int m_TranslationId = 0;
	bool m_Outgoing = false;
	bool m_AutoTriggered = false;
	// 只控制服务器回显下方的本地原文附注，绝不作为聊天发送内容。
	bool m_DisplayOriginal = true;
	int m_Connection = 0;
	int m_LocalClientId = -1;
	int m_Team = 0;
	char m_aTarget[16] = "";
	std::string m_OriginalText;
	std::string m_AddressPrefix;

	// pBody 指向 pText 内的正文起点。后端只接收正文，前缀随任务保留。
	void StartChatTranslation(IHttp &Http, const char *pText, const char *pBody, const char *pSource, FTranslateRequestFactory pCreateRequest = CreateHttpRequest);
};

struct STranslateCompletion
{
	STranslateJob m_Job;
	bool m_Success;
	// 仅成功译文可作为发送内容；失败、拒绝、取消均不回退发送原文。
	std::basic_string<char, std::char_traits<char>, CBorrowedAllocator<char>> m_SendText;
};

struct STranslateSendOptions
{
	bool m_AllowTranslation = true;
	bool m_TranslateOnce = false;
};

// 临时反转只决定当前消息的发送方式，不修改自动翻译配置。
STranslateSendOptions ResolveTranslateSendOptions(bool AutoOutgoing, bool InvertTranslation);

// 显式语言后缀优先；单次翻译使用发送目标，空输入和命令不进入翻译。
// pFallbackTarget 为空时，仅识别显式语言后缀。
bool PrepareOutgoingTranslateRequest(const char *pLine, const char *pFallbackTarget, std::string &Text, std::string &Target);

// 分发器只调用一次译文发送入口，原文通过独立的本地附注参数传递。
bool DispatchOutgoingTranslation(const STranslateCompletion &Completed, const std::function<bool(int, const char *, const char *, int)> &SendTranslated);

struct SOutgoingTranslateText
{
	std::string_view m_Text;
	std::string_view m_Target;
};

// 返回值借用输入，仅在调用期间创建任务时使用，不跨帧保存。
std::optional<SOutgoingTranslateText> ParseOutgoingTranslateTarget(const char *pLine);

// 等待、成功和自动重复请求都保留现有响应。只有手动请求可以替换失败响应。
bool TranslateResponseBlocksRequest(const CTranslateResponse *pResponse, bool AutoTriggered = false);

// 收发任务共用容量，完成或取消时统一释放后端。
class CTranslateJobQueue
{
	struct SPending
	{
		STranslateJob m_Job;
		// 后端只写入任务私有响应。结果存储失败时保留结果，避免再次轮询。
		CTranslateResponse m_Response;
		bool m_Done = false;
	};
	std::vector<SPending, CBorrowedAllocator<SPending>> m_vJobs;

public:
	// 自定义队列内存源须存活到队列销毁，不影响完成结果的独立内存域。
	explicit CTranslateJobQueue(IAllocationResource *pMemory = nullptr) :
		m_vJobs(pMemory) {}
	bool CanSubmit(int Limit) const;
	// 拒绝或扩容失败均释放传入任务，不发布响应或进度状态。
	bool Submit(STranslateJob Job, int Limit);
	// 创建回调不得重入队列或发布UI；构造失败与入队失败共用拒绝路径。
	template<typename FCreateJob>
	bool CreateAndSubmit(FCreateJob &&CreateJob, int Limit)
	{
		if(!CanSubmit(Limit))
			return false;
		try
		{
			return Submit(std::forward<FCreateJob>(CreateJob)(), Limit);
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
	}
	// 入队成功后才发布新响应，重试提交失败时旧响应仍属于聊天行。
	template<typename FCreateJob>
	bool CreateAndSubmitIncoming(std::shared_ptr<CTranslateResponse> &pCurrentResponse, FCreateJob &&CreateJob, int Limit)
	{
		std::shared_ptr<CTranslateResponse> pResponse;
		const bool Accepted = CreateAndSubmit([&] {
			auto Job = std::forward<FCreateJob>(CreateJob)();
			pResponse = Job.m_pTranslateResponse;
			return Job;
		},
			Limit);
		if(Accepted)
			pCurrentResponse = std::move(pResponse);
		return Accepted;
	}
	size_t Size() const { return m_vJobs.size(); }
	void Clear() { m_vJobs.clear(); }
	// 取消入站任务时通知聊天行清除进度，回调不得重入本队列。
	template<typename FCancelIncoming>
	void Clear(FCancelIncoming &&CancelIncoming)
	{
		for(const auto &Pending : m_vJobs)
			if(!Pending.m_Job.m_Outgoing)
				CancelIncoming(Pending.m_Job);
		Clear();
	}
	void CancelOutgoing(int Connection);
	// 自定义结果内存源须存活到返回列表及其发送文本销毁；回调不得重入本队列。
	std::vector<STranslateCompletion, CBorrowedAllocator<STranslateCompletion>> Update(const std::function<bool(const STranslateJob &)> &IncomingStillValid, IAllocationResource *pResultMemory = nullptr);
};

bool IsTranslateResponseCurrent(bool Initialized, unsigned int TranslationId, const std::shared_ptr<CTranslateResponse> &pResponse, const STranslateJob &Job);
// 仅解除当前响应关联，返回原响应是否有需要刷新的进度文本。
bool ClearCanceledTranslateResponse(bool Initialized, unsigned int TranslationId, std::shared_ptr<CTranslateResponse> &pResponse, const STranslateJob &Job);

bool IsTranslatePlayerCandidate(int ClientId, bool Initialized, bool HasText, bool ResponseBlocks, const int *pLocalIds, size_t NumLocalIds);
// 无效候选返回负分；相同分数保留最新消息，精确名字匹配优先于大小写兼容匹配。
int SelectTranslateHistoryLine(int CurrentLine, int NumLines, const std::function<int(int)> &CandidateScore);
int TranslateNameMatchScore(const char *pRequested, const char *pDisplayName, const char *pPlayerName);

#endif
