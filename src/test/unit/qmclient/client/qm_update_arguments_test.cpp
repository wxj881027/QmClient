#include <gtest/gtest.h>
#include <qm/update/updater_arguments.h>
#include <qm/update/updater_session.h>
#include <test/test.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
	std::vector<std::wstring> RequiredArguments(const wchar_t *pParentPid)
	{
		const std::wstring Session = std::wstring(pParentPid) == L"0" ? L"42" : pParentPid;
		const std::filesystem::path Save = std::filesystem::current_path() / "qm-update-test-save";
		return {
			L"QmClient-Updater.exe",
			L"--parent-pid",
			pParentPid,
			L"--package",
			(Save / (L"QmClient-windows.zip." + Session + L".tmp")).wstring(),
			L"--package-signature",
			(Save / (L"QmClient-windows.zip.sig." + Session + L".tmp")).wstring(),
			L"--manifest",
			(Save / (L"QmClient-windows-update.json." + Session + L".tmp")).wstring(),
			L"--manifest-signature",
			(Save / (L"QmClient-windows-update.json.sig." + Session + L".tmp")).wstring(),
			L"--install",
			(std::filesystem::current_path() / "qm-update-test-install").wstring(),
		};
	}

	std::filesystem::path UpdaterPath(uint32_t SessionPid)
	{
		return std::filesystem::current_path() / "qm-update-test-save" / "qmclient" / (L"QmClient-Updater-" + std::to_wstring(SessionPid) + L".exe");
	}

	std::vector<std::wstring> SetupArguments(const wchar_t *pPid)
	{
		auto Arguments = RequiredArguments(pPid);
		const auto Save = std::filesystem::current_path() / "qm-update-test-save";
		Arguments[4] = (Save / "QmClient-Setup-42.exe").wstring();
		Arguments[6] = (Save / "QmClient-Setup.exe.sig.42.tmp").wstring();
		Arguments[8] = (Save / "QmClient-windows-setup-update.json.42.tmp").wstring();
		Arguments[10] = (Save / "QmClient-windows-setup-update.json.sig.42.tmp").wstring();
		Arguments.emplace_back(L"--setup");
		return Arguments;
	}
}

TEST(QmUpdateArguments, ElevatedRelaunchAcceptsZeroParentPid)
{
	auto ArgumentsList = RequiredArguments(L"0");
	ArgumentsList.emplace_back(L"--qm-elevated");
	QmUpdate::SArguments Arguments;
	ASSERT_TRUE(QmUpdate::ParseArguments(ArgumentsList, Arguments));
	EXPECT_EQ(Arguments.m_ParentPid, 0U);
	EXPECT_TRUE(Arguments.m_Elevated);
	EXPECT_TRUE(QmUpdate::ValidateSessionPaths(Arguments, UpdaterPath(42)));
}

TEST(QmUpdateArguments, InitialLaunchRequiresNonZeroParentPid)
{
	QmUpdate::SArguments Arguments;
	EXPECT_FALSE(QmUpdate::ParseArguments(RequiredArguments(L"0"), Arguments));
	EXPECT_TRUE(QmUpdate::ParseArguments(RequiredArguments(L"42"), Arguments));
	EXPECT_EQ(Arguments.m_ParentPid, 42U);
	EXPECT_FALSE(Arguments.m_Elevated);
	EXPECT_TRUE(QmUpdate::ValidateSessionPaths(Arguments, UpdaterPath(42)));
}

TEST(QmUpdateArguments, RejectsMismatchedSessionPathsAndDuplicateOptions)
{
	QmUpdate::SArguments Arguments;
	auto ArgumentsList = RequiredArguments(L"42");
	ASSERT_TRUE(QmUpdate::ParseArguments(ArgumentsList, Arguments));
	EXPECT_FALSE(QmUpdate::ValidateSessionPaths(Arguments, UpdaterPath(41)));
	Arguments.m_ManifestSignature = (std::filesystem::current_path() / "other" / "QmClient-windows-update.json.sig.42.tmp").wstring();
	EXPECT_FALSE(QmUpdate::ValidateSessionPaths(Arguments, UpdaterPath(42)));

	ArgumentsList.emplace_back(L"--package");
	ArgumentsList.emplace_back((std::filesystem::current_path() / "qm-update-test-save" / "QmClient-windows.zip.42.tmp").wstring());
	EXPECT_FALSE(QmUpdate::ParseArguments(ArgumentsList, Arguments));
}

TEST(QmUpdateArguments, RejectsMalformedOrOverflowingParentPid)
{
	for(const wchar_t *pPid : {L"-1", L"1x", L"4294967296"})
	{
		QmUpdate::SArguments Arguments;
		auto ArgumentsList = RequiredArguments(pPid);
		ArgumentsList.emplace_back(L"--qm-elevated");
		EXPECT_FALSE(QmUpdate::ParseArguments(ArgumentsList, Arguments));
	}
}

TEST(QmUpdateArguments, PermissionDetectionUsesStableMarker)
{
	EXPECT_TRUE(QmUpdate::IsPermissionError("QM_UPDATE_PERMISSION_DENIED: localized system message"));
	EXPECT_FALSE(QmUpdate::IsPermissionError("rollback failed: QM_UPDATE_PERMISSION_DENIED: localized system message"));
	EXPECT_FALSE(QmUpdate::IsPermissionError("Access is denied"));
	EXPECT_FALSE(QmUpdate::IsPermissionError("permission denied"));
}

TEST(QmUpdateArguments, NormalAndPortableSevenZipRequireMatchingSignatureAndManifestPaths)
{
	for(const auto &Stem : {L"QmClient-windows", L"QmClient-windows-portable"})
	{
		QmUpdate::SArguments Arguments;
		ASSERT_TRUE(QmUpdate::ParseArguments(RequiredArguments(L"42"), Arguments));
		const auto Directory = std::filesystem::path(Arguments.m_Package).parent_path();
		Arguments.m_Package = (Directory / (std::wstring(Stem) + L".7z.42.tmp")).wstring();
		Arguments.m_PackageSignature = (Directory / (std::wstring(Stem) + L".7z.sig.42.tmp")).wstring();
		Arguments.m_Manifest = (Directory / (std::wstring(Stem) + L"-7z-update.json.42.tmp")).wstring();
		Arguments.m_ManifestSignature = (Directory / (std::wstring(Stem) + L"-7z-update.json.sig.42.tmp")).wstring();
		EXPECT_TRUE(QmUpdate::ValidateSessionPaths(Arguments, UpdaterPath(42)));
		Arguments.m_ManifestSignature = (Directory / L"QmClient-windows-update.json.sig.42.tmp").wstring();
		EXPECT_FALSE(QmUpdate::ValidateSessionPaths(Arguments, UpdaterPath(42)));
	}
}

TEST(QmUpdateArguments, SetupSessionRequiresExplicitModeAndMatchingAttachments)
{
	auto ArgumentsList = SetupArguments(L"42");
	QmUpdate::SArguments Arguments;
	ASSERT_TRUE(QmUpdate::ParseArguments(ArgumentsList, Arguments));
	ASSERT_TRUE(Arguments.m_Setup);
	EXPECT_TRUE(QmUpdate::ValidateSessionPaths(Arguments, UpdaterPath(42)));
	Arguments.m_Setup = false;
	EXPECT_FALSE(QmUpdate::ValidateSessionPaths(Arguments, UpdaterPath(42)));
	Arguments.m_Setup = true;
	Arguments.m_Manifest = (std::filesystem::current_path() / "qm-update-test-save" / "QmClient-windows-update.json.42.tmp").wstring();
	EXPECT_FALSE(QmUpdate::ValidateSessionPaths(Arguments, UpdaterPath(42)));
	ArgumentsList.emplace_back(L"--setup");
	EXPECT_FALSE(QmUpdate::ParseArguments(ArgumentsList, Arguments));
}

TEST(QmUpdateArguments, ElevatedSetupRetainsItsOriginalSession)
{
	auto ArgumentsList = SetupArguments(L"0");
	ArgumentsList.emplace_back(L"--qm-elevated");
	QmUpdate::SArguments Arguments;
	ASSERT_TRUE(QmUpdate::ParseArguments(ArgumentsList, Arguments));
	EXPECT_TRUE(Arguments.m_Setup);
	EXPECT_TRUE(Arguments.m_Elevated);
	EXPECT_TRUE(QmUpdate::ValidateSessionPaths(Arguments, UpdaterPath(42)));
	EXPECT_FALSE(QmUpdate::ValidateSessionPaths(Arguments, UpdaterPath(43)));
}

TEST(QmUpdateSetupSession, RejectingSignatureCleansFilesWithoutStartingInstaller)
{
	bool Started = false;
	bool Cleaned = false;
	const auto Result = QmUpdate::RunSetupSession([] { return false; }, [&] {
		Started = true;
		return QmUpdate::ESetupResult::SUCCEEDED; }, [&] { Cleaned = true; });
	EXPECT_EQ(Result, QmUpdate::ESetupResult::FAILED);
	EXPECT_FALSE(Started);
	EXPECT_TRUE(Cleaned);
}

TEST(QmUpdateSetupSession, CompletedInstallerCleansOnlyAfterExit)
{
	bool Verified = false;
	bool Exited = false;
	bool Cleaned = false;
	const auto Result = QmUpdate::RunSetupSession([&] {
		Verified = true;
		return true; }, [&] {
		EXPECT_TRUE(Verified);
		EXPECT_FALSE(Cleaned);
		Exited = true;
		return QmUpdate::ESetupResult::SUCCEEDED; }, [&] {
		EXPECT_TRUE(Exited);
		Cleaned = true; });
	EXPECT_EQ(Result, QmUpdate::ESetupResult::SUCCEEDED);
	EXPECT_TRUE(Cleaned);
}

TEST(QmUpdateSetupSession, FailedOrCancelledInstallerStillCleansFiles)
{
	bool Cleaned = false;
	const auto Result = QmUpdate::RunSetupSession([] { return true; }, [] { return QmUpdate::ESetupResult::FAILED; }, [&] { Cleaned = true; });
	EXPECT_EQ(Result, QmUpdate::ESetupResult::FAILED);
	EXPECT_TRUE(Cleaned);
}

TEST(QmUpdateSetupSession, UnknownProcessExitRetainsRunningInstallerFiles)
{
	bool Cleaned = false;
	const auto Result = QmUpdate::RunSetupSession([] { return true; }, [] { return QmUpdate::ESetupResult::STILL_RUNNING; }, [&] { Cleaned = true; });
	EXPECT_EQ(Result, QmUpdate::ESetupResult::STILL_RUNNING);
	EXPECT_FALSE(Cleaned);
}

TEST(QmUpdateSetupSession, FailedLaunchRemovesDownloadedFilesAndPreservesUnrelatedFile)
{
	CTestInfo Info;
	const auto Directory = std::filesystem::absolute(Info.StoragePath());
	ASSERT_TRUE(std::filesystem::create_directories(Directory));
	Info.m_HasCreatedStoragePath = true;
	QmUpdate::SArguments Arguments;
	Arguments.m_Package = (Directory / "QmClient-Setup-42.exe").wstring();
	Arguments.m_PackageSignature = (Directory / "package.sig.tmp").wstring();
	Arguments.m_Manifest = (Directory / "manifest.tmp").wstring();
	Arguments.m_ManifestSignature = (Directory / "manifest.sig.tmp").wstring();
	for(const auto *pPath : {&Arguments.m_Package, &Arguments.m_PackageSignature, &Arguments.m_Manifest, &Arguments.m_ManifestSignature})
	{
		std::ofstream File{std::filesystem::path(*pPath)};
		File << "download";
		ASSERT_TRUE(File.good());
	}
	const auto Unrelated = Directory / "player.cfg";
	std::ofstream{Unrelated} << "player data";
	const auto Result = QmUpdate::RunSetupSession([] { return true; }, [] { return QmUpdate::ESetupResult::FAILED; }, [&] { EXPECT_TRUE(QmUpdate::CleanupDownloadedFiles(Arguments)); });
	EXPECT_EQ(Result, QmUpdate::ESetupResult::FAILED);
	for(const auto *pPath : {&Arguments.m_Package, &Arguments.m_PackageSignature, &Arguments.m_Manifest, &Arguments.m_ManifestSignature})
		EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(*pPath)));
	EXPECT_TRUE(std::filesystem::exists(Unrelated));
	EXPECT_TRUE(QmUpdate::CleanupDownloadedFiles(Arguments));
}

TEST(QmUpdateSetupSession, CleanupFailurePreservesSuccessfulInstallResult)
{
	CTestInfo Info;
	const auto Directory = std::filesystem::absolute(Info.StoragePath());
	ASSERT_TRUE(std::filesystem::create_directories(Directory / "locked-package"));
	Info.m_HasCreatedStoragePath = true;
	std::ofstream{Directory / "locked-package" / "child"} << "cannot remove nonempty directory";
	std::ofstream{Directory / "manifest.tmp"} << "manifest";
	QmUpdate::SArguments Arguments;
	Arguments.m_Package = (Directory / "locked-package").wstring();
	Arguments.m_Manifest = (Directory / "manifest.tmp").wstring();
	const auto Result = QmUpdate::RunSetupSession([] { return true; }, [] { return QmUpdate::ESetupResult::SUCCEEDED; }, [&] { EXPECT_FALSE(QmUpdate::CleanupDownloadedFiles(Arguments)); });
	EXPECT_EQ(Result, QmUpdate::ESetupResult::SUCCEEDED);
	EXPECT_TRUE(std::filesystem::exists(Directory / "locked-package" / "child"));
	EXPECT_FALSE(std::filesystem::exists(Directory / "manifest.tmp"));
}
