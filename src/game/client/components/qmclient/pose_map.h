#ifndef GAME_CLIENT_COMPONENTS_QMCLIENT_POSE_MAP_H
#define GAME_CLIENT_COMPONENTS_QMCLIENT_POSE_MAP_H

#include "pose_capture.h"

#include <base/system.h>
#include <base/types.h>

#include <string>
#include <vector>

// 合影（.poses）本身不记录地图。打开、渲染和导出都必须先有一张地图，
// 这份头文件只负责「用哪张地图」的判断和命令拼接，不碰界面与引擎：
// - 已加载地图时直接沿用，不打断玩家；
// - 没有地图时进入选择态，等玩家挑一张再执行，绝不猜测 dm1。
namespace qm_pose_map
{
	enum class EAction
	{
		OPEN,
		RENDER,
	};

	enum class EStatus
	{
		OK,
		// 输入为空：还没有选择地图。
		EMPTY,
		// 输入有值，但 maps/ 与 downloadedmaps/ 里没有对应的 .map。
		NOT_FOUND,
		// 输入本身不能当作地图路径使用（例如包含 .. 段）。
		INVALID,
	};

	// 点开一份合影后的处理结果：要么立刻可以执行的命令，要么先让玩家选择地图。
	struct SPlan
	{
		bool m_NeedsSelection = false;
		std::string m_Command;
	};

	inline const char *Basename(const char *pPath)
	{
		const char *pSlash = str_rchr(pPath, '/');
		return pSlash == nullptr ? pPath : pSlash + 1;
	}

	// 逐段检查 ..，避免把「名字里恰好带两个点」的合法地图误判成越级路径。
	inline bool HasParentSegment(const char *pPath)
	{
		const std::string Path(pPath);
		size_t Start = 0;
		while(Start <= Path.size())
		{
			const size_t Slash = Path.find('/', Start);
			const std::string Segment = Path.substr(Start, Slash == std::string::npos ? std::string::npos : Slash - Start);
			if(Segment == "..")
				return true;
			if(Slash == std::string::npos)
				break;
			Start = Slash + 1;
		}
		return false;
	}

	inline bool EqualsNoCase(const char *pLeft, const char *pRight)
	{
		return str_utf8_comp_nocase(pLeft, pRight) == 0;
	}

	inline bool EndsWithMap(const std::string &Name)
	{
		const int Length = (int)Name.size();
		return Length > 4 && str_endswith_nocase(Name.c_str(), ".map") != nullptr;
	}

	// 目录浏览：根列表是 maps/ 与 downloadedmaps/ 两个入口，进入后可以继续下钻。
	inline bool IsMapRootFolder(const char *pFolder)
	{
		return pFolder != nullptr && (EqualsNoCase(pFolder, "maps") || EqualsNoCase(pFolder, "downloadedmaps"));
	}

	inline std::string BrowseChild(const char *pFolder, const char *pEntry)
	{
		if(pFolder == nullptr || pFolder[0] == '\0')
			return pEntry == nullptr ? std::string() : std::string(pEntry);
		std::string Path(pFolder);
		Path += '/';
		Path += pEntry == nullptr ? "" : pEntry;
		return Path;
	}

	// ".." 的语义：maps/sub → maps；maps / downloadedmaps → 根列表（空）；根列表再往上仍是根列表。
	inline std::string BrowseParent(const char *pFolder)
	{
		if(pFolder == nullptr || pFolder[0] == '\0' || IsMapRootFolder(pFolder))
			return std::string();
		std::string Path(pFolder);
		const size_t Slash = Path.rfind('/');
		if(Slash == std::string::npos)
			return std::string();
		return Path.substr(0, Slash);
	}

	// 把界面输入解析成可以直接交给 poses_* 的地图路径。
	// downloadedmaps 里的文件名是「地图名_sha1.map」。列表里只显示人能认出来的部分：
	// 去掉 .map 后缀和结尾的十六进制指纹，其余原样保留（含空格与中文）。
	inline std::string DisplayName(const char *pFilename)
	{
		std::string Name = pFilename == nullptr ? "" : pFilename;
		if(EndsWithMap(Name))
			Name.resize(Name.size() - 4);
		const size_t Underscore = Name.rfind('_');
		if(Underscore != std::string::npos && Name.size() - Underscore - 1 >= 8)
		{
			bool AllHex = true;
			for(size_t Index = Underscore + 1; Index < Name.size(); ++Index)
			{
				const char Character = Name[Index];
				const bool Hex = (Character >= '0' && Character <= '9') || (Character >= 'a' && Character <= 'f') || (Character >= 'A' && Character <= 'F');
				if(!Hex)
				{
					AllHex = false;
					break;
				}
			}
			if(AllHex)
				Name.resize(Underscore);
		}
		return Name;
	}

	// 命中时返回已知地图的存储相对路径（例如 "maps/Tropical Etorm.map"），
	// 这样引擎的 LoadMap 能像以前一样从 maps/ 或 downloadedmaps/ 里取到文件。
	inline EStatus Resolve(const char *pInput, const std::vector<std::string> &vKnownPaths, std::string &MapPath)
	{
		MapPath.clear();
		if(pInput == nullptr)
			return EStatus::EMPTY;

		std::string Input(pInput);
		size_t Begin = 0;
		while(Begin < Input.size() && (Input[Begin] == ' ' || Input[Begin] == '\t'))
			Begin++;
		size_t End = Input.size();
		while(End > Begin && (Input[End - 1] == ' ' || Input[End - 1] == '\t'))
			End--;
		Input = Input.substr(Begin, End - Begin);
		if(Input.empty())
			return EStatus::EMPTY;
		if(HasParentSegment(Input.c_str()))
			return EStatus::INVALID;

		// 绝对路径原样交给引擎：是否存在、能否加载由 LoadMap 报错，
		// 这里不做重复的文件系统判断。
		if(!fs_is_relative_path(Input.c_str()))
		{
			MapPath = Input;
			return EStatus::OK;
		}

		const char *pInputName = Basename(Input.c_str());
		std::string InputWithSuffix = Input;
		if(!EndsWithMap(Input))
			InputWithSuffix += ".map";
		const char *pInputWithSuffixName = Basename(InputWithSuffix.c_str());

		// 先按完整路径匹配，再退到文件名匹配；两次都忽略大小写。
		for(int Pass = 0; Pass < 2; ++Pass)
		{
			const std::string &Probe = Pass == 0 ? InputWithSuffix : Input;
			for(const std::string &Known : vKnownPaths)
			{
				if(EqualsNoCase(Known.c_str(), Probe.c_str()))
				{
					MapPath = Known;
					return EStatus::OK;
				}
			}
		}
		for(int Pass = 0; Pass < 2; ++Pass)
		{
			const char *pProbeName = Pass == 0 ? pInputWithSuffixName : pInputName;
			for(const std::string &Known : vKnownPaths)
			{
				if(EqualsNoCase(Basename(Known.c_str()), pProbeName))
				{
					MapPath = Known;
					return EStatus::OK;
				}
			}
		}
		return EStatus::NOT_FOUND;
	}

	// 与手写命令保持完全一致的参数顺序，界面只是把地图拼进去，
	// 不再维护第二套命令格式。
	// 合影就是一张照片：渲染输出只做静帧，可选 PNG / WebP。
	enum class EOutput
	{
		PNG,
		WEBP,
	};

	inline const char *OutputExtension(EOutput Output)
	{
		return Output == EOutput::WEBP ? "webp" : "png";
	}

	inline std::string BuildCommand(EAction Action, const char *pPosesPath, const char *pMapPath, const char *pOutputName, EOutput Output = EOutput::PNG, int CaptureScale = 1, bool EntityLayer = false)
	{
		char aCommand[4 * IO_MAX_PATH_LENGTH + 128];
		switch(Action)
		{
		case EAction::OPEN:
			str_format(aCommand, sizeof(aCommand), "poses_open \"%s\" \"%s\"", pPosesPath, pMapPath);
			break;
		case EAction::RENDER:
		{
			// 扩展名决定编码格式：截图管线按后缀选 png / webp，也决定落盘文件名。
			// 倍数只在大于 1 时写出来：1x 命令与旧调用保持完全一致。
			const int Scale = qm_pose::NormalizeCaptureScale(CaptureScale);
			// 第 5 个参数是「实体层」：1 = 用实体铺满背景（盖掉地图方块）。倍数 > 1 时才有第 4 个参数，
			// 所以这里显式写出倍数，保持参数位置固定。
			str_format(aCommand, sizeof(aCommand), "poses_render \"%s\" \"%s.%s\" \"%s\" %d %d",
				pPosesPath, pOutputName == nullptr ? "" : pOutputName, OutputExtension(Output), pMapPath, Scale, EntityLayer ? 1 : 0);
			break;
		}
		}
		return aCommand;
	}

	// 点击「打开 / 渲染」时的决定：当前地图可用就立刻执行，
	// 否则把请求交给界面去选地图（不能在没有地图的情况下硬开）。
	inline SPlan PlanStart(EAction Action, const char *pPosesPath, const char *pOutputName, const char *pCurrentMapPath)
	{
		SPlan Plan;
		if(pCurrentMapPath != nullptr && pCurrentMapPath[0] != '\0')
			Plan.m_Command = BuildCommand(Action, pPosesPath, pCurrentMapPath, pOutputName);
		else
			Plan.m_NeedsSelection = true;
		return Plan;
	}

	// 等待玩家确认地图时的请求状态。取消或确认后必须回到初始态，
	// 否则下一次打开会沿用上一次的选择。
	class CSelection
	{
	public:
		void Begin(EAction Action, const char *pPosesPath, const char *pOutputName)
		{
			m_Active = true;
			m_Action = Action;
			m_PosesPath = pPosesPath == nullptr ? "" : pPosesPath;
			m_OutputName = pOutputName == nullptr ? "" : pOutputName;
			m_Status = EStatus::EMPTY;
			m_Output = EOutput::PNG;
			m_CaptureScale = 1;
			m_EntityLayer = false;
		}

		void Cancel()
		{
			m_Active = false;
			m_Action = EAction::OPEN;
			m_PosesPath.clear();
			m_OutputName.clear();
			m_Status = EStatus::EMPTY;
			m_Output = EOutput::PNG;
			m_CaptureScale = 1;
			m_EntityLayer = false;
		}

		// 弹窗里「预览」和「渲染」是两个按钮：动作在点按钮时决定，不靠打开时的那一个。
		void SetAction(EAction Action) { m_Action = Action; }

		// 输出格式来自弹窗：png 或 webp。
		void SetOutput(EOutput Output) { m_Output = Output; }
		EOutput Output() const { return m_Output; }

		// 实体层：是否用实体铺满背景（盖掉难看的地图方块）。
		void SetEntityLayer(bool EntityLayer) { m_EntityLayer = EntityLayer; }
		bool EntityLayer() const { return m_EntityLayer; }

		// 超采样倍数来自弹窗：1 / 2 / 4（命令还接受 3）。越界值归一成 1。
		void SetCaptureScale(int Scale) { m_CaptureScale = qm_pose::NormalizeCaptureScale(Scale); }
		int CaptureScale() const { return m_CaptureScale; }

		// 确认一次选择：成功时给出要执行的命令并结束选择态；
		// 失败时保持选择态，让界面把错误显示在原地。
		bool Confirm(const char *pInput, const std::vector<std::string> &vKnownPaths, std::string &Command)
		{
			Command.clear();
			if(!m_Active)
			{
				m_Status = EStatus::EMPTY;
				return false;
			}
			std::string MapPath;
			m_Status = Resolve(pInput, vKnownPaths, MapPath);
			if(m_Status != EStatus::OK)
				return false;
			Command = BuildCommand(m_Action, m_PosesPath.c_str(), MapPath.c_str(), m_OutputName.c_str(), m_Output, m_CaptureScale, m_EntityLayer);
			Cancel();
			return true;
		}

		bool Active() const { return m_Active; }
		EAction Action() const { return m_Action; }
		EStatus Status() const { return m_Status; }
		const std::string &PosesPath() const { return m_PosesPath; }
		const std::string &OutputName() const { return m_OutputName; }

	private:
		bool m_Active = false;
		EAction m_Action = EAction::OPEN;
		EStatus m_Status = EStatus::EMPTY;
		std::string m_PosesPath;
		std::string m_OutputName;
		EOutput m_Output = EOutput::PNG;
		int m_CaptureScale = 1;
		bool m_EntityLayer = false;
	};
}

#endif
