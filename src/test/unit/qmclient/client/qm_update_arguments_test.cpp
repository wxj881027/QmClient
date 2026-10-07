#include <gtest/gtest.h>
#include <qm/update/updater_arguments.h>

#include <filesystem>
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
