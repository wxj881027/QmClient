// VoiceUtils voice_state_utils_test.cpp 行为测试。
// VoiceUtils 协议、音频算法和设备选择行为测试。
// 请抬头享受阳光｜日子很好 我很我---------致咩子
#define CONF_TEST 1
#include "test.h"

#include <base/str.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <game/client/components/qmclient/qmclient_utils.h>
#include <game/client/components/qmclient/voice/voice_capture_pipeline.h>
#include <game/client/components/qmclient/voice/voice_core.h>
#include <game/client/components/qmclient/voice/voice_utils.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

#if defined(CONF_RNNOISE)
#include <rnnoise.h>
#endif

using namespace VoiceUtils;

namespace VoiceUtils
{
	int ResolveNoiseSuppressMode(int ConfigValue, bool RnnoiseRuntimeAvailable, bool *pFallbackUsed);
}

static constexpr int TEST_VOICE_NOISE_SUPPRESS_OFF = 0;
static constexpr int TEST_VOICE_NOISE_SUPPRESS_SIMPLE = 1;
static constexpr int TEST_VOICE_NOISE_SUPPRESS_RNNOISE = 2;

TEST(VoiceUtils, VoiceTransmitBlockersNetworkAndDevice)
{
	SVoiceTransmitPreconditions Preconditions;
	Preconditions.m_NeedNetwork = true;

	const uint32_t Blockers = VoiceTransmitBlockers(Preconditions);
	EXPECT_NE(Blockers & VOICE_TX_BLOCK_SERVER_ADDR, 0u);
	EXPECT_NE(Blockers & VOICE_TX_BLOCK_SOCKET, 0u);
	EXPECT_NE(Blockers & VOICE_TX_BLOCK_ONLINE, 0u);
	EXPECT_NE(Blockers & VOICE_TX_BLOCK_CAPTURE, 0u);
	EXPECT_NE(Blockers & VOICE_TX_BLOCK_ENCODER, 0u);
}

TEST(VoiceUtils, VoiceTransmitBlockersLocalTestIgnoresNetwork)
{
	SVoiceTransmitPreconditions Preconditions;
	Preconditions.m_NeedNetwork = false;
	Preconditions.m_HaveCaptureDevice = true;
	Preconditions.m_HaveEncoder = true;

	const uint32_t Blockers = VoiceTransmitBlockers(Preconditions);
	EXPECT_EQ(Blockers & VOICE_TX_BLOCK_SERVER_ADDR, 0u);
	EXPECT_EQ(Blockers & VOICE_TX_BLOCK_SOCKET, 0u);
	EXPECT_EQ(Blockers & VOICE_TX_BLOCK_ONLINE, 0u);
	EXPECT_EQ(Blockers, 0u);
}

TEST(VoiceUtils, VoiceTransmitBlockersMicMutedIsReportedSeparately)
{
	SVoiceTransmitPreconditions Preconditions;
	Preconditions.m_NeedNetwork = true;
	Preconditions.m_ServerAddrValid = true;
	Preconditions.m_HaveSocket = true;
	Preconditions.m_Online = true;
	Preconditions.m_HaveCaptureDevice = true;
	Preconditions.m_HaveEncoder = true;
	Preconditions.m_MicMuted = true;

	const uint32_t Blockers = VoiceTransmitBlockers(Preconditions);
	EXPECT_EQ(Blockers, VOICE_TX_BLOCK_MIC_MUTED);
}

TEST(VoiceUtils, FormatVoiceTransmitBlockersEmpty)
{
	char aBuf[64];
	FormatVoiceTransmitBlockers(0, aBuf, (int)sizeof(aBuf));
	EXPECT_STREQ(aBuf, "none");
}

TEST(VoiceUtils, FormatVoiceTransmitBlockersListsReasonsInStableOrder)
{
	char aBuf[128];
	const uint32_t Blockers =
		VOICE_TX_BLOCK_SERVER_ADDR |
		VOICE_TX_BLOCK_SOCKET |
		VOICE_TX_BLOCK_CAPTURE |
		VOICE_TX_BLOCK_MIC_MUTED;
	FormatVoiceTransmitBlockers(Blockers, aBuf, (int)sizeof(aBuf));
	EXPECT_STREQ(aBuf, "server_addr,socket,capture,mic_muted");
}

TEST(VoiceUtils, VoiceNeedsAudioRefreshWhenStereoLayoutChanges)
{
	SVoiceAudioRefreshState State;
	State.m_EncoderReady = true;
	State.m_OutputReady = true;
	State.m_CaptureReady = true;
	State.m_CurrentOutputChannels = 1;
	State.m_DesiredOutputChannels = 2;

	EXPECT_TRUE(VoiceNeedsAudioRefresh(State));
}

TEST(VoiceUtils, VoiceNeedsAudioRefreshWhenUnavailableDeviceCanRetry)
{
	SVoiceAudioRefreshState State;
	State.m_EncoderReady = true;
	State.m_OutputReady = false;
	State.m_CaptureReady = true;
	State.m_OutputUnavailable = false;

	EXPECT_TRUE(VoiceNeedsAudioRefresh(State));
}

TEST(VoiceUtils, VoiceNeedsAudioRefreshStaysIdleWhenEverythingIsReady)
{
	SVoiceAudioRefreshState State;
	State.m_EncoderReady = true;
	State.m_OutputReady = true;
	State.m_CaptureReady = true;
	State.m_CurrentOutputChannels = 2;
	State.m_DesiredOutputChannels = 2;

	EXPECT_FALSE(VoiceNeedsAudioRefresh(State));
}

TEST(VoiceUtils, VoiceRuntimeResetFlagsStayIdleWhenContextAndTokenStaySame)
{
	EXPECT_EQ(VoiceRuntimeResetFlags(false, true, 0x11u, 0x11u), 0u);
}

TEST(VoiceUtils, VoiceRuntimeResetFlagsResetPeersWhenRoomTokenChanges)
{
	EXPECT_EQ(VoiceRuntimeResetFlags(false, true, 0x11u, 0x22u), VOICE_RUNTIME_RESET_PEERS);
}

TEST(VoiceUtils, VoiceRuntimeResetFlagsResetConnectionAndPeersWhenOfflineOrContextChanges)
{
	EXPECT_EQ(VoiceRuntimeResetFlags(true, true, 0x11u, 0x11u), VOICE_RUNTIME_RESET_CONNECTION | VOICE_RUNTIME_RESET_PEERS);
	EXPECT_EQ(VoiceRuntimeResetFlags(false, false, 0x11u, 0x11u), VOICE_RUNTIME_RESET_CONNECTION | VOICE_RUNTIME_RESET_PEERS);
}

TEST(VoiceUtils, VoiceUiMicStatusReportsMutedAndUnavailable)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_MicMuted = true;
	EXPECT_STREQ(VoiceUiMicStatus(Status), "muted");

	Status.m_MicMuted = false;
	Status.m_CaptureUnavailable = true;
	EXPECT_STREQ(VoiceUiMicStatus(Status), "unavailable");
}

TEST(VoiceUtils, VoiceUiServerStatusDistinguishesLocalOfflineAndConnected)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_NeedNetwork = false;
	EXPECT_STREQ(VoiceUiServerStatus(Status), "local_test");

	Status.m_NeedNetwork = true;
	EXPECT_STREQ(VoiceUiServerStatus(Status), "offline");

	Status.m_Online = true;
	Status.m_ServerAddrValid = true;
	Status.m_HaveSocket = true;
	Status.m_PingMs = 42;
	EXPECT_STREQ(VoiceUiServerStatus(Status), "connected");
}

TEST(VoiceUtils, VoiceUiRoomAndTransportStatusReflectPeerAndTraffic)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_NeedNetwork = true;
	Status.m_Online = true;
	EXPECT_STREQ(VoiceUiRoomStatus(Status), "waiting_peer");
	EXPECT_STREQ(VoiceUiTransportStatus(Status), "idle_no_peer");

	Status.m_HaveRecentPeers = true;
	EXPECT_STREQ(VoiceUiRoomStatus(Status), "matched");
	EXPECT_STREQ(VoiceUiTransportStatus(Status), "idle_with_peer");

	Status.m_TxActive = true;
	EXPECT_STREQ(VoiceUiTransportStatus(Status), "tx_active");

	Status.m_HaveRecentRx = true;
	EXPECT_STREQ(VoiceUiTransportStatus(Status), "tx_rx_active");
}

TEST(VoiceUtils, VoiceUiActionHintPointsToNextCheck)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_CaptureUnavailable = true;
	EXPECT_STREQ(VoiceUiActionHint(Status), "check_input");

	Status.m_CaptureUnavailable = false;
	Status.m_NeedNetwork = true;
	Status.m_Online = true;
	Status.m_ServerAddrValid = false;
	EXPECT_STREQ(VoiceUiActionHint(Status), "check_server");

	Status.m_ServerAddrValid = true;
	Status.m_HaveSocket = true;
	Status.m_HaveRecentPeers = false;
	EXPECT_STREQ(VoiceUiActionHint(Status), "check_room");

	Status.m_HaveRecentPeers = true;
	Status.m_TxActive = true;
	EXPECT_STREQ(VoiceUiActionHint(Status), "wait_peer");
}

TEST(VoiceUtils, VoiceUiActionHintPrefersSpecificAudioFailureGuidance)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_CaptureUnavailable = true;
	str_copy(Status.m_aAudioError, "Failed to open capture device: kAudioHardwareNotPermittedError", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiActionHint(Status), "grant_mic_permission");

	str_copy(Status.m_aAudioError, "Input device not found: 'USB Mic'", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiActionHint(Status), "select_input_device");

	Status.m_CaptureUnavailable = false;
	Status.m_OutputUnavailable = true;
	str_copy(Status.m_aAudioError, "Output device not found: 'USB DAC'", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiActionHint(Status), "select_output_device");
}

TEST(VoiceUtils, VoiceUiRouteStatusShowsSwitchingAndSelectedDeviceResults)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_AudioRefreshPending = true;
	str_copy(Status.m_aRequestedInputDevice, "USB Mic", sizeof(Status.m_aRequestedInputDevice));
	str_copy(Status.m_aRequestedOutputDevice, "USB DAC", sizeof(Status.m_aRequestedOutputDevice));
	EXPECT_STREQ(VoiceUiInputRouteStatus(Status), "switching_selected");
	EXPECT_STREQ(VoiceUiOutputRouteStatus(Status), "switching_selected");

	Status.m_AudioRefreshPending = false;
	Status.m_CaptureReady = true;
	Status.m_OutputReady = true;
	EXPECT_STREQ(VoiceUiInputRouteStatus(Status), "using_selected");
	EXPECT_STREQ(VoiceUiOutputRouteStatus(Status), "using_selected");

	Status = {};
	Status.m_Enabled = true;
	Status.m_CaptureReady = true;
	Status.m_OutputReady = true;
	EXPECT_STREQ(VoiceUiInputRouteStatus(Status), "using_default");
	EXPECT_STREQ(VoiceUiOutputRouteStatus(Status), "using_default");
}

TEST(VoiceUtils, VoiceUiRouteStatusDistinguishesPermissionAndFailure)
{
	SVoiceUiStatus Status;
	Status.m_Enabled = true;
	Status.m_CaptureUnavailable = true;
	str_copy(Status.m_aRequestedInputDevice, "USB Mic", sizeof(Status.m_aRequestedInputDevice));
	str_copy(Status.m_aAudioError, "Failed to open capture device: kAudioHardwareNotPermittedError", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiInputRouteStatus(Status), "permission_denied");
	EXPECT_STREQ(VoiceUiAudioIssueKey(Status), "permission_denied");

	str_copy(Status.m_aAudioError, "Input device not found: 'USB Mic'", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiInputRouteStatus(Status), "selected_failed");
	EXPECT_STREQ(VoiceUiAudioIssueKey(Status), "input_device_not_found");

	Status = {};
	Status.m_Enabled = true;
	Status.m_OutputUnavailable = true;
	str_copy(Status.m_aRequestedOutputDevice, "USB DAC", sizeof(Status.m_aRequestedOutputDevice));
	str_copy(Status.m_aAudioError, "Failed to open output device: device busy", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiOutputRouteStatus(Status), "selected_failed");
	EXPECT_STREQ(VoiceUiAudioIssueKey(Status), "open_output_failed");
}

TEST(VoiceUtils, VoiceUiPrimaryErrorPrefersAudioThenNetworkThenCodec)
{
	SVoiceUiStatus Status;
	str_copy(Status.m_aCodecError, "codec", sizeof(Status.m_aCodecError));
	EXPECT_STREQ(VoiceUiPrimaryError(Status), "codec");

	str_copy(Status.m_aNetworkError, "network", sizeof(Status.m_aNetworkError));
	EXPECT_STREQ(VoiceUiPrimaryError(Status), "network");

	str_copy(Status.m_aAudioError, "audio", sizeof(Status.m_aAudioError));
	EXPECT_STREQ(VoiceUiPrimaryError(Status), "audio");
}

TEST(VoiceUtils, VoiceAudioErrorLooksLikeMacPermissionDenied)
{
	EXPECT_TRUE(VoiceAudioErrorLooksLikePermissionDenied("Failed to open capture device: kAudioHardwareNotPermittedError"));
	EXPECT_TRUE(VoiceAudioErrorLooksLikePermissionDenied("Failed to open capture device: microphone access not authorized"));
	EXPECT_FALSE(VoiceAudioErrorLooksLikePermissionDenied("Failed to open capture device: device busy"));
}

TEST(VoiceUtils, ClassifyVoiceAudioIssueRecognizesDeviceFailurePaths)
{
	SVoiceUiStatus Status;

	str_copy(Status.m_aAudioError, "Input device not found: 'USB Mic'", sizeof(Status.m_aAudioError));
	EXPECT_EQ(ClassifyVoiceAudioIssue(Status), EVoiceAudioIssue::INPUT_DEVICE_NOT_FOUND);
	EXPECT_STREQ(VoiceUiAudioFailureHint(Status), "select_input_device");

	str_copy(Status.m_aAudioError, "No output devices available", sizeof(Status.m_aAudioError));
	EXPECT_EQ(ClassifyVoiceAudioIssue(Status), EVoiceAudioIssue::NO_OUTPUT_DEVICES);
	EXPECT_STREQ(VoiceUiAudioFailureHint(Status), "select_output_device");

	str_copy(Status.m_aAudioError, "Failed to open capture device: device busy", sizeof(Status.m_aAudioError));
	EXPECT_EQ(ClassifyVoiceAudioIssue(Status), EVoiceAudioIssue::OPEN_CAPTURE_FAILED);
	EXPECT_STREQ(VoiceUiAudioFailureHint(Status), "retry_input_open");

	str_copy(Status.m_aAudioError, "Failed to init audio backend 'coreaudio': unavailable", sizeof(Status.m_aAudioError));
	EXPECT_EQ(ClassifyVoiceAudioIssue(Status), EVoiceAudioIssue::BACKEND_INIT_FAILED);
	EXPECT_STREQ(VoiceUiAudioFailureHint(Status), "check_audio_backend");
}

TEST(VoiceUtils, ClassifyVoiceAudioIssueMapsMacPermissionToHint)
{
	SVoiceUiStatus Status;
	str_copy(Status.m_aAudioError, "Failed to open capture device: kAudioHardwareNotPermittedError", sizeof(Status.m_aAudioError));

	EXPECT_EQ(ClassifyVoiceAudioIssue(Status), EVoiceAudioIssue::PERMISSION_DENIED);
	EXPECT_STREQ(VoiceUiAudioFailureHint(Status), "grant_mic_permission");
}

TEST(VoiceUtils, VoiceShouldIgnoreDistanceRespectsConfigAndSharedGroup)
{
	EXPECT_TRUE(VoiceShouldIgnoreDistance(true, false, 0x11u, 0x22u));
	EXPECT_FALSE(VoiceShouldIgnoreDistance(false, false, 0x11u, 0x11u));
	EXPECT_TRUE(VoiceShouldIgnoreDistance(false, true, 0x11u, 0x11u));
	EXPECT_FALSE(VoiceShouldIgnoreDistance(false, true, 0x00u, 0x00u));
	EXPECT_FALSE(VoiceShouldIgnoreDistance(false, true, 0x11u, 0x22u));
	EXPECT_TRUE(VoiceShouldIgnoreDistance(false, true, 0x40000011u, 0x00000011u));
}

TEST(VoiceUtils, VoiceResolveListenerPositionUsesSpecPositionOnlyWhenEnabled)
{
	const vec2 LocalPos(10.0f, 20.0f);
	const vec2 SpecPos(30.0f, 40.0f);

	EXPECT_EQ(VoiceResolveListenerPosition(LocalPos, false, SpecPos, true), LocalPos);
	EXPECT_EQ(VoiceResolveListenerPosition(LocalPos, true, SpecPos, false), LocalPos);
	EXPECT_EQ(VoiceResolveListenerPosition(LocalPos, true, SpecPos, true), SpecPos);
}

TEST(VoiceUtils, EvaluateVoiceReceiveAudibilityBlocksSelfUnlessTestServer)
{
	SVoiceReceiveAudibilityContext Context;
	Context.m_IsSelf = true;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "self"), EVoiceReceiveAudibility::DROP_SELF);

	Context.m_TestServer = true;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "self"), EVoiceReceiveAudibility::ALLOW);
}

TEST(VoiceUtils, EvaluateVoiceReceiveAudibilityAppliesVisibilityRules)
{
	SVoiceReceiveAudibilityContext Context;
	Context.m_VisibilityMode = 0;
	Context.m_SenderActive = false;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::DROP_INACTIVE);

	Context.m_IgnoreDistance = true;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::ALLOW);

	Context = {};
	Context.m_VisibilityMode = 1;
	Context.m_SenderOtherTeam = true;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::DROP_OTHER_TEAM);

	Context.m_HearPeoplesInSpectate = true;
	Context.m_SenderActive = false;
	Context.m_SenderSpec = false;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::ALLOW);
}

TEST(VoiceUtils, EvaluateVoiceReceiveAudibilityAppliesMuteListsAndVad)
{
	SVoiceReceiveAudibilityContext Context;
	Context.m_SenderActive = true;
	Context.m_pMuteList = "peer";
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::DROP_MUTED);

	Context = {};
	Context.m_SenderActive = true;
	Context.m_ListMode = 1;
	Context.m_pWhitelist = "allowed";
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::DROP_NOT_WHITELISTED);
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "allowed"), EVoiceReceiveAudibility::ALLOW);

	Context = {};
	Context.m_SenderActive = true;
	Context.m_ListMode = 2;
	Context.m_pBlacklist = "peer";
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::DROP_BLACKLISTED);

	Context = {};
	Context.m_SenderActive = true;
	Context.m_SenderUsesVad = true;
	Context.m_HearVad = false;
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::DROP_VAD_BLOCKED);

	Context.m_pVadAllow = "peer";
	EXPECT_EQ(EvaluateVoiceReceiveAudibility(Context, "peer"), EVoiceReceiveAudibility::ALLOW);
}

TEST(VoiceUtils, VoiceIsPacketWithinAudibleRadiusRespectsDistanceAndOverride)
{
	const vec2 LocalPos(0.0f, 0.0f);
	const vec2 NearPos(16.0f, 0.0f);
	const vec2 FarPos(128.0f, 0.0f);

	EXPECT_TRUE(VoiceIsPacketWithinAudibleRadius(LocalPos, NearPos, 32.0f, false));
	EXPECT_FALSE(VoiceIsPacketWithinAudibleRadius(LocalPos, FarPos, 32.0f, false));
	EXPECT_TRUE(VoiceIsPacketWithinAudibleRadius(LocalPos, FarPos, 32.0f, true));
}

TEST(VoiceUtils, VoiceAudioDeviceConfigEqualsForIdenticalRequests)
{
	SVoiceAudioDeviceConfig Left;
	str_copy(Left.m_aBackend, "pipewire", sizeof(Left.m_aBackend));
	str_copy(Left.m_aInputDevice, "Mic A", sizeof(Left.m_aInputDevice));
	str_copy(Left.m_aOutputDevice, "Headset B", sizeof(Left.m_aOutputDevice));
	Left.m_OutputStereo = true;

	SVoiceAudioDeviceConfig Right = Left;
	EXPECT_TRUE(VoiceAudioDeviceConfigEquals(Left, Right));
	EXPECT_EQ(VoiceDesiredOutputChannels(Left), 2);
}

TEST(VoiceUtils, VoiceAudioDeviceConfigEqualsDetectsAnyFieldChange)
{
	SVoiceAudioDeviceConfig Base;
	str_copy(Base.m_aBackend, "coreaudio", sizeof(Base.m_aBackend));
	str_copy(Base.m_aInputDevice, "Built-in Microphone", sizeof(Base.m_aInputDevice));
	str_copy(Base.m_aOutputDevice, "Built-in Output", sizeof(Base.m_aOutputDevice));
	Base.m_OutputStereo = false;

	SVoiceAudioDeviceConfig Changed = Base;
	str_copy(Changed.m_aBackend, "dummy", sizeof(Changed.m_aBackend));
	EXPECT_FALSE(VoiceAudioDeviceConfigEquals(Base, Changed));

	Changed = Base;
	str_copy(Changed.m_aInputDevice, "USB Mic", sizeof(Changed.m_aInputDevice));
	EXPECT_FALSE(VoiceAudioDeviceConfigEquals(Base, Changed));

	Changed = Base;
	str_copy(Changed.m_aOutputDevice, "USB DAC", sizeof(Changed.m_aOutputDevice));
	EXPECT_FALSE(VoiceAudioDeviceConfigEquals(Base, Changed));

	Changed = Base;
	Changed.m_OutputStereo = true;
	EXPECT_FALSE(VoiceAudioDeviceConfigEquals(Base, Changed));
	EXPECT_EQ(VoiceDesiredOutputChannels(Base), 1);
}

TEST(VoiceUtils, BuildVoiceDeviceDropdownEntriesKeepsDefaultAndDeduplicatesDevices)
{
	std::vector<std::string> vDetectedDeviceNames = {"Built-in Microphone", "USB Mic", "usb mic", "", "Line In"};
	std::vector<SVoiceDeviceDropdownEntry> vEntries;

	BuildVoiceDeviceDropdownEntries(vDetectedDeviceNames, "", "Default", "Disconnected", vEntries);

	ASSERT_EQ(vEntries.size(), 4u);
	EXPECT_EQ(vEntries[0].m_DisplayName, "Default");
	EXPECT_EQ(vEntries[0].m_ConfigValue, "");
	EXPECT_EQ(vEntries[1].m_ConfigValue, "Built-in Microphone");
	EXPECT_EQ(vEntries[2].m_ConfigValue, "USB Mic");
	EXPECT_EQ(vEntries[3].m_ConfigValue, "Line In");
	EXPECT_EQ(VoiceFindSelectedDeviceIndex(vEntries, ""), 0);
	EXPECT_EQ(VoiceFindSelectedDeviceIndex(vEntries, "usb mic"), 2);
}

TEST(VoiceUtils, BuildVoiceDeviceDropdownEntriesPreservesDisconnectedCurrentDevice)
{
	std::vector<std::string> vDetectedDeviceNames = {"Built-in Output", "Headset"};
	std::vector<SVoiceDeviceDropdownEntry> vEntries;

	BuildVoiceDeviceDropdownEntries(vDetectedDeviceNames, "USB DAC", "Default", "Disconnected", vEntries);

	ASSERT_EQ(vEntries.size(), 4u);
	EXPECT_EQ(vEntries.back().m_DisplayName, "USB DAC (Disconnected)");
	EXPECT_EQ(vEntries.back().m_ConfigValue, "USB DAC");
	EXPECT_TRUE(vEntries.back().m_Disconnected);
	EXPECT_EQ(VoiceFindSelectedDeviceIndex(vEntries, "USB DAC"), 3);
}

TEST(VoiceUtils, BuildVoiceDeviceDropdownEntriesDoesNotDuplicateCurrentDeviceWhenCaseDiffers)
{
	std::vector<std::string> vDetectedDeviceNames = {"USB DAC", "Built-in Output"};
	std::vector<SVoiceDeviceDropdownEntry> vEntries;

	BuildVoiceDeviceDropdownEntries(vDetectedDeviceNames, "usb dac", "Default", "Disconnected", vEntries);

	ASSERT_EQ(vEntries.size(), 3u);
	EXPECT_FALSE(vEntries[1].m_Disconnected);
	EXPECT_EQ(vEntries[1].m_ConfigValue, "USB DAC");
	EXPECT_EQ(VoiceFindSelectedDeviceIndex(vEntries, "usb dac"), 1);
}
