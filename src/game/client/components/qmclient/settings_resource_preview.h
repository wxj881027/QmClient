// 请抬头享受阳光｜日子很好 我很我---------致咩子
#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_RESOURCE_PREVIEW_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_RESOURCE_PREVIEW_H

#include <base/lock.h>

#include <engine/client/gpu_upload_limiter.h>
#include <engine/graphics.h>
#include <engine/image.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/components/settings_resource_jobs.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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

	bool operator==(const SResourcePreviewKey &Other) const;
};

struct SResourcePreviewKeyHash
{
	size_t operator()(const SResourcePreviewKey &Key) const;
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

	void EnqueueUpload(const SResourcePreviewKey &Key, CImageInfo &&Image, const char *pDebugName);
	void EnqueueUploadToTarget(const SResourcePreviewKey &Key, CImageInfo &&Image, TUploadFinalize &&Finalize, const char *pDebugName);
	bool DrainOne(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics);
	int Drain(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics);
	size_t QueueDepth() const { return m_vUploadQueue.size(); }
	void Clear();

private:
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

	std::deque<SUploadItem> m_vUploadQueue;
};

class CSettingsResourcePreviewJob : public IJob
{
public:
	struct SResult
	{
		SResourcePreviewArtifact m_Artifact;
	};

	CSettingsResourcePreviewJob(std::string Name, CImageInfo &&Image, int TargetSize);
	static std::shared_ptr<CSettingsResourcePreviewJob> FromPath(std::string Name, std::string Path, IStorage *pStorage, int StorageType, int TargetSize);

	bool Completed() const REQUIRES(!m_Lock);
	SResult TakeResult() REQUIRES(!m_Lock);

protected:
	void Run() override REQUIRES(!m_Lock);

private:
	std::string m_Name;
	std::string m_Path;
	IStorage *m_pStorage = nullptr;
	int m_StorageType = IStorage::TYPE_ALL;
	CImageInfo m_InputImage;
	int m_TargetSize = 0;
	mutable CLock m_Lock;
	SResult m_Result;
	bool m_Completed = false;
};

SResourcePreviewArtifact BuildPreviewArtifact(CImageInfo &&Image, int TargetSize);
SResourcePreviewArtifact BuildPreviewArtifactFromPath(const char *pPath, IStorage *pStorage, int StorageType, const char *pContextName, int TargetSize);
bool SettingsResourcePreviewImageValidForUpload(const CImageInfo &Image);
bool SettingsResourcePreviewConsumeUploadBudget(SResourcePreviewUploadBudget &Budget, int Count = 1);
void SettingsResourcePreviewCommitUploadBudget(SResourcePreviewUploadBudget &Budget, int Count = 1);
ESettingsResourcePreviewDrawResult SettingsResourcePreviewDrawResult(const SResourcePreviewState &State);
float SettingsResourcePreviewVisibleReadyRatio(int ReadyTextureCount, int VisibleCount);

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_SETTINGS_RESOURCE_PREVIEW_H
