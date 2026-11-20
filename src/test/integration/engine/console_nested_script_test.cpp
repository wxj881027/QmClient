#include <engine/console.h>
#include <engine/kernel.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <memory>
#include <string>
#include <vector>

TEST(ConsoleNestedScript, ExecutesAllowedIncludesInOrderAndResumesTheCallingLine)
{
	CTestInfo Info;
	auto pStorage = Info.CreateTestStorage();
	ASSERT_NE(pStorage, nullptr);
	std::vector<int> vRecorded;
	const std::unique_ptr<IKernel> pKernel(IKernel::Create());
	auto pConsole = CreateConsole(CFGFLAG_CLIENT);
	pKernel->RegisterInterface(pStorage.get(), false);
	pKernel->RegisterInterface(pConsole.get(), false);
	pConsole->Init();

	pConsole->Register(
		"record_script_value", "i[value]", CFGFLAG_CLIENT,
		[](IConsole::IResult *pResult, void *pUser) {
			static_cast<std::vector<int> *>(pUser)->push_back(pResult->GetInteger(0));
		},
		&vRecorded, "");

	constexpr int LastAllowedIndex = IConsole::FILE_RECURSION_LIMIT;
	std::vector<std::string> vFilenames;
	for(int Index = 0; Index <= LastAllowedIndex + 1; ++Index)
		vFilenames.push_back("console-nested-" + std::to_string(Index) + ".cfg");
	for(int Index = 0; Index <= LastAllowedIndex + 1; ++Index)
	{
		char aContent[128];
		if(Index <= LastAllowedIndex)
			str_format(aContent, sizeof(aContent), "exec %s; record_script_value %d\n", vFilenames[Index + 1].c_str(), Index);
		else
			str_format(aContent, sizeof(aContent), "record_script_value %d\n", Index);
		IOHANDLE File = pStorage->OpenFile(vFilenames[Index].c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
		ASSERT_NE(File, nullptr);
		const auto Length = str_length(aContent);
		const auto Written = io_write(File, aContent, Length);
		const int CloseResult = io_close(File);
		ASSERT_EQ(Written, static_cast<size_t>(Length));
		ASSERT_EQ(CloseResult, 0);
	}

	const std::string FirstCommand = "exec " + vFilenames.front() + "; record_script_value -1";
	pConsole->ExecuteLine(FirstCommand.c_str());

	std::vector<int> vExpected;
	for(int Index = LastAllowedIndex; Index >= -1; --Index)
		vExpected.push_back(Index);
	EXPECT_EQ(vRecorded, vExpected);

	for(const auto &Filename : vFilenames)
		EXPECT_TRUE(pStorage->RemoveFile(Filename.c_str(), IStorage::TYPE_SAVE));
}
