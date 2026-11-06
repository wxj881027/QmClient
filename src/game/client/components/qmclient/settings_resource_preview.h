// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_RESOURCE_PREVIEW_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_RESOURCE_PREVIEW_H

#include <base/borrowed_allocator.h>
#include <base/lock.h>

#include <engine/client/gpu_upload_limiter.h>
#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/assets_preview_scale.h>
#include <game/client/components/settings_resource_jobs.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum class ESettingsResourcePreviewPriority
{
	BACKGROUND,
	NEAR_VISIBLE,
	VISIBLE,
};

enum class ESettingsResourcePreviewDrawResult
{
	PLACEHOLDER,
	READY_TEXTURE,
	FAILED_PLACEHOLDER,
};

struct SResourcePreviewKey
{
	std::string m_Type;
	std::string m_Id;
	bool m_Workshop = false;
	uint64_t m_LocaleHash = 0;
	int m_UiScale = 0;
	int m_CardWidth = 0;
	uint64_t m_ResourceGeneration = 0;

	bool operator==(const SResourcePreviewKey &Other) const;
};

struct SResourcePreviewKeyHash
{
	size_t operator()(const SResourcePreviewKey &Key) const;
};

// 只在主线程同步清理期间借用，字符串不能来自即将删除的 key。
struct SResourcePreviewInvalidationScope
{
	const char *m_pType = nullptr;
	const char *m_pId = nullptr;
	std::optional<bool> m_Workshop;

	bool Matches(const SResourcePreviewKey &Key) const;
};

struct SResourcePreviewState
{
	bool m_ShellReady = true;
	bool m_MetadataReady = false;
	bool m_ArtifactReady = false;
	bool m_PreviewJobPending = false;
	bool m_UploadPending = false;
	bool m_TextureReady = false;
	bool m_Failed = false;
	IGraphics::CTextureHandle m_Texture;
	// 预览贴图首次真正绘制的时刻（time_get() 刻度，0=未绘制过）：资产页贴图
	// 加载完成后以此为起点播 0.25s 出场淡入。本结构在预览缓存里持久存在，
	// 出场时间戳必须放这里——绘制侧的 PreviewState 是每帧局部变量，在那里
	// 盖时间戳会每帧清零，alpha 永远停在 0（实测整页预览消失的回归）。
	int64_t m_AppearStart = 0;
};

struct SResourcePreviewUploadBudget
{
	int m_MaxUploads = 0;
	int m_UploadsUsed = 0;
	SSettingsResourceMergeBudget *m_pMergeBudget = nullptr;
	SSettingsWarmupFrameBudget *m_pFrameBudget = nullptr;
	CGpuUploadLimiter *m_pGpuUploadLimiter = nullptr;
};

struct SResourcePreviewTelemetry
{
	int m_PreviewJobsStarted = 0;
	int m_PreviewJobsDone = 0;
	int m_PreviewUploads = 0;
	int m_PreviewAdmissions = 0;
	double m_PreviewArtifactMs = 0.0;
	double m_MetadataHydrateMs = 0.0;
	int m_PlaceholderCount = 0;
	int m_ReadyTextureCount = 0;
	int m_VisibleCount = 0;
	int m_UploadQueueDepth = 0;
	int m_UploadBudgetExhausted = 0;
};

struct SResourcePreviewArtifact
{
	SResourcePreviewArtifact() = default;
	SResourcePreviewArtifact(SResourcePreviewArtifact &&) = default;
	SResourcePreviewArtifact &operator=(SResourcePreviewArtifact &&) = default;
	// 结果未进入上传队列或被调用方丢弃时，也回收仍持有的CPU像素。
	~SResourcePreviewArtifact() { m_Image.Free(); }
	CImageInfo m_Image;
	bool m_Success = false;
	double m_DurationMs = 0.0;
};

class CSettingsResourcePreviewCache
{
public:
	SResourcePreviewState &GetOrCreate(const SResourcePreviewKey &Key);
	const SResourcePreviewState *Find(const SResourcePreviewKey &Key) const;
	void MarkMetadataReady(const SResourcePreviewKey &Key);
	void MarkPreviewJobStarted(const SResourcePreviewKey &Key);
	void MarkPreviewJobDone(const SResourcePreviewKey &Key, bool Success);
	void MarkArtifactReady(const SResourcePreviewKey &Key);
	void MarkTextureReady(const SResourcePreviewKey &Key, IGraphics::CTextureHandle Texture = IGraphics::CTextureHandle(), IGraphics *pGraphics = nullptr);
	void MarkUploadFailed(const SResourcePreviewKey &Key);
	void Clear(IGraphics *pGraphics = nullptr);
	void Invalidate(const SResourcePreviewInvalidationScope &Scope, IGraphics *pGraphics);
	size_t Size() const { return m_vStates.size(); }

private:
	std::unordered_map<SResourcePreviewKey, SResourcePreviewState, SResourcePreviewKeyHash> m_vStates;
};

class CSettingsResourcePreviewScheduler
{
public:
	void BeginFrame(int VisibleBudget, int NearVisibleBudget, int BackgroundBudget, int UploadBudget);
	void SetShellOnlyFrame(bool ShellOnlyFrame);
	bool CanHydrateMetadata(ESettingsResourcePreviewPriority Priority);
	bool CanStartPreviewJob(ESettingsResourcePreviewPriority Priority);
	bool CanUploadPreview();
	bool ShellOnlyFrame() const { return m_ShellOnlyFrame; }

private:
	bool ConsumePriorityBudget(ESettingsResourcePreviewPriority Priority);

	int m_VisibleBudget = 0;
	int m_NearVisibleBudget = 0;
	int m_BackgroundBudget = 0;
	int m_UploadBudget = 0;
	bool m_ShellOnlyFrame = false;
};

class CSettingsResourcePreviewUploadScheduler
{
public:
	using TUploadFinalize = std::function<void(bool TextureValid, IGraphics::CTextureHandle Texture)>;

	explicit CSettingsResourcePreviewUploadScheduler(IAllocationResource *pMemory = nullptr) :
		m_vUploadQueue(pMemory) {}
	bool EnqueueUpload(const SResourcePreviewKey &Key, CImageInfo &&Image, const char *pDebugName);
	// 回调捕获与包装也在准备阶段构造，失败时不接管调用方的图像。
	template<typename FPrepareFinalize>
	bool EnqueueUploadToTarget(const SResourcePreviewKey &Key, CImageInfo &&Image, FPrepareFinalize &&PrepareFinalize, const char *pDebugName)
	{
		TUploadFinalize Finalize;
		try
		{
			Finalize = PrepareFinalize();
		}
		catch(const std::bad_alloc &)
		{
			return false;
		}
		catch(const std::length_error &)
		{
			return false;
		}
		return EnqueuePrepared(Key, std::move(Image), std::move(Finalize), pDebugName);
	}
	bool DrainOne(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics);
	int Drain(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics);
	size_t QueueDepth() const { return m_vUploadQueue.size(); }
	void Clear();
	void Invalidate(const SResourcePreviewInvalidationScope &Scope);

private:
	bool EnqueuePrepared(const SResourcePreviewKey &Key, CImageInfo &&Image, TUploadFinalize &&Finalize, const char *pDebugName);
	struct SUploadItem
	{
		SUploadItem() = default;
		SUploadItem(SUploadItem &&) = default;
		SUploadItem &operator=(SUploadItem &&) = default;
		SUploadItem(const SUploadItem &) = delete;
		SUploadItem &operator=(const SUploadItem &) = delete;
		// 排队图像在取消、清空或调度器析构时也必须释放；成功移动后指针为空。
		~SUploadItem() { m_Image.Free(); }
		SResourcePreviewKey m_Key;
		CImageInfo m_Image;
		std::string m_DebugName;
		TUploadFinalize m_Finalize;
	};

	std::deque<SUploadItem, CBorrowedAllocator<SUploadItem>> m_vUploadQueue;
};

class CSettingsResourcePreviewJob : public IJob
{
public:
	struct SResult
	{
		SResourcePreviewArtifact m_Artifact;
	};

	CSettingsResourcePreviewJob(std::string Name, CImageInfo &&Image, int TargetSize);
	~CSettingsResourcePreviewJob() override { m_InputImage.Free(); }
	static std::shared_ptr<CSettingsResourcePreviewJob> FromPath(std::string Name, std::string Path, IStorage *pStorage, int StorageType, int TargetSize,
		size_t MaxFileBytes = LOCAL_ASSET_PREVIEW_MAX_FILE_SIZE, size_t MaxDecodedBytes = ASSET_PREVIEW_MAX_DECODED_BYTES);

	bool Completed() const REQUIRES(!m_Lock);
	SResult TakeResult() REQUIRES(!m_Lock);
	void CancelPublication() REQUIRES(!m_Lock);
	// 仅供主线程发布结果；消费者准备失败时不取走结果，也不允许重入此 job。
	template<typename FConsume>
	bool TryPublishResult(FConsume &&Consume) REQUIRES(!m_Lock)
	{
		const CLockScope Lock(m_Lock);
		if(m_PublicationCancelled || m_ResultTaken)
			return true;
		if(!m_Completed && !Finished())
			return false;
		SResult Failure;
		if(!Consume(m_Completed ? m_Result : Failure))
			return false;
		m_Result = SResult();
		m_ResultTaken = true;
		return true;
	}

protected:
	void Run() override REQUIRES(!m_Lock);

private:
	std::string m_Name;
	std::string m_Path;
	IStorage *m_pStorage = nullptr;
	int m_StorageType = IStorage::TYPE_ALL;
	CImageInfo m_InputImage;
	int m_TargetSize = 0;
	size_t m_MaxFileBytes = LOCAL_ASSET_PREVIEW_MAX_FILE_SIZE;
	size_t m_MaxDecodedBytes = ASSET_PREVIEW_MAX_DECODED_BYTES;
	mutable CLock m_Lock;
	SResult m_Result;
	bool m_Completed = false;
	bool m_ResultTaken = false;
	bool m_PublicationCancelled = false;
};

bool TryPublishSettingsResourcePreviewJob(CSettingsResourcePreviewJob &Job, const SResourcePreviewKey &Key, CSettingsResourcePreviewCache &Cache, CSettingsResourcePreviewUploadScheduler &Uploads, SResourcePreviewTelemetry &Telemetry, uint64_t CurrentGeneration = 0);

// 代际先推进，再关闭发布并清理跟踪；worker 输入仍由 pool owner 保活，不阻塞等待。
// 代际区间及匹配字符串由调用方保证在同步调用期间有效，空区间用于单个身份移除。
template<typename TJobs>
void InvalidateSettingsResourcePreviews(CSettingsResourcePreviewCache &Cache, CSettingsResourcePreviewUploadScheduler &Uploads, TJobs &Jobs, IGraphics *pGraphics, const SResourcePreviewInvalidationScope &Scope = {}, uint64_t *pGenerations = nullptr, size_t GenerationCount = 0)
{
	for(size_t Index = 0; Index < GenerationCount; ++Index)
		++pGenerations[Index];
	for(auto It = Jobs.begin(); It != Jobs.end();)
	{
		if(!Scope.Matches(It->first))
		{
			++It;
			continue;
		}
		if(It->second)
			It->second->CancelPublication();
		It = Jobs.erase(It);
	}
	Uploads.Invalidate(Scope);
	Cache.Invalidate(Scope, pGraphics);
}

// 跟踪节点先准备，接收失败撤回；缓存等待标记只在引擎确认后发布。
template<typename TJobs, typename FCreate>
bool TryStartSettingsResourcePreviewJob(CSettingsResourcePreviewCache &Cache, TJobs &Jobs, const SResourcePreviewKey &Key, IEngine &Engine, FCreate CreateJob)
{
	auto Tracked = Jobs.end();
	bool Linked = false;
	try
	{
		auto &State = Cache.GetOrCreate(Key);
		if(State.m_TextureReady || State.m_PreviewJobPending || State.m_UploadPending || Jobs.find(Key) != Jobs.end())
			return false;
		auto pCandidate = CreateJob();
		if(!pCandidate)
			return false;
		auto Inserted = Jobs.emplace(Key, pCandidate);
		if(!Inserted.second)
			return false;
		Tracked = Inserted.first;
		Linked = true;
		if(!Engine.TryAddJob(pCandidate))
		{
			Jobs.erase(Tracked);
			return false;
		}
		State.m_PreviewJobPending = true;
		State.m_Failed = false;
		return true;
	}
	catch(const std::bad_alloc &)
	{
		if(Linked)
			Jobs.erase(Tracked);
		return false;
	}
}

SResourcePreviewArtifact BuildPreviewArtifact(CImageInfo &&Image, int TargetSize);
SResourcePreviewArtifact BuildPreviewArtifactFromPath(const char *pPath, IStorage *pStorage, int StorageType, const char *pContextName, int TargetSize,
	size_t MaxFileBytes = LOCAL_ASSET_PREVIEW_MAX_FILE_SIZE, size_t MaxDecodedBytes = ASSET_PREVIEW_MAX_DECODED_BYTES);
bool SettingsResourcePreviewImageValidForUpload(const CImageInfo &Image);
bool SettingsResourcePreviewConsumeUploadBudget(SResourcePreviewUploadBudget &Budget, int Count = 1);
void SettingsResourcePreviewCommitUploadBudget(SResourcePreviewUploadBudget &Budget, int Count = 1);
ESettingsResourcePreviewDrawResult SettingsResourcePreviewDrawResult(const SResourcePreviewState &State);
float SettingsResourcePreviewVisibleReadyRatio(int ReadyTextureCount, int VisibleCount);

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_RESOURCE_PREVIEW_H
