#ifndef BASE_CRASH_OUTPUT_H
#define BASE_CRASH_OUTPUT_H

#include <cstddef>
#include <cstring>
#include <string_view>

namespace crash_output
{
	// 输入与输出不得重叠；失败清空可写输出，不能继续使用截断路径。
	inline bool BuildPath(std::string_view Base, std::string_view Suffix, bool ReplaceExtension, char *pOutput, std::size_t Capacity) noexcept
	{
		if(!pOutput || Capacity == 0)
			return false;
		pOutput[0] = '\0';
		if(Base.empty() || Base.find('\0') != std::string_view::npos || Suffix.find('\0') != std::string_view::npos)
			return false;
		const auto Separator = Base.find_last_of("/\\");
		if(Separator != std::string_view::npos && Separator == Base.size() - 1)
			return false;
		auto Prefix = Base.size();
		if(ReplaceExtension)
		{
			const auto Extension = Base.find_last_of('.');
			if(Extension != std::string_view::npos && (Separator == std::string_view::npos || Extension > Separator))
				Prefix = Extension;
		}
		if(Suffix.size() >= Capacity || Prefix > Capacity - 1 - Suffix.size())
			return false;
		if(Prefix)
			std::memcpy(pOutput, Base.data(), Prefix);
		if(!Suffix.empty())
			std::memcpy(pOutput + Prefix, Suffix.data(), Suffix.size());
		pOutput[Prefix + Suffix.size()] = '\0';
		return true;
	}

	inline bool MakeSidecarPath(std::string_view Base, std::string_view Suffix, char *pOutput, std::size_t Capacity) noexcept
	{
		return BuildPath(Base, Suffix, true, pOutput, Capacity);
	}
	inline bool AppendPathSuffix(std::string_view Base, std::string_view Suffix, char *pOutput, std::size_t Capacity) noexcept
	{
		return BuildPath(Base, Suffix, false, pOutput, Capacity);
	}

	// File借用到本对象销毁；Write/Flush/Close须不抛异常。只接管一次关闭责任。
	template<typename TFile>
	class CWriter
	{
		TFile &m_File;
		bool m_Good = true;
		bool m_Finished = false;

	public:
		explicit CWriter(TFile &File) noexcept :
			m_File(File) {}
		CWriter(const CWriter &) = delete;
		CWriter &operator=(const CWriter &) = delete;
		~CWriter()
		{
			if(!m_Finished)
				m_File.Close();
		}
		bool Write(const void *pData, std::size_t Size) noexcept
		{
			if(m_Finished || !m_Good)
				return false;
			if(Size && !pData)
			{
				m_Good = false;
				return false;
			}
			const auto *pBytes = static_cast<const unsigned char *>(pData);
			while(Size)
			{
				std::size_t Written = 0;
				if(!m_File.Write(pBytes, Size, Written) || Written == 0 || Written > Size)
				{
					m_Good = false;
					return false;
				}
				pBytes += Written;
				Size -= Written;
			}
			return true;
		}
		bool WriteText(const char *pText) noexcept
		{
			return Write(pText, pText ? std::strlen(pText) : 0);
		}
		bool Finish(bool Complete = true) noexcept
		{
			if(!m_Finished)
			{
				m_Finished = true;
				const bool Flushed = m_File.Flush();
				const bool Closed = m_File.Close();
				m_Good = m_Good && Complete && Flushed && Closed;
			}
			return m_Good;
		}
	};
}

#endif
