#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_CAMERA_FILE_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_CAMERA_FILE_H

#include "demo_camera_path.h"

#include <base/system.h>

#include <engine/shared/json.h>
#include <engine/storage.h>

#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <string>

namespace qm_demo_camera
{

	struct SProject
	{
		CTrack m_Track;
		bool m_Enabled = false;
	};

	enum class ELoadResult
	{
		MISSING,
		LOADED,
		FAILED,
		UNAVAILABLE,
	};

	// 外部文本I/O与堆分配边界；关闭和C分配回调不得抛异常。
	class CFileOperations
	{
	public:
		virtual ~CFileOperations() = default;
		virtual bool ReadText(IOHANDLE File, size_t Length, std::string &Json)
		{
			Json.assign(Length, '\0');
			return io_read(File, Json.data(), static_cast<unsigned>(Length)) == Length && io_error(File) == 0;
		}
		// JSON解析器及引擎校验器以free释放，覆盖实现须返回malloc兼容内存，并遵循Zero清零请求。
		virtual void *AllocateJson(size_t Size, int Zero) noexcept { return Zero ? std::calloc(1, Size) : std::malloc(Size); }
		virtual int Close(IOHANDLE File) noexcept { return io_close(File); }
	};
	inline std::string Encode(const SProject &Project)
	{
		std::ostringstream Stream;
		Stream.exceptions(std::ios::badbit | std::ios::failbit);
		Stream.imbue(std::locale::classic());
		Stream << std::setprecision(std::numeric_limits<double>::max_digits10);
		Stream << "{\"version\":1,\"enabled\":" << (Project.m_Enabled ? "true" : "false")
		       << ",\"aspect\":" << Project.m_Track.Aspect() << ",\"keyframes\":[";
		bool First = true;
		for(const auto &Frame : Project.m_Track.Frames())
		{
			if(!First)
				Stream << ',';
			First = false;
			Stream << "{\"tick\":" << Frame.m_Tick << ",\"x\":" << Frame.m_View.m_Center.x
			       << ",\"y\":" << Frame.m_View.m_Center.y << ",\"zoom\":" << Frame.m_View.m_Zoom
			       << ",\"transition\":" << static_cast<int>(Frame.m_Transition)
			       << ",\"ease_from\":" << Frame.m_EaseFrom << ",\"ease_to\":" << Frame.m_EaseTo << '}';
		}
		Stream << "]}\n";
		return Stream.str();
	}

	inline bool ReadNumber(const json_value *pObject, const char *pName, double &Value)
	{
		const json_value *pField = json_object_get(pObject, pName);
		if(pField == nullptr || (pField->type != json_double && pField->type != json_integer))
			return false;
		Value = pField->type == json_double ? pField->u.dbl : static_cast<double>(pField->u.integer);
		return std::isfinite(Value);
	}

	inline bool ReadFloat(const json_value *pObject, const char *pName, float &Value)
	{
		double Parsed;
		if(!ReadNumber(pObject, pName, Parsed) || Parsed < -std::numeric_limits<float>::max() || Parsed > std::numeric_limits<float>::max())
			return false;
		Value = static_cast<float>(Parsed);
		return true;
	}

	inline ELoadResult DecodeWithResult(const std::string &Json, SProject &Project, CFileOperations *pOperations = nullptr) noexcept
	{
		try
		{
			if(Json.size() > 512 * 1024)
				return ELoadResult::FAILED;
			CFileOperations Default;
			auto &Operations = pOperations ? *pOperations : Default;
			struct SAllocation
			{
				CFileOperations *m_pOperations;
				bool m_Failed = false;
			} Allocation{&Operations};
			json_settings Settings{};
			Settings.max_memory = 4 * 1024 * 1024;
			Settings.user_data = &Allocation;
			Settings.mem_alloc = [](size_t Size, int Zero, void *pUser) noexcept -> void * {
				auto &State = *static_cast<SAllocation *>(pUser);
				void *pMemory = State.m_pOperations->AllocateJson(Size, Zero);
				State.m_Failed |= pMemory == nullptr;
				return pMemory;
			};
			Settings.mem_free = [](void *pMemory, void *) { std::free(pMemory); };
			std::unique_ptr<json_value, decltype(&json_value_free)> pRoot(JsonParseEx(&Settings, Json.data(), Json.size(), nullptr), json_value_free);
			if(!pRoot)
				return Allocation.m_Failed ? ELoadResult::UNAVAILABLE : ELoadResult::FAILED;
			if(pRoot->type != json_object)
				return ELoadResult::FAILED;
			const auto *pVersion = json_object_get(pRoot.get(), "version");
			const auto *pEnabled = json_object_get(pRoot.get(), "enabled");
			const auto *pFrames = json_object_get(pRoot.get(), "keyframes");
			float Aspect;
			if(pVersion == nullptr || pVersion->type != json_integer || pVersion->u.integer != 1 ||
				pEnabled == nullptr || pEnabled->type != json_boolean ||
				pFrames == nullptr || pFrames->type != json_array || pFrames->u.array.length > CTrack::MAX_KEYFRAMES ||
				!ReadFloat(pRoot.get(), "aspect", Aspect))
				return ELoadResult::FAILED;
			SProject Parsed;
			if(!Parsed.m_Track.SetAspect(Aspect))
				return ELoadResult::FAILED;
			Parsed.m_Enabled = pEnabled->u.boolean != 0;
			double PreviousTick = -1.0;
			for(unsigned i = 0; i < pFrames->u.array.length; ++i)
			{
				const auto *pFrame = pFrames->u.array.values[i];
				double Tick, EaseFrom, EaseTo;
				float X, Y, Zoom;
				if(pFrame == nullptr || pFrame->type != json_object || !ReadNumber(pFrame, "tick", Tick) ||
					!ReadFloat(pFrame, "x", X) || !ReadFloat(pFrame, "y", Y) || !ReadFloat(pFrame, "zoom", Zoom) ||
					!ReadNumber(pFrame, "ease_from", EaseFrom) || !ReadNumber(pFrame, "ease_to", EaseTo))
					return ELoadResult::FAILED;
				const auto *pTransition = json_object_get(pFrame, "transition");
				if(pTransition == nullptr || pTransition->type != json_integer || pTransition->u.integer < 0 || pTransition->u.integer > 2 || Tick <= PreviousTick)
					return ELoadResult::FAILED;
				if(Parsed.m_Track.Insert({Tick, {{X, Y}, Zoom},
					   static_cast<ETransition>(pTransition->u.integer), EaseFrom, EaseTo}) < 0)
					return ELoadResult::FAILED;
				PreviousTick = Tick;
			}
			Project = std::move(Parsed);
			return ELoadResult::LOADED;
		}
		catch(...)
		{
			return ELoadResult::UNAVAILABLE;
		}
	}

	inline bool Decode(const std::string &Json, SProject &Project)
	{
		return DecodeWithResult(Json, Project) == ELoadResult::LOADED;
	}

	inline std::string SidecarPath(const std::string &DemoPath)
	{
		const std::string Path = DemoPath + ".qm-camera.json";
		return DemoPath.empty() || Path.size() + 4 >= IO_MAX_PATH_LENGTH ? std::string() : Path;
	}

	inline std::string ResolveSidecar(IStorage *pStorage, const char *pFilename, int StorageType)
	{
		char aCompletePath[IO_MAX_PATH_LENGTH];
		IOHANDLE Demo = pStorage->OpenFile(pFilename, IOFLAG_READ, StorageType, aCompletePath, sizeof(aCompletePath));
		if(Demo == nullptr)
			return {};
		io_close(Demo);
		std::string Path = aCompletePath;
		if(fs_is_relative_path(Path.c_str()))
		{
			char aDirectory[IO_MAX_PATH_LENGTH];
			if(!fs_getcwd(aDirectory, sizeof(aDirectory)))
				return {};
			Path = std::string(aDirectory) + "/" + Path;
		}
		return SidecarPath(Path);
	}

	struct CFileCloser
	{
		CFileOperations *m_pOperations;
		void operator()(void *pFile) const noexcept { m_pOperations->Close(pFile); }
	};

	inline ELoadResult Load(IStorage *pStorage, const std::string &Path, SProject &Project, CFileOperations *pOperations = nullptr) noexcept
	{
		try
		{
			if(!pStorage || Path.empty())
				return ELoadResult::FAILED;
			CFileOperations Default;
			auto &Operations = pOperations ? *pOperations : Default;
			std::unique_ptr<void, CFileCloser> File(pStorage->OpenFile(Path.c_str(), IOFLAG_READ, IStorage::TYPE_ALL_OR_ABSOLUTE), {&Operations});
			if(!File)
				return pStorage->FileExists(Path.c_str(), IStorage::TYPE_ALL_OR_ABSOLUTE) ? ELoadResult::FAILED : ELoadResult::MISSING;
			const int64_t Length = io_length(File.get());
			if(Length < 0 || Length > 512 * 1024)
				return ELoadResult::FAILED;
			std::string Json;
			const bool Read = Operations.ReadText(File.get(), static_cast<size_t>(Length), Json);
			const bool Closed = Operations.Close(File.release()) == 0;
			if(!Read || !Closed)
				return ELoadResult::FAILED;
			return DecodeWithResult(Json, Project, &Operations);
		}
		catch(...)
		{
			return ELoadResult::UNAVAILABLE;
		}
	}

	inline bool Save(IStorage *pStorage, const std::string &Path, const SProject &Project, CFileOperations *pOperations = nullptr) noexcept
	{
		try
		{
			if(!pStorage || Path.empty() || Path.size() + 4 >= IO_MAX_PATH_LENGTH)
				return false;
			const std::string Temporary = Path + ".tmp";
			const std::string Json = Encode(Project);
			CFileOperations Default;
			auto &Operations = pOperations ? *pOperations : Default;
			std::unique_ptr<void, CFileCloser> File(pStorage->OpenFile(Temporary.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE_OR_ABSOLUTE), {&Operations});
			if(!File)
				return false;
			const bool Written = io_write(File.get(), Json.data(), static_cast<unsigned>(Json.size())) == Json.size();
			const bool Clean = io_error(File.get()) == 0;
			const bool Closed = Operations.Close(File.release()) == 0;
			// 完整关闭后才替换，异常或关闭失败都保留已有镜头文件。
			return Written && Clean && Closed && pStorage->RenameFile(Temporary.c_str(), Path.c_str(), fs_is_relative_path(Path.c_str()) ? IStorage::TYPE_SAVE : IStorage::TYPE_ABSOLUTE, false);
		}
		catch(...)
		{
			return false;
		}
	}

} // namespace qm_demo_camera

#endif // GAME_CLIENT_COMPONENTS_QMCLIENT_DEMO_CAMERA_FILE_H
