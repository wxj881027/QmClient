#include <base/crashdump.h>

#include <gtest/gtest.h>

// 报告写入端与初始化完成端共用的进程内后端状态。
extern const char *crashdump_graphics_backend_for_report();

TEST(QmCrashdumpBackendAttribution, SuccessfulBackendAndRetryReplacePreviousBackend)
{
	crashdump_set_graphics_backend(nullptr);
	EXPECT_EQ(crashdump_graphics_backend_for_report(), nullptr);
	crashdump_set_graphics_backend("Vulkan");
	ASSERT_NE(crashdump_graphics_backend_for_report(), nullptr);
	EXPECT_STREQ(crashdump_graphics_backend_for_report(), "Vulkan");

	crashdump_set_graphics_backend(nullptr);
	EXPECT_EQ(crashdump_graphics_backend_for_report(), nullptr);
	crashdump_set_graphics_backend("OpenGL");
	ASSERT_NE(crashdump_graphics_backend_for_report(), nullptr);
	EXPECT_STREQ(crashdump_graphics_backend_for_report(), "OpenGL");
	crashdump_set_graphics_backend(nullptr);
}

TEST(QmCrashdumpBackendAttribution, KnownNamesAreCanonicalAndUnknownIsNotReported)
{
	crashdump_set_graphics_backend("opengl es");
	ASSERT_NE(crashdump_graphics_backend_for_report(), nullptr);
	EXPECT_STREQ(crashdump_graphics_backend_for_report(), "OpenGL ES");
	crashdump_set_graphics_backend("unrecognized");
	EXPECT_EQ(crashdump_graphics_backend_for_report(), nullptr);
}

// QmClient: 退出阶段图形驱动故障抑制的行为合同。
//
// 背景：退出清理销毁图形后端时，NVIDIA 的 nvoglv64.dll 会在 vkDestroyDevice
// 内部访问已释放内存（见 dumps/QmClient_Crash 下的退出期报告，异常模块
// nvoglv64.dll + 0x106D939）。此时进程本来就要结束，弹窗与几十 MB 完整转储
// 对用户没有价值，因此客户端在清理前后用 crashdump_mark_shutdown_begin/end
// 限定一个“退出窗口”，窗口内落在已知图形驱动模块中的异常转为日志。
//
// 真实异常路径需要构造 EXCEPTION_POINTERS 与真实模块地址，无法在单元测试中
// 可靠复现；这里固定可确定的核心判据——模块清单匹配，以及退出窗口的状态转换。

TEST(QmCrashdumpShutdownSuppression, NvidiaOpenGlDriverIsIgnorableDuringShutdown)
{
	// 本机实际崩溃的模块（见 fatal_report 的 Exception module）。
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("nvoglv64.dll"));
}

TEST(QmCrashdumpShutdownSuppression, VendorDriversAndRuntimeLibrariesAreCovered)
{
	// 与 client.cpp 的驱动故障清单保持一致，覆盖 NV / AMD / Intel 与通用运行库。
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("nvd3dumx.dll"));
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("nvwgf2umx.dll"));
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("amdvlk64.dll"));
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("atio6axx.dll"));
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("ig9icd64.dll"));
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("igvk64.dll"));
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("opengl32.dll"));
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("vulkan-1.dll"));
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("d3d12.dll"));
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("dxgi.dll"));
}

TEST(QmCrashdumpShutdownSuppression, ModuleMatchIsCaseInsensitive)
{
	// Windows 文件名大小写不敏感；驱动上报的模块名大小写并不稳定。
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("NVOGLV64.DLL"));
	EXPECT_TRUE(crashdump_is_ignorable_shutdown_driver_module("NvOgLv64.Dll"));
}

TEST(QmCrashdumpShutdownSuppression, ProgramOwnModulesAreNeverIgnorable)
{
	// 关键负向约束：自家可执行文件与本地组件绝不能被当成驱动故障静默吞掉，
	// 否则 QmClient 自身的真实崩溃将被隐藏。
	EXPECT_FALSE(crashdump_is_ignorable_shutdown_driver_module("DDNet.exe"));
	EXPECT_FALSE(crashdump_is_ignorable_shutdown_driver_module("SDL2.dll"));
	EXPECT_FALSE(crashdump_is_ignorable_shutdown_driver_module("libfreetype.dll"));
	EXPECT_FALSE(crashdump_is_ignorable_shutdown_driver_module("steam_api.dll"));
}

TEST(QmCrashdumpShutdownSuppression, PartialAndEmptyNamesDoNotMatch)
{
	// 只允许完整文件名匹配：前缀、后缀、空串与空指针都不得命中。
	EXPECT_FALSE(crashdump_is_ignorable_shutdown_driver_module("nvoglv64"));
	EXPECT_FALSE(crashdump_is_ignorable_shutdown_driver_module("nvoglv64.dll.bak"));
	EXPECT_FALSE(crashdump_is_ignorable_shutdown_driver_module("mynvoglv64.dll"));
	EXPECT_FALSE(crashdump_is_ignorable_shutdown_driver_module(""));
	EXPECT_FALSE(crashdump_is_ignorable_shutdown_driver_module(nullptr));
}

TEST(QmCrashdumpShutdownSuppression, ShutdownWindowStartsInactive)
{
	// 初始状态：未进入退出阶段，不得抑制任何崩溃报告。
	EXPECT_FALSE(crashdump_is_shutdown_graphics_fault());
}

TEST(QmCrashdumpShutdownSuppression, ShutdownWindowCanBeReenteredAndCleared)
{
	// 进入退出窗口本身不等于发生故障；退出窗口允许重复进出，状态不得粘连。
	// 这与客户端调用形态一致：正常退出与错误恢复路径都可能触发清理。
	crashdump_mark_shutdown_begin(nullptr);
	EXPECT_FALSE(crashdump_is_shutdown_graphics_fault());
	crashdump_mark_shutdown_end();
	EXPECT_FALSE(crashdump_is_shutdown_graphics_fault());

	crashdump_mark_shutdown_begin(nullptr);
	crashdump_mark_shutdown_end();
	crashdump_mark_shutdown_begin(nullptr);
	crashdump_mark_shutdown_end();
	EXPECT_FALSE(crashdump_is_shutdown_graphics_fault());
}

TEST(QmCrashdumpShutdownSuppression, ReporterIsNotLaunchedForInvalidPaths)
{
	// 报告进程只在拿到有效报告路径时启动；无效路径必须立即失败，
	// 避免退出期无谓地再起一个进程。
	EXPECT_FALSE(crashdump_launch_reporter_if_available(nullptr));
	EXPECT_FALSE(crashdump_launch_reporter_if_available(""));
}
