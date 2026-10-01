#include <gtest/gtest.h>
#include <test/test.h>

#include <string>

TEST(QmStoragePathContract, ExecutableCandidatesPrecedeCurrentDirectoryFallbacks)
{
	const std::string Source = ReadTestSourceFile("src/engine/shared/storage.cpp");
	const size_t LoadStart = Source.find("bool LoadPathsFromFile(");
	const size_t DataStart = Source.find("void FindDataDirectory(");
	ASSERT_NE(LoadStart, std::string::npos);
	ASSERT_NE(DataStart, std::string::npos);
	const size_t ExecutableStorage = Source.find("StoragePathFromExecutable", LoadStart);
	const size_t CurrentDirectoryStorage = Source.find("io_open(\"storage.cfg\"", LoadStart);
	const size_t ExecutableData = Source.find("StoragePathFromExecutable", DataStart);
	const size_t CurrentDirectoryData = Source.find("fs_is_dir(\"data/mapres\"", DataStart);
	ASSERT_NE(ExecutableStorage, std::string::npos);
	ASSERT_NE(CurrentDirectoryStorage, std::string::npos);
	ASSERT_NE(ExecutableData, std::string::npos);
	ASSERT_NE(CurrentDirectoryData, std::string::npos);
	EXPECT_LT(ExecutableStorage, CurrentDirectoryStorage);
	EXPECT_LT(ExecutableData, CurrentDirectoryData);
}
