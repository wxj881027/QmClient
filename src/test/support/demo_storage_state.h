#ifndef TEST_SUPPORT_DEMO_STORAGE_STATE_H
#define TEST_SUPPORT_DEMO_STORAGE_STATE_H

#include <engine/shared/demo.h>
#include <engine/shared/network.h>
#include <engine/storage.h>

#include <gtest/gtest.h>
#include <test/test.h>

#include <cstdlib>
#include <memory>
#include <string>

// 状态可由异步测试共享持有；超时后不借用测试栈或已经销毁的存储。
struct CDemoStorageState
{
	CTestInfo m_Info;
	std::unique_ptr<IStorage> m_pStorage = m_Info.CreateTestStorage();
	rust::Box<CSnapshotDelta> m_pDelta = CSnapshotDelta::New();
	rust::Box<CSnapshotDelta> m_pDeltaSixup = CSnapshotDelta::New();
	CDemoRecorder m_Recorder{&*m_pDelta};

	~CDemoStorageState()
	{
		if(m_Recorder.IsRecording())
			m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE);
	}

	void Write(const char *pPath, const std::string &Bytes)
	{
		IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_WRITE, IStorage::TYPE_SAVE_OR_ABSOLUTE);
		ASSERT_NE(File, nullptr);
		EXPECT_EQ(io_write(File, Bytes.data(), static_cast<unsigned>(Bytes.size())), Bytes.size());
		EXPECT_EQ(io_close(File), 0);
	}

	std::string Read(const char *pPath)
	{
		IOHANDLE File = m_pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_SAVE_OR_ABSOLUTE);
		EXPECT_NE(File, nullptr);
		if(!File)
			return {};
		const int64_t Size = io_length(File);
		std::string Bytes(Size > 0 ? static_cast<size_t>(Size) : 0, '\0');
		EXPECT_EQ(io_read(File, Bytes.data(), static_cast<unsigned>(Bytes.size())), Bytes.size());
		EXPECT_EQ(io_close(File), 0);
		return Bytes;
	}

	bool Exists(const char *pPath)
	{
		return m_pStorage->FileExists(pPath, IStorage::TYPE_SAVE_OR_ABSOLUTE);
	}

	void Begin(const char *pPath = "source.demo", bool Snapshots = true)
	{
		unsigned char aMap[] = {'m', 'a', 'p', 'b', 'y', 't', 'e', 's'};
		ASSERT_EQ(m_Recorder.Start(m_pStorage.get(), nullptr, pPath, "test", "map", SHA256_DIGEST{}, 0, "client", sizeof(aMap), aMap, nullptr, nullptr, nullptr), 0);
		if(Snapshots)
		{
			const CSnapshot *pSnapshot = CSnapshot::EmptySnapshot();
			const int Size = static_cast<int>(pSnapshot->AsSlice().size() * sizeof(int32_t));
			m_Recorder.RecordSnapshot(100, pSnapshot, Size);
			const int aMessage[] = {1, 2, 3};
			m_Recorder.RecordMessage(aMessage, sizeof(aMessage));
			m_Recorder.RecordSnapshot(150, pSnapshot, Size, true);
		}
		else
		{
			m_Recorder.RecordTickMarker(100, true);
			const int aMessage[] = {1, 2, 3};
			m_Recorder.RecordMessage(aMessage, sizeof(aMessage));
			m_Recorder.RecordTickMarker(150, true);
		}
	}

	void Create(const char *pPath = "source.demo", bool Snapshots = true)
	{
		Begin(pPath, Snapshots);
		ASSERT_EQ(m_Recorder.Stop(IDemoRecorder::EStopMode::KEEP_FILE), 0);
	}

	std::string MapData(const char *pPath)
	{
		CDemoPlayer Player(&*m_pDelta, &*m_pDeltaSixup, false);
		EXPECT_EQ(Player.Load(m_pStorage.get(), nullptr, pPath, IStorage::TYPE_SAVE_OR_ABSOLUTE, false), 0);
		std::unique_ptr<unsigned char, decltype(&std::free)> pData(Player.GetMapData(m_pStorage.get()), &std::free);
		const std::string Bytes = pData ? std::string(reinterpret_cast<const char *>(pData.get()), Player.GetMapInfo()->m_Size) : "";
		Player.Stop();
		return Bytes;
	}
};

#endif
