#ifndef ENGINE_CLIENT_QM_FONT_FILE_PROBE_H
#define ENGINE_CLIENT_QM_FONT_FILE_PROBE_H

#include "qm_font_names.h"

#include <algorithm>
#include <limits>

// 只读字体名称，不把无关字体整份装入内存；独立 FreeType 库只在 worker 内使用。
class CQmFontFileProbe
{
	FT_Library m_Library = nullptr;

	class CFace
	{
		IOHANDLE m_File = nullptr;
		FT_StreamRec m_Stream{};
		FT_Face m_Face = nullptr;

		static unsigned long Read(FT_Stream pStream, unsigned long Offset, unsigned char *pBuffer, unsigned long Count)
		{
			const auto File = static_cast<IOHANDLE>(pStream->descriptor.pointer);
			if(Offset > pStream->size || io_seek(File, Offset, IOSEEK_START) != 0)
				return Count == 0 ? 1 : 0;
			if(Count == 0)
				return 0;
			return io_read(File, pBuffer, static_cast<unsigned>(std::min(Count, pStream->size - Offset)));
		}

	public:
		CFace(FT_Library Library, const char *pPath, FT_Long Index)
		{
			m_File = io_open(pPath, IOFLAG_READ);
			if(m_File == nullptr)
				return;
			const int64_t Size = io_length(m_File);
			if(Size <= 0 || Size > std::min<int64_t>(std::numeric_limits<unsigned>::max(), std::numeric_limits<FT_Long>::max()))
				return;
			m_Stream.size = static_cast<unsigned long>(Size);
			m_Stream.descriptor.pointer = m_File;
			m_Stream.read = Read;
			// 文件由本对象持有，FreeType 关闭流时不提前释放句柄。
			m_Stream.close = [](FT_Stream) {};
			FT_Open_Args Args{};
			Args.flags = FT_OPEN_STREAM;
			Args.stream = &m_Stream;
			if(FT_Open_Face(Library, &Args, Index, &m_Face) != 0)
				m_Face = nullptr;
		}

		~CFace()
		{
			if(m_Face != nullptr)
				FT_Done_Face(m_Face);
			if(m_File != nullptr)
				io_close(m_File);
		}

		CFace(const CFace &) = delete;
		CFace &operator=(const CFace &) = delete;
		FT_Face Get() const { return m_Face; }
	};

public:
	CQmFontFileProbe() { FT_Init_FreeType(&m_Library); }
	~CQmFontFileProbe()
	{
		if(m_Library != nullptr)
			FT_Done_FreeType(m_Library);
	}

	CQmFontFileProbe(const CQmFontFileProbe &) = delete;
	CQmFontFileProbe &operator=(const CQmFontFileProbe &) = delete;

	bool Matches(const char *pPath, const std::vector<std::string> &vNames) const
	{
		if(m_Library == nullptr || vNames.empty())
			return false;
		FT_Long NumFaces;
		{
			const CFace Collection(m_Library, pPath, -1);
			if(Collection.Get() == nullptr)
				return false;
			NumFaces = Collection.Get()->num_faces;
		}
		for(FT_Long Index = 0; Index < NumFaces; ++Index)
		{
			const CFace Face(m_Library, pPath, Index);
			if(Face.Get() == nullptr)
				continue;
			const auto Names = QmFontFaceNames(Face.Get());
			const std::string FullName = Names.m_Family + " " + Names.m_Style;
			for(const std::string &Name : vNames)
			{
				if(QmFontNamesEqual(Name.c_str(), Names.m_Family.c_str()) || QmFontNamesEqual(Name.c_str(), FullName.c_str()) ||
					QmFontFaceMatchesLegacyName(Names, Name.c_str()) || QmFontNamesEqual(Name.c_str(), FT_Get_Postscript_Name(Face.Get())))
					return true;
			}
		}
		return false;
	}
};

inline std::vector<std::string> QmFindSelectedFontFiles(const std::vector<std::string> &vPaths, const std::vector<std::string> &vNames)
{
	std::vector<std::string> vSelected;
	if(vNames.empty())
		return vSelected;
	CQmFontFileProbe Probe;
	for(const std::string &Path : vPaths)
	{
		if(Probe.Matches(Path.c_str(), vNames))
			vSelected.push_back(Path);
	}
	return vSelected;
}

#endif
