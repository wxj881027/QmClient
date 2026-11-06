// 请抬头享受阳光｜日子很好 我很我---------致咩子
#include "settings_resource_preview.h"

#include "bounded_file_reader.h"

#include <base/math.h>
#include <base/system.h>

#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace
{
	void ResizePreviewArtifact(SResourcePreviewArtifact &Artifact, int TargetSize)
	{
		auto &Image = Artifact.m_Image;
		if(!SettingsResourcePreviewImageValidForUpload(Image))
			return;
		if(TargetSize > 0 && (Image.m_Width > (size_t)TargetSize || Image.m_Height > (size_t)TargetSize))
		{
			const float Scale = minimum((float)TargetSize / (float)Image.m_Width, (float)TargetSize / (float)Image.m_Height);
			const int NewWidth = maximum(1, round_to_int((float)Image.m_Width * Scale));
			const int NewHeight = maximum(1, round_to_int((float)Image.m_Height * Scale));
			ResizeImage(Image, NewWidth, NewHeight);
		}
		Artifact.m_Success = SettingsResourcePreviewImageValidForUpload(Image);
	}

	void FinishPreviewArtifact(SResourcePreviewArtifact &Artifact)
	{
		uint64_t ReadyBytes = 0;
		if(Artifact.m_Success && SettingsAssetMemoryReadyBytes(Artifact.m_Image, ReadyBytes) && ReadyBytes <= Artifact.m_MemoryLease.Bytes())
			Artifact.m_MemoryLease.Shrink(ReadyBytes);
		else
		{
			Artifact.m_Success = false;
			Artifact.m_Image.Free();
			Artifact.m_MemoryLease.Reset();
		}
	}
}

bool SResourcePreviewKey::operator==(const SResourcePreviewKey &Other) const
{
	return m_Workshop == Other.m_Workshop &&
	       m_LocaleHash == Other.m_LocaleHash &&
	       m_UiScale == Other.m_UiScale &&
	       m_CardWidth == Other.m_CardWidth &&
	       m_ResourceGeneration == Other.m_ResourceGeneration &&
	       m_Type == Other.m_Type &&
	       m_Id == Other.m_Id;
}

size_t SResourcePreviewKeyHash::operator()(const SResourcePreviewKey &Key) const
{
	size_t Hash = std::hash<std::string>{}(Key.m_Type);
	Hash ^= std::hash<std::string>{}(Key.m_Id) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
	Hash ^= std::hash<bool>{}(Key.m_Workshop) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
	Hash ^= std::hash<uint64_t>{}(Key.m_LocaleHash) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
	Hash ^= std::hash<int>{}(Key.m_UiScale) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
	Hash ^= std::hash<int>{}(Key.m_CardWidth) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
	Hash ^= std::hash<uint64_t>{}(Key.m_ResourceGeneration) + 0x9e3779b9 + (Hash << 6) + (Hash >> 2);
	return Hash;
}

bool SResourcePreviewInvalidationScope::Matches(const SResourcePreviewKey &Key) const
{
	return (m_pType == nullptr || Key.m_Type == m_pType) &&
	       (m_pId == nullptr || Key.m_Id == m_pId) &&
	       (!m_Workshop.has_value() || Key.m_Workshop == *m_Workshop);
}

SResourcePreviewState &CSettingsResourcePreviewCache::GetOrCreate(const SResourcePreviewKey &Key)
{
	return m_vStates[Key];
}

const SResourcePreviewState *CSettingsResourcePreviewCache::Find(const SResourcePreviewKey &Key) const
{
	const auto It = m_vStates.find(Key);
	if(It == m_vStates.end())
		return nullptr;
	return &It->second;
}

void CSettingsResourcePreviewCache::MarkMetadataReady(const SResourcePreviewKey &Key)
{
	GetOrCreate(Key).m_MetadataReady = true;
}

void CSettingsResourcePreviewCache::MarkPreviewJobStarted(const SResourcePreviewKey &Key)
{
	SResourcePreviewState &State = GetOrCreate(Key);
	State.m_PreviewJobPending = true;
	State.m_Failed = false;
}

void CSettingsResourcePreviewCache::MarkPreviewJobDone(const SResourcePreviewKey &Key, bool Success)
{
	SResourcePreviewState &State = GetOrCreate(Key);
	State.m_PreviewJobPending = false;
	State.m_ArtifactReady = Success;
	State.m_UploadPending = Success;
	State.m_Failed = !Success;
}

void CSettingsResourcePreviewCache::MarkArtifactReady(const SResourcePreviewKey &Key)
{
	SResourcePreviewState &State = GetOrCreate(Key);
	State.m_PreviewJobPending = false;
	State.m_ArtifactReady = true;
	State.m_UploadPending = true;
	State.m_Failed = false;
}

void CSettingsResourcePreviewCache::MarkTextureReady(const SResourcePreviewKey &Key, IGraphics::CTextureHandle Texture, IGraphics *pGraphics)
{
	SResourcePreviewState &State = GetOrCreate(Key);
	if(pGraphics != nullptr && State.m_Texture.IsValid())
		pGraphics->UnloadTexture(&State.m_Texture);
	State.m_UploadPending = false;
	State.m_TextureReady = Texture.IsValid();
	State.m_Texture = Texture;
	State.m_Failed = !Texture.IsValid();
}

void CSettingsResourcePreviewCache::MarkUploadFailed(const SResourcePreviewKey &Key)
{
	SResourcePreviewState &State = GetOrCreate(Key);
	State.m_UploadPending = false;
	State.m_TextureReady = false;
	State.m_Failed = true;
}

void CSettingsResourcePreviewCache::Clear(IGraphics *pGraphics)
{
	Invalidate({}, pGraphics);
}

void CSettingsResourcePreviewCache::Invalidate(const SResourcePreviewInvalidationScope &Scope, IGraphics *pGraphics)
{
	for(auto It = m_vStates.begin(); It != m_vStates.end();)
	{
		if(!Scope.Matches(It->first))
		{
			++It;
			continue;
		}
		if(pGraphics != nullptr && It->second.m_Texture.IsValid())
			pGraphics->UnloadTexture(&It->second.m_Texture);
		It = m_vStates.erase(It);
	}
}

void CSettingsResourcePreviewCache::DiscardInputs(const SResourcePreviewInvalidationScope &Scope)
{
	for(auto &[Key, State] : m_vStates)
	{
		if(!Scope.Matches(Key))
			continue;
		State.m_PreviewJobPending = false;
		State.m_ArtifactReady = false;
		State.m_UploadPending = false;
		State.m_Failed = false;
	}
}

void CSettingsResourcePreviewScheduler::BeginFrame(int VisibleBudget, int NearVisibleBudget, int BackgroundBudget, int UploadBudget)
{
	m_VisibleBudget = maximum(0, VisibleBudget);
	m_NearVisibleBudget = maximum(0, NearVisibleBudget);
	m_BackgroundBudget = maximum(0, BackgroundBudget);
	m_UploadBudget = maximum(0, UploadBudget);
	m_ShellOnlyFrame = false;
}

void CSettingsResourcePreviewScheduler::SetShellOnlyFrame(bool ShellOnlyFrame)
{
	m_ShellOnlyFrame = ShellOnlyFrame;
}

bool CSettingsResourcePreviewScheduler::ConsumePriorityBudget(ESettingsResourcePreviewPriority Priority)
{
	switch(Priority)
	{
	case ESettingsResourcePreviewPriority::VISIBLE:
		if(m_VisibleBudget <= 0)
			return false;
		--m_VisibleBudget;
		return true;
	case ESettingsResourcePreviewPriority::NEAR_VISIBLE:
		if(m_NearVisibleBudget <= 0)
			return false;
		--m_NearVisibleBudget;
		return true;
	case ESettingsResourcePreviewPriority::BACKGROUND:
		if(m_BackgroundBudget <= 0)
			return false;
		--m_BackgroundBudget;
		return true;
	}
	return false;
}

bool CSettingsResourcePreviewScheduler::CanHydrateMetadata(ESettingsResourcePreviewPriority Priority)
{
	if(m_ShellOnlyFrame)
		return Priority == ESettingsResourcePreviewPriority::VISIBLE && ConsumePriorityBudget(Priority);
	return ConsumePriorityBudget(Priority);
}

bool CSettingsResourcePreviewScheduler::CanStartPreviewJob(ESettingsResourcePreviewPriority Priority)
{
	if(m_ShellOnlyFrame)
		return false;
	return ConsumePriorityBudget(Priority);
}

bool CSettingsResourcePreviewScheduler::CanUploadPreview()
{
	if(m_ShellOnlyFrame || m_UploadBudget <= 0)
		return false;
	--m_UploadBudget;
	return true;
}

CSettingsResourcePreviewJob::CSettingsResourcePreviewJob(std::string Name, CImageInfo &&Image, int TargetSize, CSettingsAssetMemoryBudget *pBudget) :
	m_Name(std::move(Name)),
	m_TargetSize(TargetSize)
{
	// CImageInfo 没有自动释放析构，额度准备失败前不能接管调用方像素。
	if(SettingsResourcePreviewImageValidForUpload(Image) && !SettingsAssetMemoryTryAcquireImage(Image, m_InputMemoryLease, pBudget))
		throw std::bad_alloc();
	m_InputImage = std::move(Image);
}

std::shared_ptr<CSettingsResourcePreviewJob> CSettingsResourcePreviewJob::FromPath(std::string Name, std::string Path, IStorage *pStorage, int StorageType, int TargetSize, size_t MaxFileBytes, size_t MaxDecodedBytes, CSettingsAssetMemoryBudget *pBudget, ESettingsAssetImageAdmission *pAdmission)
{
	if(pAdmission != nullptr)
		*pAdmission = ESettingsAssetImageAdmission::FAILED;
	uint64_t WorkingBytes = 0;
	if(Path.empty() || pStorage == nullptr || !SettingsAssetMemoryComputeWorkingBytes(MaxFileBytes, MaxDecodedBytes, TargetSize, WorkingBytes))
		return nullptr;
	if(pAdmission != nullptr)
		*pAdmission = ESettingsAssetImageAdmission::DEFERRED;
	CSettingsAssetMemoryLease MemoryLease;
	if(!(pBudget != nullptr ? *pBudget : SettingsAssetMemoryDefaultBudget()).TryAcquire(WorkingBytes, MemoryLease))
		return nullptr;
	try
	{
		auto pJob = std::make_shared<CSettingsResourcePreviewJob>(std::move(Name), CImageInfo(), TargetSize, pBudget);
		pJob->m_Path = std::move(Path);
		pJob->m_pStorage = pStorage;
		pJob->m_StorageType = StorageType;
		pJob->m_MaxFileBytes = MaxFileBytes;
		pJob->m_MaxDecodedBytes = MaxDecodedBytes;
		pJob->m_InputMemoryLease = std::move(MemoryLease);
		if(pAdmission != nullptr)
			*pAdmission = ESettingsAssetImageAdmission::ACCEPTED;
		return pJob;
	}
	catch(const std::bad_alloc &)
	{
		return nullptr;
	}
	catch(const std::length_error &)
	{
		return nullptr;
	}
}

bool CSettingsResourcePreviewJob::Completed() const
{
	const CLockScope Lock(m_Lock);
	// 池收拢异常后已无 worker 写入，消费者仍需取走失败结果并释放等待槽。
	return m_PublicationCancelled || m_Completed || Finished();
}

CSettingsResourcePreviewJob::SResult CSettingsResourcePreviewJob::TakeResult()
{
	const CLockScope Lock(m_Lock);
	if(m_PublicationCancelled || !m_Completed || m_ResultTaken)
		return SResult();
	SResult Result = std::move(m_Result);
	m_Result = SResult();
	m_ResultTaken = true;
	return Result;
}

void CSettingsResourcePreviewJob::CancelPublication()
{
	const CLockScope Lock(m_Lock);
	m_PublicationCancelled = true;
	m_Result = SResult();
	// 输入可能仍在锁外被 worker 使用，不在这里清理它或等待物理完成。
}

void CSettingsResourcePreviewJob::Run()
{
	{
		const CLockScope Lock(m_Lock);
		if(m_PublicationCancelled)
		{
			m_InputImage.Free();
			m_InputMemoryLease.Reset();
			return;
		}
	}
	SResult Result;
	if(!m_Path.empty())
		Result.m_Artifact = BuildPreviewArtifactFromPath(m_Path.c_str(), m_pStorage, m_StorageType, m_Name.c_str(), m_TargetSize, m_MaxFileBytes, m_MaxDecodedBytes, nullptr, &m_InputMemoryLease);
	else
		Result.m_Artifact = BuildPreviewArtifact(std::move(m_InputImage), m_TargetSize, &m_InputMemoryLease);
	const CLockScope Lock(m_Lock);
	if(m_PublicationCancelled)
		return;
	m_Result = std::move(Result);
	m_Completed = true;
}

bool CSettingsResourcePreviewUploadScheduler::EnqueueUpload(const SResourcePreviewKey &Key, CImageInfo &&Image, const char *pDebugName, CSettingsAssetMemoryLease *pMemoryLease)
{
	if(Image.m_pData == nullptr)
		return false;
	return EnqueuePrepared(Key, std::move(Image), {}, pDebugName, pMemoryLease);
}

bool CSettingsResourcePreviewUploadScheduler::EnqueuePrepared(const SResourcePreviewKey &Key, CImageInfo &&Image, TUploadFinalize &&Finalize, const char *pDebugName, CSettingsAssetMemoryLease *pMemoryLease)
{
	if(Image.m_pData == nullptr)
	{
		if(Finalize)
			Finalize(false, IGraphics::CTextureHandle());
		return true;
	}
	try
	{
		CSettingsAssetMemoryLease PreparedLease;
		if(SettingsResourcePreviewImageValidForUpload(Image))
		{
			uint64_t ReadyBytes = 0;
			if(!SettingsAssetMemoryReadyBytes(Image, ReadyBytes))
				return false;
			if(pMemoryLease == nullptr || !pMemoryLease->HasReservation())
			{
				if(!SettingsAssetMemoryTryAcquireImage(Image, PreparedLease))
					return false;
			}
			else if(pMemoryLease->Bytes() < ReadyBytes)
				return false;
		}
		SUploadItem Item;
		Item.m_Key = Key;
		Item.m_DebugName = pDebugName != nullptr ? pDebugName : Key.m_Id;
		Item.m_Finalize = std::move(Finalize);
		// 所有字符串、回调和容器节点都准备成功后才转移像素；此前失败保留原图。
		m_vUploadQueue.push_back(std::move(Item));
		m_vUploadQueue.back().m_Image = std::move(Image);
		if(PreparedLease.HasReservation())
			m_vUploadQueue.back().m_MemoryLease = std::move(PreparedLease);
		else if(pMemoryLease != nullptr)
			m_vUploadQueue.back().m_MemoryLease = std::move(*pMemoryLease);
		return true;
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
}

bool TryPublishSettingsResourcePreviewJob(CSettingsResourcePreviewJob &Job, const SResourcePreviewKey &Key, CSettingsResourcePreviewCache &Cache, CSettingsResourcePreviewUploadScheduler &Uploads, SResourcePreviewTelemetry &Telemetry, uint64_t CurrentGeneration)
{
	if(Key.m_ResourceGeneration != CurrentGeneration)
	{
		Job.CancelPublication();
		return true;
	}
	try
	{
		return Job.TryPublishResult([&](CSettingsResourcePreviewJob::SResult &Result) {
			// 先保证缓存条目存在；入队之后的状态提交不再分配。
			Cache.GetOrCreate(Key);
			const bool Success = Result.m_Artifact.m_Success && Result.m_Artifact.m_Image.m_pData != nullptr;
			if(Success && !Uploads.EnqueueUpload(Key, std::move(Result.m_Artifact.m_Image), Key.m_Id.c_str(), &Result.m_Artifact.m_MemoryLease))
				return false;
			Cache.MarkPreviewJobDone(Key, Success);
			Telemetry.m_PreviewArtifactMs += Result.m_Artifact.m_DurationMs;
			++Telemetry.m_PreviewJobsDone;
			return true;
		});
	}
	catch(const std::bad_alloc &)
	{
		return false;
	}
	catch(const std::length_error &)
	{
		return false;
	}
}

bool CSettingsResourcePreviewUploadScheduler::DrainOne(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics)
{
	if(pGraphics == nullptr)
	{
		Telemetry.m_UploadQueueDepth = (int)m_vUploadQueue.size();
		Telemetry.m_UploadBudgetExhausted = m_vUploadQueue.empty() ? 0 : 1;
		return false;
	}
	if(m_vUploadQueue.empty())
	{
		Telemetry.m_UploadQueueDepth = 0;
		return false;
	}

	const bool ValidImage = SettingsResourcePreviewImageValidForUpload(m_vUploadQueue.front().m_Image);
	// 帧额度先在候选上准备，原生命令拒收时不扣掉真实上传次数。
	SResourcePreviewUploadBudget CandidateBudget = Budget;
	SSettingsResourceMergeBudget CandidateMerge = Budget.m_pMergeBudget != nullptr ? *Budget.m_pMergeBudget : SSettingsResourceMergeBudget();
	SSettingsWarmupFrameBudget CandidateFrame = Budget.m_pFrameBudget != nullptr ? *Budget.m_pFrameBudget : SSettingsWarmupFrameBudget();
	CandidateBudget.m_pMergeBudget = Budget.m_pMergeBudget != nullptr ? &CandidateMerge : nullptr;
	CandidateBudget.m_pFrameBudget = Budget.m_pFrameBudget != nullptr ? &CandidateFrame : nullptr;
	if(ValidImage && !SettingsResourcePreviewConsumeUploadBudget(CandidateBudget))
	{
		if(Budget.m_pMergeBudget != nullptr)
			Budget.m_pMergeBudget->m_StopReason = CandidateMerge.m_StopReason;
		if(Budget.m_pFrameBudget != nullptr)
			Budget.m_pFrameBudget->m_StopReason = CandidateFrame.m_StopReason;
		// 延期时保留原节点及像素，不通过重新入队分配容器节点。
		Telemetry.m_UploadBudgetExhausted = 1;
		Telemetry.m_UploadQueueDepth = (int)m_vUploadQueue.size();
		return false;
	}
	if(!ValidImage)
	{
		SUploadItem Item = std::move(m_vUploadQueue.front());
		m_vUploadQueue.pop_front();
		Item.m_Image.Free();
		Item.m_MemoryLease.Reset();
		Cache.MarkUploadFailed(Item.m_Key);
		if(Item.m_Finalize)
			Item.m_Finalize(false, IGraphics::CTextureHandle());
		Telemetry.m_UploadQueueDepth = (int)m_vUploadQueue.size();
		return false;
	}
	try
	{
		// 命令接收之后只更新已有缓存，不再准备 map 节点。
		Cache.GetOrCreate(m_vUploadQueue.front().m_Key);
	}
	catch(const std::bad_alloc &)
	{
		Telemetry.m_UploadBudgetExhausted = 1;
		Telemetry.m_UploadQueueDepth = (int)m_vUploadQueue.size();
		return false;
	}
	catch(const std::length_error &)
	{
		Telemetry.m_UploadBudgetExhausted = 1;
		Telemetry.m_UploadQueueDepth = (int)m_vUploadQueue.size();
		return false;
	}
	IGraphics::CTextureHandle Texture;
	bool Accepted = false;
	{
		auto &Front = m_vUploadQueue.front();
		auto Release = Front.m_MemoryLease.Detach();
		struct SRestoreUnconsumedLease
		{
			CSettingsAssetMemoryLease &m_Lease;
			SGraphicsUploadRelease &m_Release;
			~SRestoreUnconsumedLease() { m_Lease = CSettingsAssetMemoryLease::Adopt(m_Release); }
		} Restore{Front.m_MemoryLease, Release};
		// false 或提交前异常仍归队列；true 后额度由命令的实际像素释放路径归还。
		Accepted = pGraphics->TryLoadTextureRawMoveOwned(Front.m_Image, 0, Front.m_DebugName.c_str(), Release, Texture);
	}
	if(!Accepted)
	{
		Telemetry.m_UploadBudgetExhausted = 1;
		Telemetry.m_UploadQueueDepth = (int)m_vUploadQueue.size();
		return false;
	}
	Budget.m_UploadsUsed = CandidateBudget.m_UploadsUsed;
	if(Budget.m_pMergeBudget != nullptr)
		*Budget.m_pMergeBudget = CandidateMerge;
	if(Budget.m_pFrameBudget != nullptr)
		*Budget.m_pFrameBudget = CandidateFrame;
	SUploadItem Item = std::move(m_vUploadQueue.front());
	m_vUploadQueue.pop_front();
	if(Texture.IsValid())
	{
		if(Item.m_Finalize)
		{
			SResourcePreviewState &State = Cache.GetOrCreate(Item.m_Key);
			State.m_UploadPending = false;
			State.m_TextureReady = true;
			State.m_Failed = false;
		}
		else
			Cache.MarkTextureReady(Item.m_Key, Texture, pGraphics);
		SettingsResourcePreviewCommitUploadBudget(Budget);
		++Telemetry.m_PreviewUploads;
		if(Item.m_Finalize)
			Item.m_Finalize(true, Texture);
		Telemetry.m_UploadQueueDepth = (int)m_vUploadQueue.size();
		return true;
	}
	Cache.MarkUploadFailed(Item.m_Key);
	if(Item.m_Finalize)
		Item.m_Finalize(false, IGraphics::CTextureHandle());
	Telemetry.m_UploadQueueDepth = (int)m_vUploadQueue.size();
	return false;
}

int CSettingsResourcePreviewUploadScheduler::Drain(SResourcePreviewUploadBudget &Budget, SResourcePreviewTelemetry &Telemetry, CSettingsResourcePreviewCache &Cache, IGraphics *pGraphics)
{
	int Uploads = 0;
	while(!m_vUploadQueue.empty())
	{
		if(DrainOne(Budget, Telemetry, Cache, pGraphics))
			++Uploads;
		else if(Telemetry.m_UploadBudgetExhausted)
			break;
	}
	Telemetry.m_UploadQueueDepth = (int)m_vUploadQueue.size();
	return Uploads;
}

void CSettingsResourcePreviewUploadScheduler::Clear()
{
	m_vUploadQueue.clear();
}

void CSettingsResourcePreviewUploadScheduler::Invalidate(const SResourcePreviewInvalidationScope &Scope)
{
	// 稳定压缩后一次删除尾段，不分配候选容器，也不调用取消项的 Finalize。
	m_vUploadQueue.erase(std::remove_if(m_vUploadQueue.begin(), m_vUploadQueue.end(),
				     [&](const SUploadItem &Item) { return Scope.Matches(Item.m_Key); }),
		m_vUploadQueue.end());
}

SResourcePreviewArtifact BuildPreviewArtifact(CImageInfo &&Image, int TargetSize, CSettingsAssetMemoryLease *pMemoryLease, CSettingsAssetMemoryBudget *pBudget)
{
	const auto Start = time_get_nanoseconds();
	SResourcePreviewArtifact Artifact;
	if(!SettingsResourcePreviewImageValidForUpload(Image))
	{
		Image.Free();
		if(pMemoryLease != nullptr)
			pMemoryLease->Reset();
		Artifact.m_DurationMs = std::chrono::duration<double, std::milli>(time_get_nanoseconds() - Start).count();
		return Artifact;
	}
	uint64_t ReadyBytes = 0;
	if(!SettingsAssetMemoryReadyBytes(Image, ReadyBytes))
		return Artifact;
	if(pMemoryLease != nullptr && pMemoryLease->HasReservation())
	{
		if(pMemoryLease->Bytes() < ReadyBytes)
		{
			Artifact.m_Deferred = true;
			return Artifact;
		}
		Artifact.m_MemoryLease = std::move(*pMemoryLease);
	}
	else if(!SettingsAssetMemoryTryAcquireImage(Image, Artifact.m_MemoryLease, pBudget))
	{
		Artifact.m_Deferred = true;
		return Artifact;
	}
	Artifact.m_Image = std::move(Image);
	ResizePreviewArtifact(Artifact, TargetSize);
	FinishPreviewArtifact(Artifact);
	Artifact.m_DurationMs = std::chrono::duration<double, std::milli>(time_get_nanoseconds() - Start).count();
	return Artifact;
}

SResourcePreviewArtifact BuildPreviewArtifactFromPath(const char *pPath, IStorage *pStorage, int StorageType, const char *pContextName, int TargetSize, size_t MaxFileBytes, size_t MaxDecodedBytes, CSettingsAssetMemoryBudget *pBudget, CSettingsAssetMemoryLease *pMemoryLease)
{
	const auto Start = time_get_nanoseconds();
	SResourcePreviewArtifact Artifact;
	if(pPath == nullptr || pPath[0] == '\0' || pStorage == nullptr)
	{
		Artifact.m_DurationMs = std::chrono::duration<double, std::milli>(time_get_nanoseconds() - Start).count();
		return Artifact;
	}

	uint64_t WorkingBytes = 0;
	if(!SettingsAssetMemoryComputeWorkingBytes(MaxFileBytes, MaxDecodedBytes, TargetSize, WorkingBytes))
		return Artifact;
	if(pMemoryLease != nullptr && pMemoryLease->HasReservation())
	{
		if(pMemoryLease->Bytes() < WorkingBytes)
		{
			Artifact.m_Deferred = true;
			return Artifact;
		}
		Artifact.m_MemoryLease = std::move(*pMemoryLease);
	}
	else if(!(pBudget != nullptr ? *pBudget : SettingsAssetMemoryDefaultBudget()).TryAcquire(WorkingBytes, Artifact.m_MemoryLease))
	{
		Artifact.m_Deferred = true;
		return Artifact;
	}
	{
		CQmCheckedFileData FileData;
		if(FileData.Read(*pStorage, pPath, StorageType, MaxFileBytes))
		{
			const char *pName = pContextName != nullptr ? pContextName : pPath;
			bool Success = CImageLoader::LoadWebP(FileData.Data(), FileData.Size(), pName, Artifact.m_Image, MaxDecodedBytes);
			if(!Success)
			{
				Artifact.m_Image.Free();
				Success = CImageLoader::LoadPng(FileData.Data(), FileData.Size(), pName, Artifact.m_Image, MaxDecodedBytes);
			}
			if(Success)
				ResizePreviewArtifact(Artifact, TargetSize);
		}
	}
	// 编码源及解码器临时副本离开作用域后，才缩减至随 READY/上传命令保留的额度。
	FinishPreviewArtifact(Artifact);
	Artifact.m_DurationMs = std::chrono::duration<double, std::milli>(time_get_nanoseconds() - Start).count();
	return Artifact;
}

bool SettingsResourcePreviewImageValidForUpload(const CImageInfo &Image)
{
	const bool Supported = Image.m_Format == CImageInfo::FORMAT_RGBA || Image.m_Format == CImageInfo::FORMAT_RGB ||
			       Image.m_Format == CImageInfo::FORMAT_R || Image.m_Format == CImageInfo::FORMAT_RA;
	size_t Bytes = 0;
	return Image.m_pData != nullptr && Image.m_Width > 0 && Image.m_Height > 0 &&
	       Image.m_Width <= (size_t)std::numeric_limits<int>::max() && Image.m_Height <= (size_t)std::numeric_limits<int>::max() &&
	       Supported && Image.DataSize(Bytes);
}

bool SettingsResourcePreviewConsumeUploadBudget(SResourcePreviewUploadBudget &Budget, int Count)
{
	if(Count <= 0)
		return true;
	if(Budget.m_MaxUploads - Budget.m_UploadsUsed < Count)
		return false;
	if(Budget.m_pGpuUploadLimiter != nullptr && !Budget.m_pGpuUploadLimiter->CanUpload(Count))
		return false;
	if(Budget.m_pMergeBudget != nullptr && Budget.m_pFrameBudget != nullptr)
	{
		if(!SettingsResourceConsumeGpuUploads(*Budget.m_pMergeBudget, Budget.m_pFrameBudget, Count))
			return false;
	}
	else if(Budget.m_pMergeBudget != nullptr)
	{
		for(int i = 0; i < Count; ++i)
		{
			if(!SettingsResourceConsumeGpuUpload(*Budget.m_pMergeBudget))
				return false;
		}
	}
	Budget.m_UploadsUsed += Count;
	return true;
}

void SettingsResourcePreviewCommitUploadBudget(SResourcePreviewUploadBudget &Budget, int Count)
{
	if(Count <= 0 || Budget.m_pGpuUploadLimiter == nullptr)
		return;
	for(int i = 0; i < Count; ++i)
		Budget.m_pGpuUploadLimiter->OnUploaded();
}

ESettingsResourcePreviewDrawResult SettingsResourcePreviewDrawResult(const SResourcePreviewState &State)
{
	if(State.m_TextureReady)
		return ESettingsResourcePreviewDrawResult::READY_TEXTURE;
	if(State.m_Failed)
		return ESettingsResourcePreviewDrawResult::FAILED_PLACEHOLDER;
	return ESettingsResourcePreviewDrawResult::PLACEHOLDER;
}

float SettingsResourcePreviewVisibleReadyRatio(int ReadyTextureCount, int VisibleCount)
{
	if(VisibleCount <= 0)
		return 1.0f;
	return (float)ReadyTextureCount / (float)VisibleCount;
}
