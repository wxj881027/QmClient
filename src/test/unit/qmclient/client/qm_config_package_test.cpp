#include <game/client/components/qmclient/config_package.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
	qm_config_package::SPackage Package()
	{
		qm_config_package::SPackage Result;
		Result.m_ClientVersion = "3.3-preview.1";
		Result.m_ExportedAt = "2026-10-09T08:00:00Z";
		Result.m_vFiles = {
			{"qmclient/settings.cfg", "qm_fast_input 1\r\nbind q \"exec scripts/action.cfg\"\r\n"},
			{"qmclient/qmclient_profiles.cfg", "add_profile 1 2 3 4 \"default\" \"名字\" \"\"\n"},
			{"qmclient/qmclient_chatbinds.cfg", "bindchat \"!test\" \"echo example\"\n"},
			{"qmclient/qmclient_warlist.cfg", ""}};
		return Result;
	}
}

TEST(QmConfigPackage, RoundTripPreservesMetadataOriginalBytesAndCustomDirectoryStructure)
{
	auto Original = Package();
	Original.m_vFiles.push_back({"scripts/sub/action.cfg", std::string("\xef\xbb\xbf") + "bind mouse1 \"+fire; echo \\\"quoted\\\"\"\r\n"});
	Original.m_vFiles.push_back({"autoexec_client.cfg", "exec scripts/sub/action.cfg\n"});
	std::string Json, Error;
	ASSERT_TRUE(qm_config_package::Encode(Original, Json, Error)) << Error;
	qm_config_package::SPackage Restored;
	ASSERT_TRUE(qm_config_package::Decode(Json, Restored, Error)) << Error;
	EXPECT_EQ(Restored.m_ClientVersion, Original.m_ClientVersion);
	EXPECT_EQ(Restored.m_ExportedAt, Original.m_ExportedAt);
	ASSERT_EQ(Restored.m_vFiles.size(), Original.m_vFiles.size());
	for(size_t Index = 0; Index < Original.m_vFiles.size(); ++Index)
	{
		EXPECT_EQ(Restored.m_vFiles[Index].m_Path, Original.m_vFiles[Index].m_Path);
		EXPECT_EQ(Restored.m_vFiles[Index].m_Content, Original.m_vFiles[Index].m_Content);
	}
}

TEST(QmConfigPackage, UnsafeAndLegacyConfigurationPathsAreRejected)
{
	for(const char *pPath : {"../settings.cfg", "scripts/../settings.cfg", "/settings.cfg", "C:/settings.cfg", "scripts\\action.cfg", "scripts//action.cfg", "scripts/con.cfg", "scripts/aux/action.cfg", "scripts/dir./action.cfg", "settings_ddnet.cfg", "qmclient/settings_qmclient.cfg", "QMCLIENT/settings.cfg", "qmclient/builtinscripts/action.cfg", "qmclient/config_packages/backup.cfg", "scripts/action.txt"})
	{
		SCOPED_TRACE(pPath);
		EXPECT_FALSE(qm_config_package::IsConfigPath(pPath));
	}
	EXPECT_TRUE(qm_config_package::IsConfigPath("scripts/自定义/action.cfg"));
	EXPECT_TRUE(qm_config_package::IsConfigPath("autoexec_client.cfg"));
}

TEST(QmConfigPackage, CaseAliasedDuplicatePathsCannotOverwriteTheSameWindowsFile)
{
	auto Original = Package();
	Original.m_vFiles.push_back({"scripts/action.cfg", "echo first\n"});
	Original.m_vFiles.push_back({"Scripts/Action.cfg", "echo second\n"});
	std::string Json = "unchanged", Error;
	EXPECT_FALSE(qm_config_package::Encode(Original, Json, Error));
	EXPECT_EQ(Json, "unchanged");
}

TEST(QmConfigPackage, MissingManagedFileCannotBeImportedAsACompleteConfiguration)
{
	auto Original = Package();
	Original.m_vFiles.pop_back();
	std::string Json, Error;
	EXPECT_FALSE(qm_config_package::Encode(Original, Json, Error));
}

TEST(QmConfigPackage, OnlyRecoveryBackupsMayRecordAnOriginallyMissingFile)
{
	auto Original = Package();
	Original.m_vFiles.push_back({"scripts/new.cfg", {}, false});
	std::string Json, Error;
	EXPECT_FALSE(qm_config_package::Encode(Original, Json, Error));
	Original.m_Backup = true;
	ASSERT_TRUE(qm_config_package::Encode(Original, Json, Error)) << Error;
	qm_config_package::SPackage Restored;
	ASSERT_TRUE(qm_config_package::Decode(Json, Restored, Error));
	EXPECT_TRUE(Restored.m_Backup);
	EXPECT_FALSE(Restored.m_vFiles.back().m_Present);
}

TEST(QmConfigPackage, InvalidInputLeavesPreviouslyPreviewedPackageUnchanged)
{
	auto Previous = Package();
	std::string Error;
	for(const char *pJson : {"{}", "[]", "{\"format\":\"qmconfig\",\"schema_version\":2}", "not json"})
	{
		SCOPED_TRACE(pJson);
		EXPECT_FALSE(qm_config_package::Decode(pJson, Previous, Error));
		EXPECT_EQ(Previous.m_ClientVersion, "3.3-preview.1");
		EXPECT_EQ(Previous.m_vFiles.size(), 4u);
	}
}

TEST(QmConfigPackage, InvalidBase64CannotPublishAPreview)
{
	auto Original = Package();
	std::string Json, Error;
	ASSERT_TRUE(qm_config_package::Encode(Original, Json, Error));
	const auto Content = Json.find("content_base64");
	ASSERT_NE(Content, std::string::npos);
	const auto FirstByte = Json.find('"', Json.find(':', Content)) + 1;
	Json[FirstByte] = '!';
	qm_config_package::SPackage Restored;
	EXPECT_FALSE(qm_config_package::Decode(Json, Restored, Error));
	EXPECT_TRUE(Restored.m_vFiles.empty());
}

TEST(QmConfigPackage, OversizedFileCannotBeExported)
{
	auto Original = Package();
	Original.m_vFiles[0].m_Content.assign(qm_config_package::MAX_FILE_BYTES + 1, 'a');
	std::string Json, Error;
	EXPECT_FALSE(qm_config_package::Encode(Original, Json, Error));
}

TEST(QmConfigPackage, EmbeddedNulInAPathCannotBypassPathValidation)
{
	auto Original = Package();
	std::string Path = "scripts/action.cfg";
	Path.push_back('\0');
	Path += "/../hidden.cfg";
	Original.m_vFiles.push_back({Path, "echo hidden"});
	std::string Json, Error;
	EXPECT_FALSE(qm_config_package::Encode(Original, Json, Error));
}

TEST(QmConfigPackage, UnsupportedSchemaCannotPublishAPreview)
{
	std::string Json, Error;
	ASSERT_TRUE(qm_config_package::Encode(Package(), Json, Error));
	const auto Schema = Json.find("schema_version");
	ASSERT_NE(Schema, std::string::npos);
	const auto Value = Json.find('1', Schema);
	ASSERT_NE(Value, std::string::npos);
	Json[Value] = '2';
	qm_config_package::SPackage Restored;
	EXPECT_FALSE(qm_config_package::Decode(Json, Restored, Error));
	EXPECT_TRUE(Restored.m_vFiles.empty());
}
