// 显式联网验收工具，复用生产 HTTP、源配置、代理解析和 Rust 验签；不纳入离线 gate。
#include <base/hash.h>
#include <base/logger.h>
#include <base/system.h>

#include <engine/http.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/qm_update.h>
#include <engine/storage.h>

#include <game/client/components/qmclient/update_manifest.h>
#include <game/client/components/qmclient/update_proxy.h>
#include <game/client/components/qmclient/update_request.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using TClock = std::chrono::steady_clock;
	double Seconds(TClock::time_point Start) { return std::chrono::duration<double>(TClock::now() - Start).count(); }
	void String(CJsonWriter &Writer, const char *pName, const std::string &Value)
	{
		Writer.WriteAttribute(pName);
		Writer.WriteStrValue(Value.c_str());
	}
	void Bool(CJsonWriter &Writer, const char *pName, bool Value)
	{
		Writer.WriteAttribute(pName);
		Writer.WriteBoolValue(Value);
	}
	void Number(CJsonWriter &Writer, const char *pName, double Value) { String(Writer, pName, std::to_string(Value)); }
	void Emit(CJsonStringWriter &Writer)
	{
		std::printf("QM_UPDATE_PROBE_JSON %s\n", Writer.GetOutputString().c_str());
		std::fflush(stdout);
	}
	bool Approved(const std::string &Url)
	{
		for(const auto Resource : {qm_update::RELEASE, qm_update::API, qm_update::RAW})
		{
			if(qm_update::IsOfficialUrl(Url, Resource))
				return true;
			for(const auto &Source : qm_update::BuiltinSources())
				if(Source.m_Enabled && (Source.m_Resources & Resource) && Url.starts_with(Source.m_Prefix) && qm_update::IsOfficialUrl(Url.substr(Source.m_Prefix.size()), Resource))
					return true;
		}
		// 本地响应测试只允许数字回环地址，不接受任意外部 HTTP。
		const std::string Prefix = "http://127.0.0.1:";
		if(!Url.starts_with(Prefix))
			return false;
		const auto End = Url.find('/', Prefix.size());
		return End != std::string::npos && End > Prefix.size() && Url.substr(Prefix.size(), End - Prefix.size()).find_first_not_of("0123456789") == std::string::npos;
	}
	std::vector<uint8_t> Read(const char *pPath, size_t Limit)
	{
		std::ifstream File(pPath, std::ios::binary | std::ios::ate);
		if(!File || File.tellg() < 0 || static_cast<uint64_t>(File.tellg()) > Limit)
			throw std::runtime_error("input file missing or too large");
		std::vector<uint8_t> Bytes(static_cast<size_t>(File.tellg()));
		File.seekg(0);
		if(!File.read(reinterpret_cast<char *>(Bytes.data()), Bytes.size()))
			throw std::runtime_error("input file read failed");
		return Bytes;
	}
	int Sources()
	{
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Writer.WriteAttribute("sources");
		Writer.BeginArray();
		for(const auto &Source : qm_update::BuiltinSources())
		{
			Writer.BeginObject();
			String(Writer, "prefix", Source.m_Prefix);
			String(Writer, "group", Source.m_Group);
			Writer.WriteAttribute("resources");
			Writer.WriteIntValue(Source.m_Resources);
			Writer.WriteAttribute("priority");
			Writer.WriteIntValue(Source.m_Priority);
			Bool(Writer, "enabled", Source.m_Enabled);
			Writer.EndObject();
		}
		Writer.EndArray();
		Writer.EndObject();
		Emit(Writer);
		return 0;
	}
	int Release(const char *pPath, const char *pCurrent = "0.0", bool InfoOnly = false, bool Portable = false)
	{
		auto Bytes = Read(pPath, 4 * 1024 * 1024);
		SQmClientUpdateRelease Release;
		char aError[256] = "";
		const bool Valid = (InfoOnly ? ParseQmClientReleaseInfo : ParseQmClientUpdateRelease)(reinterpret_cast<const char *>(Bytes.data()), Bytes.size(), pCurrent, Release, aError, sizeof(aError), false, Portable);
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Bool(Writer, "valid", Valid);
		Bool(Writer, "new_version", Release.m_NewVersion);
		Bool(Writer, "package_available", Release.m_PackageAvailable);
		Writer.WriteAttribute("notes_bytes");
		Writer.WriteIntValue(Release.m_Notes.size());
		String(Writer, "error", aError);
		if(Valid)
		{
			String(Writer, "version", Release.m_aVersion);
			String(Writer, "package_url", Release.m_aPackageUrl);
			String(Writer, "package_signature_url", Release.m_aPackageSignatureUrl);
			String(Writer, "manifest_url", Release.m_aManifestUrl);
			String(Writer, "manifest_signature_url", Release.m_aManifestSignatureUrl);
			Bool(Writer, "sevenzip", Release.m_SevenZip);
		}
		Writer.EndObject();
		Emit(Writer);
		return Valid ? 0 : 1;
	}
	int Verify(const char *pPackage, const char *pPackageSignature, const char *pManifest, const char *pManifestSignature)
	{
		auto Manifest = Read(pManifest, 32 * 1024 * 1024);
		auto ManifestSignature = Read(pManifestSignature, 64);
		auto PackageSignature = Read(pPackageSignature, 64);
		auto Storage = CreateLocalStorage();
		char aError[256] = "";
		uint64_t Size = 0;
		std::array<uint8_t, 32> SignedDigest{};
		SHA256_DIGEST Digest{};
		bool Valid = qm_update_verify_manifest_package(Manifest.data(), Manifest.size(), ManifestSignature.data(), ManifestSignature.size(), &Size, SignedDigest.data(), SignedDigest.size(), aError, sizeof(aError));
		if(Valid)
		{
			Valid = Storage && Storage->CalculateHashes(pPackage, IStorage::TYPE_ABSOLUTE, &Digest) && mem_comp(Digest.data, SignedDigest.data(), SignedDigest.size()) == 0 && std::filesystem::file_size(pPackage) == Size;
			if(!Valid)
				str_copy(aError, "downloaded file size/SHA-256 mismatch");
		}
		if(Valid)
			Valid = qm_update_verify_package_digest(Digest.data, 32, PackageSignature.data(), PackageSignature.size(), aError, sizeof(aError));
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Bool(Writer, "valid", Valid);
		String(Writer, "error", aError);
		Number(Writer, "signed_size", Size);
		Writer.EndObject();
		Emit(Writer);
		return Valid ? 0 : 1;
	}
	// 复用同一真实 HTTP 引擎，取消排队任务后仍能请求，且取消任务不能触达服务器。
	// 对真实官方 API 验收生产元数据状态机的系统代理失败/直连恢复，不下载大包。
	int CheckOfficial(const std::string &Url)
	{
		if(!qm_update::IsOfficialUrl(Url, qm_update::API))
			throw std::runtime_error("metadata check requires approved official API");
		CJobPool Jobs;
		Jobs.Init(1);
		auto Job = std::make_shared<qm_update::CSystemProxyJob>(Url);
		auto Begin = TClock::now();
		Jobs.Add(Job);
		while(Job->State() != IJob::STATE_DONE && Seconds(Begin) < 8)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		const bool Proxied = Job->State() == IJob::STATE_DONE && Job->HasDecision() && !Job->Proxy().empty();
		const std::string Proxy = Proxied ? Job->Proxy() : "";
		Jobs.Shutdown();
		std::unique_ptr<IEngineHttp> Http(CreateEngineHttp());
		if(!Http->Init(std::chrono::milliseconds(1000)))
			throw std::runtime_error("HTTP initialization failed");
		qm_update::CSourceRegistry Sources({});
		qm_update::CUpdateRequest Check;
		int Attempts = 0;
		const auto Factory = [&](const std::string &RequestUrl, bool Direct) -> std::shared_ptr<IHttpRequest> {
			++Attempts;
			std::shared_ptr<IHttpRequest> Request = HttpGet(RequestUrl.c_str());
			Request->Proxy(Direct ? "" : Proxy.c_str());
			Request->Timeout(CTimeout{5000, 30000, 1, 10});
			Request->MaxResponseSize(4 * 1024 * 1024);
			Request->LogProgress(HTTPLOG::FAILURE);
			Http->Run(Request);
			return Request;
		};
		Check.Begin(Sources, Url, qm_update::API, 0, [&](const std::string &RequestUrl) { return Factory(RequestUrl, false); }, [](const IHttpRequest &Request) {
                unsigned char *Data = nullptr; size_t Size = 0; Request.Result(&Data,&Size);
                SQmClientUpdateRelease Release; char Error[256];
                return ParseQmClientUpdateRelease(reinterpret_cast<const char *>(Data),Size,"0.0",Release,Error,sizeof(Error)); }, [&](const std::string &RequestUrl) { return Factory(RequestUrl, true); });
		auto Start = TClock::now();
		auto State = qm_update::CUpdateRequest::EState::RUNNING;
		while(State == qm_update::CUpdateRequest::EState::RUNNING && Seconds(Start) < 60)
		{
			State = Check.Poll(Seconds(Start));
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		const bool Valid = State == qm_update::CUpdateRequest::EState::SUCCEEDED;
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Bool(Writer, "valid", Valid);
		Bool(Writer, "system_proxy_selected", Proxied);
		Writer.WriteAttribute("attempts");
		Writer.WriteIntValue(Attempts);
		Number(Writer, "seconds", Seconds(Start));
		Writer.EndObject();
		Emit(Writer);
		Check.Cancel();
		Http->Shutdown();
		return Valid ? 0 : 1;
	}

	int CancelQueued(const std::string &Url)
	{
		if(!Url.starts_with("http://127.0.0.1:") || !Approved(Url))
			throw std::runtime_error("queued cancellation fixture must be loopback");
		std::unique_ptr<IEngineHttp> Http(CreateEngineHttp());
		if(!Http->Init(std::chrono::milliseconds(1000)))
			throw std::runtime_error("HTTP initialization failed");
		std::vector<std::shared_ptr<IHttpRequest>> Requests;
		for(int Index = 0; Index < 6; ++Index)
		{
			std::shared_ptr<IHttpRequest> Request = HttpGet(Url.c_str());
			Request->AllowInsecureProtocol();
			Request->Proxy("");
			Request->Abort();
			Requests.push_back(Request);
			Http->Run(Request);
		}
		std::shared_ptr<IHttpRequest> Recovery = HttpGet(Url.c_str());
		Recovery->AllowInsecureProtocol();
		Recovery->Proxy("");
		Recovery->Timeout(CTimeout{1000, 3000, 1, 1});
		Http->Run(Recovery);
		auto Start = TClock::now();
		bool Completed = false;
		while(Seconds(Start) < 5)
		{
			Completed = Recovery->Done();
			for(const auto &Request : Requests)
				Completed &= Request->Done();
			if(Completed)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		bool Valid = Completed && Recovery->State() == EHttpState::DONE;
		for(const auto &Request : Requests)
			Valid &= Request->State() == EHttpState::ABORTED;
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Bool(Writer, "success", Valid);
		Writer.EndObject();
		Emit(Writer);
		for(const auto &Request : Requests)
			Request->Abort();
		Recovery->Abort();
		Http->Shutdown();
		return Valid ? 0 : 1;
	}

	int Fetch(const std::string &Url, const std::string &Destination, const std::string &Mode, double Budget, int64_t MaxBytes)
	{
		if(!Approved(Url) || (Mode != "direct" && Mode != "system" && Mode != "environment") || !std::isfinite(Budget) || Budget < 1 || Budget > 3600 || MaxBytes < 1 || MaxBytes > 5LL * 1024 * 1024 * 1024)
			throw std::runtime_error("invalid probe arguments");
		const auto Output = std::filesystem::path(Destination);
		if(Output.is_absolute() || !Destination.starts_with("tmp/") || Destination.find("..") != std::string::npos)
			throw std::runtime_error("output must stay in workspace tmp/");
		std::filesystem::create_directories(Output.parent_path());
		bool ProxyDecision = false;
		std::string Proxy;
		double ProxySeconds = 0;
		if(Mode == "system")
		{
			if(!qm_update::IsOfficialUrl(Url, qm_update::API) && !qm_update::IsOfficialUrl(Url, qm_update::RELEASE))
				throw std::runtime_error("system proxy mode only applies to official requests");
			CJobPool Jobs;
			Jobs.Init(1);
			auto Job = std::make_shared<qm_update::CSystemProxyJob>(Url);
			auto Start = TClock::now();
			Jobs.Add(Job);
			while(Job->State() != IJob::STATE_DONE && Seconds(Start) < 8)
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			if(Job->State() == IJob::STATE_DONE)
			{
				ProxyDecision = Job->HasDecision();
				if(ProxyDecision)
					Proxy = Job->Proxy();
			}
			ProxySeconds = Seconds(Start);
			Jobs.Shutdown();
		}
		std::unique_ptr<IEngineHttp> Http(CreateEngineHttp());
		auto Storage = CreateLocalStorage();
		if(!Storage || !Http->Init(std::chrono::milliseconds(1000)))
			throw std::runtime_error("HTTP initialization failed");
		std::shared_ptr<IHttpRequest> Request = HttpGet(Url.c_str());
		if(Url.starts_with("http://127.0.0.1:"))
			Request->AllowInsecureProtocol();
		Request->Timeout(CTimeout{5000, 0, 1, 20});
		Request->MaxResponseSize(MaxBytes);
		Request->SkipByFileTime(false);
		Request->LogProgress(HTTPLOG::FAILURE);
		Request->WriteToFile(Storage.get(), Destination.c_str(), IStorage::TYPE_SAVE);
		if(Mode == "direct" || Mode == "system")
			Request->Proxy(Proxy.c_str());
		qm_update::CProgressDeadline Deadline;
		Deadline.Begin(0);
		auto Start = TClock::now();
		double FirstByte = -1, LastPrint = -5;
		Http->Run(Request);
		while(!Request->Done())
		{
			const double Now = Seconds(Start);
			if(FirstByte < 0 && Request->Current() > 0)
				FirstByte = Now;
			if(Now >= Budget || Deadline.Expired(Now, Request->Current()))
				Request->Abort();
			if(Now - LastPrint >= 5)
			{
				std::fprintf(stderr, "probe %s: %.1f MiB, %.1fs\n", Mode.c_str(), Request->Current() / 1048576.0, Now);
				LastPrint = Now;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		const double Elapsed = Seconds(Start);
		const bool Success = Request->State() == EHttpState::DONE && Request->StatusCode() == 200;
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Bool(Writer, "success", Success);
		String(Writer, "url", Url);
		String(Writer, "proxy_mode", Mode);
		Bool(Writer, "system_proxy_decision", ProxyDecision);
		Bool(Writer, "system_proxy_selected", !Proxy.empty());
		Number(Writer, "proxy_seconds", ProxySeconds);
		Number(Writer, "seconds", Elapsed);
		Number(Writer, "first_byte_seconds", FirstByte);
		Number(Writer, "bytes", Request->Current());
		Number(Writer, "mib_per_second", Elapsed > 0 ? Request->Current() / 1048576.0 / Elapsed : 0);
		Writer.WriteAttribute("http_status");
		Writer.WriteIntValue(Request->CompletedStatusCode());
		Writer.WriteAttribute("state");
		Writer.WriteIntValue(static_cast<int>(Request->State()));
		if(Success)
		{
			char aDigest[65];
			sha256_str(Request->ResultSha256(), aDigest, sizeof(aDigest));
			String(Writer, "sha256", aDigest);
		}
		Writer.EndObject();
		Emit(Writer);
		Http->Shutdown();
		return Success ? 0 : 1;
	}
}
int main(int argc, const char **argv)
{
	log_set_global_logger(log_logger_stdout().release());
	try
	{
		if(argc == 2 && std::string(argv[1]) == "sources")
			return Sources();
		if(argc == 3 && std::string(argv[1]) == "check-official")
			return CheckOfficial(argv[2]);
		if(argc == 3 && std::string(argv[1]) == "cancel-queued")
			return CancelQueued(argv[2]);
		if(argc == 5 && std::string(argv[1]) == "info" && (std::string(argv[4]) == "portable" || std::string(argv[4]) == "normal"))
			return Release(argv[2], argv[3], true, std::string(argv[4]) == "portable");
		if(argc == 3 && std::string(argv[1]) == "release")
			return Release(argv[2]);
		if(argc == 6 && std::string(argv[1]) == "verify")
			return Verify(argv[2], argv[3], argv[4], argv[5]);
		if(argc == 7 && std::string(argv[1]) == "fetch")
			return Fetch(argv[2], argv[3], argv[4], std::stod(argv[5]), std::stoll(argv[6]));
		throw std::runtime_error("usage: sources | release FILE | info FILE VERSION portable|normal | verify PACKAGE SIG MANIFEST SIG | fetch URL tmp/FILE direct|system|environment BUDGET MAX_BYTES");
	}
	catch(const std::exception &Error)
	{
		CJsonStringWriter Writer;
		Writer.BeginObject();
		Bool(Writer, "success", false);
		String(Writer, "error", Error.what());
		Writer.EndObject();
		Emit(Writer);
		return 2;
	}
}
