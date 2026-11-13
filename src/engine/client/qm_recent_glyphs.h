#ifndef ENGINE_CLIENT_QM_RECENT_GLYPHS_H
#define ENGINE_CLIENT_QM_RECENT_GLYPHS_H

#include <base/str.h>
#include <base/system.h>

#include <engine/storage.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace QmRecentGlyphs
{
	inline constexpr size_t MAX_ENTRIES = 4096;
	// 沿用每条48字节序列化缓冲与4096项容量，为缓存读取提供格式派生预算。
	inline constexpr size_t MAX_FILE_BYTES = MAX_ENTRIES * 48;
	class CCollection
	{
		std::vector<std::pair<int, int>> m_vItems;
		std::unordered_set<uint64_t> m_Keys;
		size_t m_Cursor = 0;
		bool m_Recording = true;

	public:
		void Record(int Chr, int Size)
		{
			if(!m_Recording || Chr <= 0 || m_vItems.size() >= MAX_ENTRIES)
				return;
			const uint64_t Key = (static_cast<uint64_t>(static_cast<uint32_t>(Chr)) << 32) | static_cast<uint32_t>(Size);
			if(m_Keys.find(Key) != m_Keys.end())
				return;
			// 索引成功后只做预留容量内的无抛整数对移动，避免幽灵去重键。
			if(m_vItems.size() == m_vItems.capacity())
				m_vItems.reserve(std::min(MAX_ENTRIES, std::max(m_vItems.size() + 1, m_vItems.capacity() * 2)));
			if(m_Keys.insert(Key).second)
				m_vItems.emplace_back(Chr, Size);
		}
		const std::vector<std::pair<int, int>> &Items() const { return m_vItems; }
		size_t Cursor() const { return m_Cursor; }
		bool Warming() const { return !m_Recording; }
		void Swap(CCollection &Other) noexcept
		{
			m_vItems.swap(Other.m_vItems);
			m_Keys.swap(Other.m_Keys);
			std::swap(m_Cursor, Other.m_Cursor);
			std::swap(m_Recording, Other.m_Recording);
		}
		template<typename FPrewarm>
		int Prewarm(int MaxCount, FPrewarm &&Warm) noexcept
		{
			if(MaxCount <= 0 || !m_Recording)
				return 0;
			m_Recording = false;
			int Done = 0;
			while(Done < MaxCount && m_Cursor < m_vItems.size())
			{
				const auto Item = m_vItems[m_Cursor];
				bool Ready;
				try
				{
					Ready = Warm(Item.first, Item.second);
				}
				catch(...)
				{
					break;
				}
				++m_Cursor;
				// 空结果沿原规则跳过，避免不受字体支持的项阻塞后续缓存。
				if(Ready)
					++Done;
			}
			m_Recording = true;
			return Done;
		}
	};
	namespace detail
	{
		template<typename TIo>
		class CCloseGuard
		{
			TIo &m_Io;
			bool m_Closed = false;

		public:
			explicit CCloseGuard(TIo &Io) :
				m_Io(Io) {}
			CCloseGuard(const CCloseGuard &) = delete;
			CCloseGuard &operator=(const CCloseGuard &) = delete;
			bool Close() noexcept
			{
				if(std::exchange(m_Closed, true))
					return true;
				try
				{
					return m_Io.Close();
				}
				catch(...)
				{
					return false;
				}
			}
			~CCloseGuard() { Close(); }
		};
		class CParser
		{
			CCollection &m_Output;
			struct SLine
			{
				int m_Chr = -1, m_Size = -1, m_Value = 0;
				bool m_Digits = false, m_Invalid = false, m_Complete = false;
			} m_Line;
			void FinishNumber()
			{
				if(!m_Line.m_Digits || m_Line.m_Invalid || m_Line.m_Complete)
					return;
				if(m_Line.m_Chr < 0)
					m_Line.m_Chr = m_Line.m_Value;
				else
				{
					m_Line.m_Size = m_Line.m_Value;
					m_Line.m_Complete = true;
				}
				m_Line.m_Value = 0;
				m_Line.m_Digits = false;
			}

		public:
			explicit CParser(CCollection &Output) :
				m_Output(Output) {}
			void FinishLine()
			{
				FinishNumber();
				if(!m_Line.m_Invalid && m_Line.m_Chr > 0 && m_Line.m_Size > 0)
					m_Output.Record(m_Line.m_Chr, m_Line.m_Size);
				m_Line = {};
			}
			void Feed(char Byte)
			{
				if(Byte == '\n' || Byte == '\r')
				{
					FinishLine();
					return;
				}
				if(m_Line.m_Invalid || m_Line.m_Complete)
					return;
				if(Byte < '0' || Byte > '9')
				{
					FinishNumber();
					return;
				}
				const int Digit = Byte - '0';
				m_Line.m_Digits = true;
				if(m_Line.m_Value > (std::numeric_limits<int>::max() - Digit) / 10)
					m_Line.m_Invalid = true;
				else
					m_Line.m_Value = m_Line.m_Value * 10 + Digit;
			}
		};
		template<typename TOutput>
		struct CCandidateGuard
		{
			TOutput &m_Output;
			bool m_Published = false;
			~CCandidateGuard()
			{
				if(!m_Published)
					try
					{
						m_Output.Discard();
					}
					catch(...)
					{
					}
			}
		};
	}
	// Input借用到返回；Close消耗句柄，即使报告失败也不能再使用它。
	template<typename TInput>
	bool Load(TInput &Input, CCollection &Collection) noexcept
	{
		detail::CCloseGuard Close(Input);
		try
		{
			if(Collection.Warming())
				return false;
			CCollection Candidate = Collection;
			detail::CParser Parser(Candidate);
			size_t Total = 0;
			char aBuffer[4096];
			for(;;)
			{
				const unsigned Requested = static_cast<unsigned>(std::min(sizeof(aBuffer), MAX_FILE_BYTES - Total + 1));
				const unsigned Read = Input.Read(aBuffer, Requested);
				if(Read > Requested || Input.Failed() || Read > MAX_FILE_BYTES - Total)
					return false;
				if(Read == 0)
					break;
				Total += Read;
				for(unsigned Index = 0; Index < Read; ++Index)
					Parser.Feed(aBuffer[Index]);
			}
			Parser.FinishLine();
			if(!Close.Close())
				return false;
			Collection.Swap(Candidate);
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
	// Output的Close须幂等并消耗句柄；Discard只清理本次创建且尚未发布的候选。
	template<typename TOutput>
	bool Save(TOutput &Output, const CCollection &Collection) noexcept
	{
		if(Collection.Items().empty())
			return true;
		detail::CCandidateGuard Candidate{Output};
		detail::CCloseGuard Close(Output);
		try
		{
			if(!Output.Open())
				return false;
			for(const auto &Item : Collection.Items())
			{
				char aLine[48];
				str_format(aLine, sizeof(aLine), "%d %d\n", Item.first, Item.second);
				const unsigned Length = str_length(aLine);
				if(Output.Write(aLine, Length) != Length)
					return false;
			}
			if(!Output.Sync() || !Close.Close() || !Output.Commit())
				return false;
			Candidate.m_Published = true;
			return true;
		}
		catch(...)
		{
			return false;
		}
	}
	namespace detail
	{
		class CFileInput
		{
			IOHANDLE m_File;

		public:
			explicit CFileInput(IOHANDLE File) :
				m_File(File) {}
			CFileInput(const CFileInput &) = delete;
			CFileInput &operator=(const CFileInput &) = delete;
			~CFileInput() { Close(); }
			unsigned Read(char *pBuffer, unsigned Size) { return io_read(m_File, pBuffer, Size); }
			bool Failed() const { return io_error(m_File) != 0; }
			bool Close() { return !m_File || io_close(std::exchange(m_File, nullptr)) == 0; }
		};
		class CFileOutput
		{
			IStorage *m_pStorage;
			const char *m_pPath;
			std::string m_Candidate;
			IOHANDLE m_File = nullptr;
			bool m_Owned = false;

		public:
			CFileOutput(IStorage *pStorage, const char *pPath) :
				m_pStorage(pStorage), m_pPath(pPath) {}
			CFileOutput(const CFileOutput &) = delete;
			CFileOutput &operator=(const CFileOutput &) = delete;
			~CFileOutput()
			{
				Close();
				Discard();
			}
			bool Open()
			{
				static std::atomic<uint64_t> Sequence{0};
				m_Candidate = std::string(m_pPath) + ".candidate." + std::to_string(pid()) + "." + std::to_string(Sequence.fetch_add(1, std::memory_order_relaxed));
				if(m_Candidate.size() >= IO_MAX_PATH_LENGTH || m_pStorage->FileExists(m_Candidate.c_str(), IStorage::TYPE_SAVE))
					return false;
				m_File = m_pStorage->OpenFile(m_Candidate.c_str(), IOFLAG_WRITE, IStorage::TYPE_SAVE);
				m_Owned = m_File != nullptr;
				return m_Owned;
			}
			unsigned Write(const char *pText, unsigned Size) { return io_write(m_File, pText, Size); }
			bool Sync() { return io_error(m_File) == 0 && io_sync(m_File) == 0; }
			bool Close() { return !m_File || io_close(std::exchange(m_File, nullptr)) == 0; }
			bool Commit()
			{
				// 禁止Windows删除目标后重试；发布失败必须保留旧缓存。
				if(!m_pStorage->RenameFile(m_Candidate.c_str(), m_pPath, IStorage::TYPE_SAVE, false))
					return false;
				m_Owned = false;
				return true;
			}
			void Discard() noexcept
			{
				if(std::exchange(m_Owned, false))
					try
					{
						m_pStorage->RemoveFile(m_Candidate.c_str(), IStorage::TYPE_SAVE);
					}
					catch(...)
					{
					}
			}
		};
	}
	inline bool LoadFile(IStorage *pStorage, const char *pPath, CCollection &Collection) noexcept
	{
		if(!pStorage || !pPath || !pPath[0])
			return false;
		try
		{
			const IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_SAVE);
			if(!File)
				return false;
			detail::CFileInput Input(File);
			return Load(Input, Collection);
		}
		catch(...)
		{
			return false;
		}
	}
	inline bool SaveFile(IStorage *pStorage, const char *pPath, const CCollection &Collection) noexcept
	{
		if(!pStorage || !pPath || !pPath[0])
			return false;
		detail::CFileOutput Output(pStorage, pPath);
		return Save(Output, Collection);
	}
}
#endif
