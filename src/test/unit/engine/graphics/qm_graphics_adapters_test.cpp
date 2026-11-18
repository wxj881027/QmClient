#include <engine/client/qm_graphics_adapters.h>

#include <gtest/gtest.h>

#include <new>

TEST(QmGraphicsAdapters, VirtualDisplayCopyOfPhysicalGpuDoesNotCountAsSecondAdapter)
{
	STWGraphicGpu List;
	QmPopulateOpenGLGpuList(List, "NVIDIA GeForce RTX 4060 Ti/PCIe/SSE2", [] {
		std::vector<std::string> Names;
		for(const auto Path : {L"\\\\?\\PCI#VEN_10DE&DEV_2803#physical", L"\\\\?\\ROOT#DISPLAY#0000"})
			if(QmIsSystemGraphicsHardwareAdapter(Path))
				Names.emplace_back("NVIDIA GeForce RTX 4060 Ti");
		return Names;
	});
	ASSERT_EQ(List.m_vGpus.size(), 1u);
	EXPECT_FALSE(List.m_CanSelect);
}

TEST(QmGraphicsAdapters, HardwareFilterKeepsSameNamedPhysicalCardsAndUnknownDevicePaths)
{
	EXPECT_TRUE(QmIsSystemGraphicsHardwareAdapter(L"\\\\?\\PCI#VEN_10DE#first"));
	EXPECT_TRUE(QmIsSystemGraphicsHardwareAdapter(L"\\\\?\\PCI#VEN_10DE#second"));
	EXPECT_TRUE(QmIsSystemGraphicsHardwareAdapter(L""));
	EXPECT_FALSE(QmIsSystemGraphicsHardwareAdapter(L"\\\\?\\root#display#0000"));
	EXPECT_FALSE(QmIsSystemGraphicsHardwareAdapter(L"ROOT\\DISPLAY\\0000"));
	EXPECT_TRUE(QmIsSystemGraphicsHardwareAdapter(L"\\\\?\\ROOT#OTHER#0000"));
}

TEST(QmGraphicsAdapters, DefaultSelectableListHasInitializedAutomaticDevice)
{
	STWGraphicGpu List;
	EXPECT_TRUE(List.m_CanSelect);
	EXPECT_TRUE(List.m_vGpus.empty());
	EXPECT_STREQ(List.m_AutoGpu.m_aName, "");
	EXPECT_EQ(List.m_AutoGpu.m_GpuType, STWGraphicGpu::GRAPHICS_GPU_TYPE_INVALID);
}

TEST(QmGraphicsAdapters, OpenGLListsSystemHardwareSeparatelyFromActualRenderer)
{
	STWGraphicGpu List;
	int Reads = 0;
	QmPopulateOpenGLGpuList(List, "NVIDIA GeForce RTX 4060/PCIe/SSE2", [&] {
		++Reads;
		return std::vector<std::string>{"Intel UHD Graphics", "NVIDIA GeForce RTX 4060"};
	});
	EXPECT_EQ(Reads, 1);
	EXPECT_FALSE(List.m_CanSelect);
	ASSERT_EQ(List.m_vGpus.size(), 2u);
	EXPECT_STREQ(List.m_vGpus[0].m_aName, "Intel UHD Graphics");
	EXPECT_STREQ(List.m_vGpus[1].m_aName, "NVIDIA GeForce RTX 4060");
	EXPECT_STREQ(List.m_AutoGpu.m_aName, "NVIDIA GeForce RTX 4060/PCIe/SSE2");
	EXPECT_EQ(List.m_AutoGpu.m_GpuType, STWGraphicGpu::GRAPHICS_GPU_TYPE_INVALID);
	for(const auto &Gpu : List.m_vGpus)
		EXPECT_EQ(Gpu.m_GpuType, STWGraphicGpu::GRAPHICS_GPU_TYPE_INVALID);
}

TEST(QmGraphicsAdapters, SameNamedPhysicalAdaptersRemainSeparateEntries)
{
	STWGraphicGpu List;
	QmPopulateOpenGLGpuList(List, "Actual renderer", [] {
		return std::vector<std::string>{"NVIDIA GeForce RTX 4090", "NVIDIA GeForce RTX 4090"};
	});
	ASSERT_EQ(List.m_vGpus.size(), 2u);
	EXPECT_STREQ(List.m_vGpus[0].m_aName, "NVIDIA GeForce RTX 4090");
	EXPECT_STREQ(List.m_vGpus[1].m_aName, "NVIDIA GeForce RTX 4090");
	EXPECT_STREQ(List.m_AutoGpu.m_aName, "Actual renderer");
}

TEST(QmGraphicsAdapters, NoHardwarePreservesActualSoftwareRenderer)
{
	STWGraphicGpu List;
	QmPopulateOpenGLGpuList(List, "llvmpipe (LLVM 18.1.0, 256 bits)", [] { return std::vector<std::string>{}; });
	EXPECT_FALSE(List.m_CanSelect);
	EXPECT_TRUE(List.m_vGpus.empty());
	EXPECT_STREQ(List.m_AutoGpu.m_aName, "llvmpipe (LLVM 18.1.0, 256 bits)");
	EXPECT_EQ(List.m_AutoGpu.m_GpuType, STWGraphicGpu::GRAPHICS_GPU_TYPE_INVALID);
}

TEST(QmGraphicsAdapters, ReinitializationClearsVulkanCandidatesAndPreviousRenderer)
{
	STWGraphicGpu List;
	EXPECT_TRUE(List.m_CanSelect);
	STWGraphicGpu::STWGraphicGpuItem Previous{};
	str_copy(Previous.m_aName, "Previous Vulkan device");
	Previous.m_GpuType = STWGraphicGpu::GRAPHICS_GPU_TYPE_DISCRETE;
	List.m_vGpus.push_back(Previous);
	List.m_AutoGpu = Previous;
	QmPopulateOpenGLGpuList(List, "OpenGL renderer", [] { return std::vector<std::string>{"Detected hardware"}; });
	ASSERT_EQ(List.m_vGpus.size(), 1u);
	EXPECT_STREQ(List.m_vGpus[0].m_aName, "Detected hardware");
	EXPECT_FALSE(List.m_CanSelect);
	QmPopulateOpenGLGpuList(List, "New renderer", [] { return std::vector<std::string>{}; });
	EXPECT_TRUE(List.m_vGpus.empty());
	EXPECT_STREQ(List.m_AutoGpu.m_aName, "New renderer");
	EXPECT_EQ(List.m_AutoGpu.m_GpuType, STWGraphicGpu::GRAPHICS_GPU_TYPE_INVALID);
	EXPECT_FALSE(List.m_CanSelect);
}

TEST(QmGraphicsAdapters, MissingRendererResetsPreviousAutomaticDevice)
{
	STWGraphicGpu List;
	QmPopulateOpenGLGpuList(List, "Previous renderer", [] { return std::vector<std::string>{}; });
	QmPopulateOpenGLGpuList(List, nullptr, [] { return std::vector<std::string>{}; });
	EXPECT_STREQ(List.m_AutoGpu.m_aName, "");
	EXPECT_FALSE(List.m_CanSelect);
}

TEST(QmGraphicsAdapters, HardwareEnumerationAllocationFailurePreservesCurrentRenderer)
{
	STWGraphicGpu List;
	QmPopulateOpenGLGpuList(List, "Previous renderer", [] { return std::vector<std::string>{"Previous GPU"}; });
	QmPopulateOpenGLGpuList(List, "Current renderer", []() -> std::vector<std::string> { throw std::bad_alloc(); });
	EXPECT_STREQ(List.m_AutoGpu.m_aName, "Current renderer");
	EXPECT_TRUE(List.m_vGpus.empty());
	EXPECT_FALSE(List.m_CanSelect);
}
