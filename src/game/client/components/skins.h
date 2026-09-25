/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SKINS_H
#define GAME_CLIENT_COMPONENTS_SKINS_H

#include <base/hash.h>
#include <base/lock.h>

#include <engine/client/enums.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>

#include <generated/protocol7.h>

#include <game/client/component.h>
#include <game/client/components/qmclient/settings_resource_preview.h>
#include <game/client/components/qmclient/skin_load_budget.h>
#include <game/client/components/qmclient/skin_prepared_textures.h>
#include <game/client/components/qmclient/skin_prepared_visuals.h>
#include <game/client/components/settings_resource_jobs.h>
#include <game/client/skin.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <limits>
#include <list>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class IHttpRequest;
class CSkins : public CComponent
{
private:
	/**
	 * The data of a skin that can be loaded in a separate thread.
	 */
	class CSkinLoadData
	{
	public:
		CImageInfo m_Info;
		CImageInfo m_InfoGrayscale;
		CSkin::CSkinMetrics m_Metrics;
		ColorRGBA m_BloodColor;
		// 解码任务在 CPU 侧备好素材，主线程只做纹理上传与接管（避免主线程重复提取精灵）。
		// 纹理预备按精灵粒度记录可用性，越界精灵留待主线程走空白素材回退。
		SQmPreparedSkinVisuals m_PreparedVisuals;
		std::unique_ptr<CQmPreparedSkinTextures> m_pPreparedTextures;
		size_t m_SourceWidth = 0;
		size_t m_SourceHeight = 0;
	};

	/**
	 * An abstract job to load a skin from a source determined by the derived class.
	 */
	class CAbstractSkinLoadJob : public IJob
	{
	public:
		CAbstractSkinLoadJob(CSkins *pSkins, const char *pName);
		~CAbstractSkinLoadJob() override;

		CSkinLoadData m_Data;
		bool m_NotFound = false;

	protected:
		CSkins *m_pSkins;
		char m_aName[MAX_SKIN_LENGTH];
	};

public:
	struct SSkinSpriteSpec
	{
		int m_GridX;
		int m_GridY;
		int m_X;
		int m_Y;
		int m_W;
		int m_H;
	};

	struct SSkinMetricPlan
	{
		int m_Width = 0;
		int m_Height = 0;
		int m_OffsetX = 0;
		int m_OffsetY = 0;
		int m_MaxWidth = 0;
		int m_MaxHeight = 0;
	};

	struct SSkinDataPlan
	{
		SSkinMetricPlan m_Body;
		SSkinMetricPlan m_Feet;
	};

	/**
	 * Container for a skin, its loading state, job and various meta data.
	 */
	class CSkinContainer
	{
		friend class CSkins;

	public:
		enum class EType
		{
			/**
			 * Skin should be loaded locally (from skins folder).
			 */
			LOCAL,
			/**
			 * Skin should be downloaded (or loaded from downloadedskins).
			 */
			DOWNLOAD,
		};

		enum class EState
		{
			/**
			 * Skin is unloaded and loading is not desired.
			 */
			UNLOADED,
			/**
			 * Skin was requested by the settings Tee source path but has not entered the admitted load queue yet.
			 */
			BACKGROUND_REQUESTED,
			/**
			 * Skin is unloaded and should be loaded when a slot is free. Skin will enter @link EState::LOADING @endlink
			 * state when maximum number of loaded skins is not exceeded.
			 */
			PENDING,
			/**
			 * Skin is currently loading, iff @link m_pLoadJob @endlink is set.
			 */
			LOADING,
			/**
			 * Skin is loaded, iff @link m_pSkin @endlink is set.
			 */
			LOADED,
			/**
			 * Skin failed to be loaded due to an unexpected error.
			 */
			ERROR,
			/**
			 * Skin failed to be downloaded because it could not be found.
			 */
			NOT_FOUND,
		};

		enum class EStatusIndicator
		{
			NONE = 0,
			LOADING,
			NOT_FOUND,
			ERROR,
		};

		CSkinContainer(CSkinContainer &&Other) = default;
		CSkinContainer(CSkins *pSkins, const char *pName, EType Type, int StorageType);
		~CSkinContainer();

		bool operator<(const CSkinContainer &Other) const;
		CSkinContainer &operator=(CSkinContainer &&Other) = default;

		const char *Name() const { return m_aName; }
		EType Type() const { return m_Type; }
		int StorageType() const { return m_StorageType; }
		time_t LastModified() const { return m_LastModified; }
		void SetLastModified(time_t LastModified) { m_LastModified = LastModified; }
		int OfficialReleaseDate() const { return m_OfficialReleaseDate; }
		void SetOfficialReleaseDate(int OfficialReleaseDate) { m_OfficialReleaseDate = OfficialReleaseDate; }
		const char *OfficialCreator() const { return m_aOfficialCreator; }
		void SetOfficialCreator(const char *pOfficialCreator) { str_copy(m_aOfficialCreator, pOfficialCreator == nullptr ? "" : pOfficialCreator, sizeof(m_aOfficialCreator)); }
		bool IsVanilla() const { return m_Vanilla; }
		bool IsSpecial() const { return m_Special; }
		bool IsAlwaysLoaded() const { return m_AlwaysLoaded; }
		EState State() const { return m_State; }
		const std::unique_ptr<CSkin> &Skin() const { return m_pSkin; }
		size_t SettingsSourceApproxBytes() const { return m_SettingsSourceApproxBytes; }
		struct SUsageTrackingUpdate
		{
			bool m_ShouldTouch = false;
			bool m_ShouldErase = false;
		};
		static bool ShouldDiscardPendingUpload(EState OldState, EState NewState)
		{
			return OldState == EState::LOADING && NewState != EState::LOADING && NewState != EState::LOADED;
		}
		static bool TracksUsage(EState State, bool AlwaysLoaded)
		{
			return !AlwaysLoaded &&
			       (State == EState::PENDING || State == EState::LOADING || State == EState::LOADED);
		}
		static bool TracksPriorityUsage(EState State, bool AlwaysLoaded, ESettingsResourcePriority Priority)
		{
			return !AlwaysLoaded &&
			       Priority != ESettingsResourcePriority::BACKGROUND &&
			       (State == EState::BACKGROUND_REQUESTED || TracksUsage(State, AlwaysLoaded));
		}
		static bool SettingsResourcePriorityCanUpgrade(ESettingsResourcePriority Priority, ESettingsResourcePriority CurrentPriority)
		{
			return static_cast<int>(Priority) > static_cast<int>(CurrentPriority);
		}
		static SUsageTrackingUpdate UsageTrackingUpdate(EState State, bool AlwaysLoaded, bool HasUsageEntry)
		{
			const bool ShouldTrack = TracksUsage(State, AlwaysLoaded);
			return {ShouldTrack && !HasUsageEntry, !ShouldTrack && HasUsageEntry};
		}
		static SUsageTrackingUpdate UsageTrackingUpdate(EState State, bool AlwaysLoaded, bool HasUsageEntry, ESettingsResourcePriority Priority)
		{
			const bool ShouldTrack = TracksPriorityUsage(State, AlwaysLoaded, Priority);
			return {ShouldTrack && !HasUsageEntry, !ShouldTrack && HasUsageEntry};
		}
		static bool ShouldDiscardUsageEntryBeforeUnload(bool ExistsInSkinMap, EState State, bool AlwaysLoaded)
		{
			return !ExistsInSkinMap || (!TracksUsage(State, AlwaysLoaded) && State != EState::BACKGROUND_REQUESTED);
		}
		static bool StateChangeRequiresListRefresh(EState OldState, EState NewState)
		{
			return (OldState == EState::NOT_FOUND) != (NewState == EState::NOT_FOUND);
		}
		static bool ShouldDiscardPendingUpload(EState OldState, EState NewState)
		{
			return OldState == EState::LOADING && NewState != EState::LOADING && NewState != EState::LOADED;
		}
		static bool IsUnresolved(EState State)
		{
			return State == EState::ERROR || State == EState::NOT_FOUND;
		}
		static EStatusIndicator StatusIndicator(EState State)
		{
			switch(State)
			{
			case EState::UNLOADED:
			case EState::BACKGROUND_REQUESTED:
			case EState::PENDING:
			case EState::LOADING:
				return EStatusIndicator::LOADING;
			case EState::NOT_FOUND:
				return EStatusIndicator::NOT_FOUND;
			case EState::ERROR:
				return EStatusIndicator::ERROR;
			case EState::LOADED:
				break;
			}
			return EStatusIndicator::NONE;
		}

		/**
		 * Request that this skin should be loaded and should stay loaded.
		 */
		void RequestLoad(bool Immediate = false);
		void RequestLoad(ESettingsResourcePriority Priority);

	private:
		CSkins *m_pSkins;
		char m_aName[MAX_SKIN_LENGTH];
		EType m_Type;
		int m_StorageType;
		time_t m_LastModified = 0;
		int m_OfficialReleaseDate = 0;
		char m_aOfficialCreator[MAX_NAME_LENGTH] = {};
		bool m_Vanilla;
		bool m_Special;
		bool m_AlwaysLoaded;

		EState m_State = EState::UNLOADED;
		bool m_UnresolvedNotified = false;
		ESettingsResourcePriority m_LoadPriority = ESettingsResourcePriority::BACKGROUND;
		std::unique_ptr<CSkin> m_pSkin = nullptr;
		std::unique_ptr<CSkin> m_pPendingSkin;
		std::shared_ptr<CAbstractSkinLoadJob> m_pLoadJob = nullptr;
		CSkinLoadData m_SettingsPendingUploadData;
		size_t m_SettingsPendingUploadSprite = 0;
		std::chrono::nanoseconds m_SettingsPendingUploadStart{};
		size_t m_SettingsSourceApproxBytes = 0;

		/**
		 * The time when loading of this skin was first requested.
		 */
		std::optional<std::chrono::nanoseconds> m_FirstLoadRequest;
		/**
		 * The time when loading of this skin was most recently requested.
		 */
		std::optional<std::chrono::nanoseconds> m_LastLoadRequest;
		/**
		 * Iterator into @link CSkins::m_SkinsUsageList @endlink for this skin container.
		 */
		std::optional<std::list<std::string>::iterator> m_UsageEntryIterator;
		std::optional<std::list<std::string>::iterator> m_BackgroundEntryIterator;

		EState DetermineInitialState() const;
		bool IsBackgroundTracked() const { return m_BackgroundEntryIterator.has_value() && !m_UsageEntryIterator.has_value(); }
		void TouchUsage();
		void ClearUsage();
		void TouchBackgroundUsage();
		void ClearBackgroundUsage();
		void SetState(EState State, ESettingsResourcePriority Priority = ESettingsResourcePriority::VISIBLE);
	};

	class CUnresolvedSkinScanState
	{
	public:
		void OnStateChange(CSkinContainer::EState OldState, CSkinContainer::EState NewState)
		{
			if(OldState != NewState && CSkinContainer::IsUnresolved(NewState))
				m_Pending = true;
		}
		bool Consume() { return std::exchange(m_Pending, false); }

	private:
		bool m_Pending = false;
	};

	/**
	 * Represents a skin being displayed in a list in the UI.
	 */
	class CSkinListEntry
	{
	public:
		struct SColorKey
		{
			bool m_UseCustomColor = false;
			int m_ColorBody = 0;
			int m_ColorFeet = 0;
		};

		CSkinListEntry() :
			m_pSkinContainer(nullptr),
			m_Favorite(false),
			m_SelectedMain(false),
			m_SelectedDummy(false) {}
		CSkinListEntry(CSkinContainer *pSkinContainer, bool Favorite, bool SelectedMain, bool SelectedDummy, std::optional<SColorKey> ColorKey, std::optional<std::pair<int, int>> NameMatch) :
			m_pSkinContainer(pSkinContainer),
			m_Favorite(Favorite),
			m_SelectedMain(SelectedMain),
			m_SelectedDummy(SelectedDummy),
			m_ColorKey(ColorKey),
			m_NameMatch(NameMatch) {}

		bool operator<(const CSkinListEntry &Other) const;

		const CSkinContainer *SkinContainer() const { return m_pSkinContainer; }
		bool IsFavorite() const { return m_Favorite; }
		bool IsSelectedMain() const { return m_SelectedMain; }
		bool IsSelectedDummy() const { return m_SelectedDummy; }
		const std::optional<SColorKey> &ColorKey() const { return m_ColorKey; }
		const std::optional<std::pair<int, int>> &NameMatch() const { return m_NameMatch; }

		const void *ListItemId() const { return &m_ListItemId; }
		const void *FavoriteButtonId() const { return &m_FavoriteButtonId; }
		const void *ErrorTooltipId() const { return &m_ErrorTooltipId; }
		// 右键双击的目标角色判定需要独立状态：列表框左键双击用的是 ListItemId，
		// 共用会让一次左键单击把右键双击状态提前置位。
		const void *RightDoubleClickId() const { return &m_RightDoubleClickId; }

		/**
		 * Request that this skin should be loaded and should stay loaded.
		 */
		void RequestLoad(bool Immediate = false);
		void RequestLoad(ESettingsResourcePriority Priority);

	private:
		CSkinContainer *m_pSkinContainer;
		bool m_Favorite;
		bool m_SelectedMain;
		bool m_SelectedDummy;
		std::optional<SColorKey> m_ColorKey;
		std::optional<std::pair<int, int>> m_NameMatch;
		char m_ListItemId;
		char m_FavoriteButtonId;
		char m_ErrorTooltipId;
		char m_RightDoubleClickId;
	};

	class CSkinList
	{
		friend class CSkins;

	public:
		std::vector<CSkinListEntry> &Skins() { return m_vSkins; }
		int UnfilteredCount() const { return m_UnfilteredCount; }
		uint64_t Revision() const { return m_Revision; }
		void ForceRefresh() { m_NeedsUpdate = true; }

	private:
		std::vector<CSkinListEntry> m_vSkins;
		int m_UnfilteredCount = 0;
		uint64_t m_Revision = 0;
		int m_Dummy = -1;
		CSkinListEntry::SColorKey m_MainColorKey;
		CSkinListEntry::SColorKey m_DummyColorKey;
		bool m_NeedsUpdate = true;
	};

	class CUnresolvedSkinScanState
	{
	public:
		void OnStateChange(CSkinContainer::EState OldState, CSkinContainer::EState NewState)
		{
			if(OldState != NewState && CSkinContainer::IsUnresolved(NewState))
				m_Pending = true;
		}

		bool Consume() { return std::exchange(m_Pending, false); }

	private:
		bool m_Pending = false;
	};

	class CSkinLoadingStats
	{
	public:
		size_t m_NumUnloaded = 0;
		size_t m_NumBackgroundRequested = 0;
		size_t m_NumPending = 0;
		size_t m_NumLoading = 0;
		size_t m_NumLoaded = 0;
		size_t m_NumError = 0;
		size_t m_NumNotFound = 0;

		void AddState(CSkinContainer::EState State)
		{
			switch(State)
			{
			case CSkinContainer::EState::UNLOADED: ++m_NumUnloaded; break;
			case CSkinContainer::EState::BACKGROUND_REQUESTED: ++m_NumBackgroundRequested; break;
			case CSkinContainer::EState::PENDING: ++m_NumPending; break;
			case CSkinContainer::EState::LOADING: ++m_NumLoading; break;
			case CSkinContainer::EState::LOADED: ++m_NumLoaded; break;
			case CSkinContainer::EState::ERROR: ++m_NumError; break;
			case CSkinContainer::EState::NOT_FOUND: ++m_NumNotFound; break;
			}
		}

		size_t RealInflight() const { return m_NumPending + m_NumLoading; }
		bool AdmissionInvariantViolated(int CountFuseLimit) const
		{
			return CountFuseLimit >= 0 && RealInflight() > (size_t)CountFuseLimit;
		}
	};

	struct SSettingsSourceAdmissionTelemetry
	{
		int m_AdmittedDelta = 0;
		int m_StartedDelta = 0;
		int m_RealInflight = 0;
		int m_CountFuseLimit = 0;
		int m_VisibleReserve = 0;
		int m_LoadingWindowLimit = 0;
		int m_LoadingWindowUsed = 0;
		int m_GpuUploadLimitUnits = 0;
		int m_GpuUploadRemainingUnits = 0;
		int m_FinalizeBudgetLimit = 0;
		int m_VisibleBackgroundRequested = 0;
		int m_VisibleNonterminalWaiting = 0;
		int m_UnderfedStreak = 0;
		int m_FallbackSweepScanned = 0;
		int m_FallbackSweepStarted = 0;
		float m_FrameTimeAverageMs = 0.0f;
		float m_RenderFrameTimeMs = 0.0f;
		bool m_AdmissionInvariantViolated = false;
		bool m_AdmissionUnderfed = false;
		char m_aDynamicDecision[48] = "hold";
		char m_aControllerMode[32] = "idle_visible";
		char m_aControllerReason[32] = "none";
		char m_aLastWaitReason[32] = "none";
	};

	struct SSettingsTeeVisibleSnapshot
	{
		int m_VisibleTotal = 0;
		int m_VisibleReady = 0;
		int m_VisibleWaiting = 0;
		int m_VisibleBackgroundRequested = 0;
		int m_VisibleNonterminalWaiting = 0;
		char m_aRequestBudgetBlockReason[32] = "none";
	};

	CSkins();

	static bool BuildSkinDataPlan(CImageInfo &Image, const SSkinSpriteSpec &Body, const SSkinSpriteSpec &BodyOutline, const SSkinSpriteSpec &Feet, const SSkinSpriteSpec &FeetOutline, SSkinDataPlan &Plan)
	{
		if(!PrepareSkinImage(Image, Body.m_GridX, Body.m_GridY))
		{
			return false;
		}
		if(Image.m_Format != CImageInfo::FORMAT_RGBA)
		{
			return false;
		}
		if((size_t)Body.m_W * (Image.m_Width / Body.m_GridX) > Image.m_Width ||
			(size_t)Body.m_H * (Image.m_Height / Body.m_GridY) > Image.m_Height)
		{
			return false;
		}

		SSkinMetricPlan BodyPlan;
		const bool BodyHasPixels = MeasureSkinSprite(BodyPlan, Image, Body);
		SSkinMetricPlan BodyOutlinePlan;
		const bool BodyOutlineHasPixels = MeasureSkinSprite(BodyOutlinePlan, Image, BodyOutline);
		Plan.m_Body = BodyPlan;
		if(BodyOutlineHasPixels)
		{
			if(BodyHasPixels)
				MergeSkinMetricPlans(Plan.m_Body, BodyOutlinePlan);
			else
				Plan.m_Body = BodyOutlinePlan;
		}

		SSkinMetricPlan FeetPlan;
		const bool FeetHasPixels = MeasureSkinSprite(FeetPlan, Image, Feet);
		SSkinMetricPlan FeetOutlinePlan;
		const bool FeetOutlineHasPixels = MeasureSkinSprite(FeetOutlinePlan, Image, FeetOutline);
		Plan.m_Feet = FeetPlan;
		if(FeetOutlineHasPixels)
		{
			if(FeetHasPixels)
				MergeSkinMetricPlans(Plan.m_Feet, FeetOutlinePlan);
			else
				Plan.m_Feet = FeetOutlinePlan;
		}
		return true;
	}

	typedef std::function<void()> TSkinLoadedCallback;

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnInit() override;
	void OnShutdown() override;
	void OnUpdate() override;
	void OnRender() override;

	void RefreshEventSkins();
	void Refresh(TSkinLoadedCallback &&SkinLoadedCallback);
	CSkinLoadingStats LoadingStats() const;
	CSkinList &SkinList(int Dummy);
	void RebuildSkinListPlan();
	bool SkinListSkeletonReady() const;
	bool SkinListReady() const;
	uint64_t SettingsSourceUploadsCompleted() const { return m_SettingsSourceUploadsCompleted; }
	uint64_t SettingsSourceLoadsCompleted() const { return m_SettingsSourceLoadsCompleted; }
	const SSettingsSourceAdmissionTelemetry &SettingsSourceAdmissionTelemetry() const { return m_SettingsSourceAdmissionTelemetry; }
	void SetSettingsTeeVisibleSnapshot(const SSettingsTeeVisibleSnapshot &Snapshot) { m_SettingsTeeVisibleSnapshot = Snapshot; }
	void PrepareSettingsThroughputForFrame();
	void UpdateForSettingsWarmup();
	int SettingsGpuUploadLimiterUnitsForFrame() const { return m_SettingsThroughputControllerOutput.m_GpuUploadLimitUnits; }
	int SettingsGpuUploadFrameBudgetForFrame() const { return m_SettingsThroughputControllerOutput.m_GpuUploadFrameBudget; }
	int SettingsFinalizeBudgetForFrame() const { return m_SettingsThroughputControllerOutput.m_FinalizeBudgetLimit; }
	int SettingsNormalLoadingWindowForFrame() const { return m_SettingsThroughputControllerOutput.m_NormalLoadingWindow; }
	int SettingsVisibleLoadingWindowForFrame() const { return m_SettingsThroughputControllerOutput.m_VisibleLoadingWindow; }
	int SettingsVisibleReserveForFrame() const { return m_SettingsThroughputControllerOutput.m_VisibleReserve; }
	int SettingsBackgroundRequestBudgetForFrame() const { return m_SettingsThroughputControllerOutput.m_BackgroundRequestBudget; }
	int SettingsCountFuseLimitForFrame() const { return m_SettingsThroughputControllerOutput.m_CountFuseLimit; }
	bool SettingsBackgroundDrainActiveForFrame() const { return m_SettingsThroughputControllerOutput.m_BackgroundDrainActive; }
	const SSettingsSkinThroughputControllerOutput &SettingsThroughputControllerOutput() const { return m_SettingsThroughputControllerOutput; }
	void PrewarmByNames(const std::vector<std::string> &vNames, bool Immediate = false);
	bool PrewarmPlayerPreviewReady(int Dummy, int MaxEntries, bool ProgressiveListReady = false);

	const CSkinContainer *FindContainerOrNullptr(const char *pName);
	/**
	 * 只读查找皮肤容器：未登记的皮肤名返回 nullptr，不会新建容器、不会发起加载请求。
	 * 用于判断“皮肤名已知但资源解析失败”，因为 FindContainerOrNullptr 会重新请求加载。
	 */
	const CSkinContainer *LookupContainerOrNullptr(const char *pName) const;
	const CSkin *FindOrNullptr(const char *pName);
	const CSkin *Find(const char *pName);

	void AddFavorite(const char *pName);
	void RemoveFavorite(const char *pName);
	bool IsFavorite(const char *pName) const;

	class CSkinQueueEntry
	{
	public:
		std::string m_SkinName;
		bool m_UseCustomColor = false;
		int m_ColorBody = 0;
		int m_ColorFeet = 0;
		bool m_HasSixup = false;
		char m_aaSixupSkinPartNames[protocol7::NUM_SKINPARTS][protocol7::MAX_SKIN_LENGTH] = {};
		int m_aSixupUseCustomColors[protocol7::NUM_SKINPARTS] = {};
		int m_aSixupSkinPartColors[protocol7::NUM_SKINPARTS] = {};

		bool operator==(const CSkinQueueEntry &Other) const
		{
			if(m_SkinName != Other.m_SkinName || m_UseCustomColor != Other.m_UseCustomColor || m_HasSixup != Other.m_HasSixup)
				return false;
			if(!m_UseCustomColor)
				return !m_HasSixup || SixupEquals(Other);
			return m_ColorBody == Other.m_ColorBody && m_ColorFeet == Other.m_ColorFeet && (!m_HasSixup || SixupEquals(Other));
		}

	private:
		bool SixupEquals(const CSkinQueueEntry &Other) const
		{
			for(int Part = 0; Part < protocol7::NUM_SKINPARTS; ++Part)
			{
				if(str_comp(m_aaSixupSkinPartNames[Part], Other.m_aaSixupSkinPartNames[Part]) != 0 ||
					m_aSixupUseCustomColors[Part] != Other.m_aSixupUseCustomColors[Part] ||
					m_aSixupSkinPartColors[Part] != Other.m_aSixupSkinPartColors[Part])
				{
					return false;
				}
			}
			return true;
		}
	};

	class CSkinQueuePreset
	{
	public:
		enum class EKind
		{
			USER,
			SERVER,
		};

		std::string m_Name;
		std::vector<CSkinQueueEntry> m_Queue;
		EKind m_Kind = EKind::USER;

		bool IsProtected() const { return m_Kind != EKind::USER; }
		EKind Kind() const { return m_Kind; }
	};

	const std::vector<CSkinQueueEntry> &SkinQueue(int Dummy) const { return m_aSkinQueue[Dummy]; }
	const std::vector<CSkinQueuePreset> &SkinQueuePresets(int Dummy) const { return m_vSkinQueuePresets; }
	int AppliedSkinQueuePresetIndex(int Dummy) const { return m_aAppliedSkinQueuePresetIndex[Dummy]; }
	bool SkinQueueDirty(int Dummy) const { return m_aSkinQueueDirty[Dummy]; }
	static constexpr size_t SKIN_QUEUE_DEFAULT_PRESET = 0;
	static constexpr size_t SKIN_QUEUE_SERVER_PRESET = 1;
	bool IsBuiltInSkinQueuePreset(size_t PresetIndex) const { return PresetIndex < 2; }
	// A preset is writable (Save can overwrite it) when it exists and is not the
	// dynamic Server preset. Pure helper, defined inline so unit tests can call
	// it without linking the full CSkins object.
	static bool IsSkinQueuePresetWritable(int PresetIndex, size_t PresetCount)
	{
		return PresetIndex >= 0 && (size_t)PresetIndex < PresetCount && PresetIndex != (int)SKIN_QUEUE_SERVER_PRESET;
	}
	// After removing a preset, recompute a dummy's Applied index: the removed
	// preset → -1, anything above shifts down, anything below is unchanged.
	static int NextAppliedPresetIndexAfterRemove(int Current, int Removed)
	{
		if(Current == Removed)
			return -1;
		if(Current > Removed)
			return Current - 1;
		return Current;
	}
	bool IsInSkinQueue(const char *pName, int Dummy) const;
	bool IsInSkinQueue(const char *pName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy) const;
	bool AddSkinQueue(const char *pName, int Dummy);
	bool AddSkinQueue(const char *pName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy);
	bool AddActiveSkinQueue(const char *pName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy);
	bool RemoveSkinQueue(const char *pName, int Dummy);
	bool RemoveSkinQueue(const char *pName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy);
	bool RemoveSkinQueue(const CSkinQueueEntry &Entry, int Dummy);
	bool RemoveActiveSkinQueue(const char *pName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy);
	bool RemoveActiveSkinQueue(const CSkinQueueEntry &Entry, int Dummy);
	void MoveSkinQueueItem(size_t FromIndex, size_t ToIndex, int Dummy);
	void MoveActiveSkinQueueItem(size_t FromIndex, size_t ToIndex, int Dummy);
	bool ApplySkinQueueIndex(size_t QueueIndex, int Dummy);
	bool RandomSkinQueueIndex(int Dummy);
	void TrimSkinQueueToLimit(int Dummy);
	void TrimActiveSkinQueueToLimit(int Dummy);
	bool AddSkinQueuePresetFromCurrent(int Dummy);
	bool SaveSkinQueueToAppliedPreset(int Dummy);
	bool RenameSkinQueuePreset(size_t PresetIndex, const char *pName, int Dummy);
	void ClearSkinQueue(int Dummy);
	bool ApplySkinQueuePreset(size_t PresetIndex, int Dummy);
	bool RemoveSkinQueuePreset(size_t PresetIndex, int Dummy);

	void RandomizeSkin(int Dummy);

	const char *SkinPrefix() const;

	static bool IsSpecialSkin(const char *pName);

	/**
	 * 旧 Tee 渲染信息只有在它引用的 6.x 皮肤贴图仍然驻留时才能继续复用。
	 * 皮肤贴图被资源预算卸载（或目录扫描重建）后，句柄依旧 IsValid()，但纹理已经释放；
	 * 继续复用会把这些句柄画到屏幕上，表现为一只没有贴图的纯白块 Tee。
	 */
	static bool CanReusePreviousSixSkin(bool SixFlagSet, bool SkinNameValid, bool SkinResident)
	{
		return !SixFlagSet || !SkinNameValid || SkinResident;
	}

	static int ParseOfficialSkinReleaseDateKey(const char *pDate)
	{
		if(pDate == nullptr)
			return 0;
		int Key = 0;
		for(int i = 0; i < 10; ++i)
		{
			const char c = pDate[i];
			if(i == 4 || i == 7)
			{
				if(c != '-')
					return 0;
				continue;
			}
			if(c < '0' || c > '9')
				return 0;
			Key = Key * 10 + (c - '0');
		}
		return pDate[10] == '\0' ? Key : 0;
	}

private:
	static bool IsVanillaSkin(const char *pName);
	static bool PrepareSkinImage(CImageInfo &Image, int DivX, int DivY)
	{
		dbg_assert(DivX != 0 && DivY != 0, "Passing 0 to this function is not allowed.");
		const bool WidthBroken = Image.m_Width == 0 || (Image.m_Width % DivX) != 0;
		const bool HeightBroken = Image.m_Height == 0 || (Image.m_Height % DivY) != 0;
		if(!WidthBroken && !HeightBroken)
		{
			return true;
		}
		if(Image.m_Width == 0 || Image.m_Height == 0)
		{
			return false;
		}

		int NewWidth = DivX;
		int NewHeight = DivY;
		if(WidthBroken)
		{
			NewWidth = maximum<int>(HighestBit(Image.m_Width), DivX);
			NewHeight = (NewWidth / DivX) * DivY;
		}
		else
		{
			NewHeight = maximum<int>(HighestBit(Image.m_Height), DivY);
			NewWidth = (NewHeight / DivY) * DivX;
		}
		ResizeImage(Image, NewWidth, NewHeight);
		return true;
	}
	static bool MeasureSkinSprite(SSkinMetricPlan &Plan, const CImageInfo &Image, const SSkinSpriteSpec &Sprite)
	{
		const int GridPixelsWidth = Image.m_Width / Sprite.m_GridX;
		const int GridPixelsHeight = Image.m_Height / Sprite.m_GridY;
		const int CheckWidth = Sprite.m_W * GridPixelsWidth;
		const int CheckHeight = Sprite.m_H * GridPixelsHeight;
		const int ImgX = Sprite.m_X * GridPixelsWidth;
		const int ImgY = Sprite.m_Y * GridPixelsHeight;
		const size_t Pitch = Image.m_Width * Image.PixelSize();
		int MaxY = -1;
		int MinY = CheckHeight + 1;
		int MaxX = -1;
		int MinX = CheckWidth + 1;

		for(int y = 0; y < CheckHeight; y++)
		{
			for(int x = 0; x < CheckWidth; x++)
			{
				const size_t OffsetAlpha = (y + ImgY) * Pitch + (x + ImgX) * Image.PixelSize() + 3;
				if(Image.m_pData[OffsetAlpha] > 0)
				{
					MaxY = maximum(MaxY, y);
					MinY = minimum(MinY, y);
					MaxX = maximum(MaxX, x);
					MinX = minimum(MinX, x);
				}
			}
		}

		Plan.m_MaxWidth = CheckWidth;
		Plan.m_MaxHeight = CheckHeight;
		if(MaxX < 0 || MaxY < 0)
		{
			Plan.m_Width = 1;
			Plan.m_Height = 1;
			Plan.m_OffsetX = 0;
			Plan.m_OffsetY = 0;
			return false;
		}

		Plan.m_Width = std::clamp((MaxX - MinX) + 1, 1, CheckWidth);
		Plan.m_Height = std::clamp((MaxY - MinY) + 1, 1, CheckHeight);
		Plan.m_OffsetX = std::clamp(MinX, 0, CheckWidth - 1);
		Plan.m_OffsetY = std::clamp(MinY, 0, CheckHeight - 1);
		return true;
	}

	static void MergeSkinMetricPlans(SSkinMetricPlan &Plan, const SSkinMetricPlan &Other)
	{
		const int MinX = minimum(Plan.m_OffsetX, Other.m_OffsetX);
		const int MinY = minimum(Plan.m_OffsetY, Other.m_OffsetY);
		const int MaxX = maximum(Plan.m_OffsetX + Plan.m_Width, Other.m_OffsetX + Other.m_Width);
		const int MaxY = maximum(Plan.m_OffsetY + Plan.m_Height, Other.m_OffsetY + Other.m_Height);

		Plan.m_OffsetX = MinX;
		Plan.m_OffsetY = MinY;
		Plan.m_Width = MaxX - MinX;
		Plan.m_Height = MaxY - MinY;
		Plan.m_MaxWidth = maximum(Plan.m_MaxWidth, Other.m_MaxWidth);
		Plan.m_MaxHeight = maximum(Plan.m_MaxHeight, Other.m_MaxHeight);
	}

	/**
	 * Names of all vanilla and special skins.
	 *
	 * The names have to be in lower case for efficient comparison.
	 */
	constexpr static const char *VANILLA_SKINS[] = {"bluekitty", "bluestripe", "brownbear",
		"cammo", "cammostripes", "coala", "default", "limekitty",
		"pinky", "redbopp", "redstripe", "saddo", "toptri",
		"twinbop", "twintri", "warpaint", "x_ninja", "x_spec"};

	class CSkinLoadJob : public CAbstractSkinLoadJob
	{
	public:
		CSkinLoadJob(CSkins *pSkins, const char *pName, int StorageType);

	protected:
		void Run() override;

	private:
		int m_StorageType;
	};

	class CSkinDownloadJob : public CAbstractSkinLoadJob
	{
	public:
		CSkinDownloadJob(CSkins *pSkins, const char *pName);

		bool Abort() override REQUIRES(!m_Lock);

	protected:
		void Run() override REQUIRES(!m_Lock);

	private:
		CLock m_Lock;
		std::shared_ptr<IHttpRequest> m_pGetRequest GUARDED_BY(m_Lock);
	};

	struct SSkinListSnapshotEntry
	{
		std::string m_Name;
		std::optional<CSkinListEntry::SColorKey> m_ColorKey;
		bool m_SelectedMain = false;
		bool m_SelectedDummy = false;
		bool m_Favorite = false;
		bool m_NotFound = false;
		bool m_Special = false;
		bool m_ForceShowNotFound = false;
		int m_OfficialReleaseDate = 0;
		time_t m_LastModified = 0;
	};

	class CSkinListPlanJob : public IJob
	{
	public:
		CSkinListPlanJob(std::vector<SSkinListSnapshotEntry> vEntries, std::string Filter, int Generation, int SortMode);

		struct SResult
		{
			int m_Generation = 0;
			int m_UnfilteredCount = 0;
			SSettingsSkinListPlan m_Plan;
			std::string m_Filter;
		};

		SResult TakeResult() { return std::move(m_Result); }

	protected:
		void Run() override;

	private:
		std::vector<SSkinListSnapshotEntry> m_vEntries;
		std::string m_Filter;
		int m_SortMode = 0;
		SResult m_Result;
	};

	class CSkinDirectoryScanJob : public IJob
	{
	public:
		CSkinDirectoryScanJob(IStorage *pStorage);

		struct SResult
		{
			struct SEntry
			{
				std::string m_Name;
				CSkinContainer::EType m_Type = CSkinContainer::EType::LOCAL;
				int m_StorageType = IStorage::TYPE_ALL;
				time_t m_LastModified = 0;
			};
			std::vector<SEntry> m_vEntries;
		};

		SResult TakeResult() { return std::move(m_Result); }

	protected:
		void Run() override;

	private:
		static int ScanCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser);
		void ScanDirectory(const char *pDirectory, CSkinContainer::EType Type);

		IStorage *m_pStorage;
		SResult m_Result;
		CSkinContainer::EType m_CurrentScanType = CSkinContainer::EType::LOCAL;
	};

	static bool PrepareSkinData(const char *pName, CSkinLoadData &Data);
	void LoadSkinFinish(CSkinContainer *pSkinContainer, CSkinLoadData &Data);
	bool BeginSkinPreviewUpload(CSkinContainer *pSkinContainer, CSkinLoadData &&Data);
	bool UploadNextSkinPreviewSprite(CSkinContainer *pSkinContainer, SResourcePreviewUploadBudget &Budget);
	void FinishSkinPreviewUpload(CSkinContainer *pSkinContainer);
	void DiscardSkinPreviewUpload(CSkinContainer *pSkinContainer);
	void LoadSkinDirect(const char *pName);
	const CSkinContainer *FindContainerImpl(const char *pName);
	static int SkinScan(const char *pName, int IsDir, int StorageType, void *pUser);

	void UpdateUnloadSkins(CSkinLoadingStats &Stats);
	void UnloadLoadedSkinTextures(CSkinContainer *pSkinContainer);
	void QueueSkinTexturesUnloaded(const char *pSkinName);
	bool ReclaimBackgroundSkinForPriorityRequest(const char *pRequesterName, int CountFuseLimit);
	void UpdateStartLoading(CSkinLoadingStats &Stats);
	void UpdateFinishLoading(CSkinLoadingStats &Stats, std::chrono::nanoseconds StartTime, std::chrono::nanoseconds MaxTime);
	void CollectUnresolvedSkins();
	size_t LoadedSkinLimit() const;
	void QueueSkinDirectoryScanJob();
	void ProcessSkinDirectoryScanJob();
	void QueueOfficialSkinIndexRequest();
	void ProcessOfficialSkinIndexRequest();
	void LoadOfficialSkinIndexCache();
	bool ApplyOfficialSkinIndexJson(const char *pJson, size_t JsonSize);
	void QueueSkinListPlanJob(int Dummy);
	void ProcessSkinListPlanJob();
	CSkinListEntry MakeSkinListEntry(const CSkinContainer *pSkinContainer, std::optional<CSkinListEntry::SColorKey> ColorKey) const;

	static void ConAddFavoriteSkin(IConsole::IResult *pResult, void *pUserData);
	static void ConRemFavoriteSkin(IConsole::IResult *pResult, void *pUserData);
	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);
	void OnConfigSave(IConfigManager *pConfigManager);
	static void ConAddSkinQueue(IConsole::IResult *pResult, void *pUserData);
	static void ConAddDummySkinQueue(IConsole::IResult *pResult, void *pUserData);
	static void ConAddSkinQueueEx(IConsole::IResult *pResult, void *pUserData);
	static void ConAddDummySkinQueueEx(IConsole::IResult *pResult, void *pUserData);
	static void ConRandomSkinQueue(IConsole::IResult *pResult, void *pUserData);
	static void ConRandomDummySkinQueue(IConsole::IResult *pResult, void *pUserData);
	static void ConAddSkinQueuePreset(IConsole::IResult *pResult, void *pUserData);
	static void ConAddDummySkinQueuePreset(IConsole::IResult *pResult, void *pUserData);
	static void ConAddSkinQueuePresetItem(IConsole::IResult *pResult, void *pUserData);
	static void ConAddDummySkinQueuePresetItem(IConsole::IResult *pResult, void *pUserData);
	static void ConAddSkinQueuePresetItemEx(IConsole::IResult *pResult, void *pUserData);
	static void ConAddDummySkinQueuePresetItemEx(IConsole::IResult *pResult, void *pUserData);
	static void ConfigSaveQueueCallback(IConfigManager *pConfigManager, void *pUserData);
	void OnQueueConfigSave(IConfigManager *pConfigManager);
	static void ConchainRefreshSkinList(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	void UpdateSkinQueue(std::chrono::nanoseconds Now, int Dummy);
	void SyncSkinQueueFromMapPlayers(int Dummy);
	void ApplySkinQueueCurrent(int Dummy);
	void ClampSkinQueueIndex(int Dummy);
	bool AddSkinQueuePreset(const char *pName, int Dummy);
	bool AddSkinQueuePresetItem(int PresetIndex, const char *pSkinName, int Dummy);
	bool AddSkinQueuePresetItem(int PresetIndex, const char *pSkinName, bool UseCustomColor, int ColorBody, int ColorFeet, int Dummy);
	bool AddSkinQueueImpl(const CSkinQueueEntry &Entry, int Dummy);
	bool RemoveSkinQueueImpl(const CSkinQueueEntry &Entry, int Dummy);

	friend class CSkinProfiles;

	std::unordered_map<std::string, std::unique_ptr<CSkinContainer>> m_Skins;
	std::optional<std::chrono::nanoseconds> m_ContainerUpdateTime;
	CSkinContainer *m_pSkinPreviewUpload = nullptr;
	size_t m_NumLoadingSkins = 0;
	CQmSkinUploadFrameBudget m_SkinUploadFrameBudget;
	CUnresolvedSkinScanState m_UnresolvedSkinScanState;
	std::vector<std::string> m_vSkinsUnresolvedThisFrame;
	std::vector<std::string> m_vSkinsTexturesUnloadedThisFrame;
	/**
	 * Sorted from most recently to least recently used. Must be kept synchronized with the skin containers.
	 * Contains prioritized skins in pending/loading/loaded states so visible items can be started and finished first.
	 */
	std::list<std::string> m_SkinsUsageList;
	std::list<std::string> m_SkinsBackgroundList;

	CSkinList m_SkinList;
	std::shared_ptr<CSkinDirectoryScanJob> m_pSkinDirectoryScanJob;
	std::shared_ptr<CSkinListPlanJob> m_pSkinListPlanJob;
	std::shared_ptr<IHttpRequest> m_pOfficialSkinIndexRequest;
	std::vector<SSettingsSkinListEntry> m_vPendingSkinListMergeEntries;
	std::vector<CSkinListEntry> m_vPendingSkinListEntries;
	size_t m_SkinListMergeCursor = 0;
	int m_PendingSkinListUnfilteredCount = 0;
	bool m_HasPendingSkinListMergePlan = false;
	int m_SkinListPlanGeneration = 0;
	std::vector<CSkinDirectoryScanJob::SResult::SEntry> m_vPendingSkinDirectoryEntries;
	size_t m_SkinDirectoryMergeCursor = 0;
	std::set<std::string> m_Favorites;
	std::array<std::vector<CSkinQueueEntry>, NUM_DUMMIES> m_aSkinQueue;
	std::vector<CSkinQueuePreset> m_vSkinQueuePresets;
	std::array<int, NUM_DUMMIES> m_aAppliedSkinQueuePresetIndex = {};
	std::array<bool, NUM_DUMMIES> m_aSkinQueueDirty = {};
	std::array<std::chrono::nanoseconds, NUM_DUMMIES> m_aSkinQueueElapsed = {};
	std::array<std::optional<std::chrono::nanoseconds>, NUM_DUMMIES> m_aSkinQueueLastUpdate = {};

	CSkin m_PlaceholderSkin;
	char m_aEventSkinPrefix[MAX_SKIN_LENGTH];
	uint64_t m_SettingsSourceUploadsCompleted = 0;
	uint64_t m_SettingsSourceLoadsCompleted = 0;
	CSettingsResourcePreviewUploadScheduler m_SettingsSkinPreviewUploadScheduler;
	uint64_t m_SettingsSourceLoadsAtLastStartLoading = 0;
	uint64_t m_SettingsSourceUploadsAtLastControllerFrame = 0;
	uint64_t m_SettingsSourceLoadsAtLastControllerFrame = 0;
	int m_SettingsBackgroundLoadingWindowLimit = 0;
	int m_SettingsBackgroundWindowHealthyFrames = 0;
	SSettingsTeeVisibleSnapshot m_SettingsTeeVisibleSnapshot;
	SSettingsSourceAdmissionTelemetry m_SettingsSourceAdmissionTelemetry;
	SSettingsSkinThroughputControllerState m_SettingsThroughputControllerState;
	SSettingsSkinThroughputControllerOutput m_SettingsThroughputControllerOutput;

	/**
	 * Maximum number of skins to process per frame in UpdateFinishLoading.
	 * This limit prevents frame stuttering caused by uploading too many textures at once.
	 * Each skin requires approximately 24 texture uploads (12 original + 12 colorable).
	 */
	static constexpr int MAX_SKINS_PER_FRAME = 12;

	enum class ESkinProcessResult
	{
		CONTINUE,
		BREAK_GPU_LIMIT,
		BREAK_TIME_EXCEEDED,
		BREAK_UPLOAD,
	};

	ESkinProcessResult ProcessSkinContainer(CSkinContainer *pSkinContainer, CSkinLoadingStats &Stats,
		int &SkinsProcessedThisFrame, std::chrono::nanoseconds StartTime,
		std::chrono::nanoseconds MaxTime);
	ESkinProcessResult DrainSettingsSkinPreviewUpload(CSkinContainer *pSkinContainer, CSkinLoadingStats &Stats,
		int &SkinsProcessedThisFrame, std::chrono::nanoseconds StartTime,
		std::chrono::nanoseconds MaxTime);
};

#endif
