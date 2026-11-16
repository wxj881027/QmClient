#include <windows.h>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <algorithm>
#include <cstring>
#include <cwchar>

// 将 SDL 的 C COM 调用适配到测试使用的 C++ COM 接口；属性读取逻辑直接包含生产实现。
#define IMMDevice_OpenPropertyStore(Device, Mode, Store) (Device)->OpenPropertyStore(Mode, Store)
#define IPropertyStore_GetValue(Store, Key, Value) (Store)->GetValue(*(Key), Value)
#define IPropertyStore_Release(Store) (Store)->Release()
#define WIN_StringToUTF8W(S) SDL_iconv_string("UTF-8", "UTF-16LE", (const char *)(S), (SDL_wcslen(S) + 1) * sizeof(WCHAR))

static const PROPERTYKEY SDL_PKEY_Device_FriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
static const PROPERTYKEY SDL_PKEY_AudioEngine_DeviceFormat = {{0xf19f064d, 0x082c, 0x4e27, {0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c}}, 0};
static const PROPERTYKEY SDL_PKEY_AudioEndpoint_GUID = {{0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 4};

#include "../../../../qmclient_scripts/sdl3/SDL_qm_immdevice_info.h"

namespace
{
	enum class EPropertyValue
	{
		VALID,
		MISSING,
		WRONG_TYPE,
		NULL_POINTER,
		EMPTY_STRING,
		FAILURE,
	};

	class CPropertyStore : public IPropertyStore
	{
	public:
		EPropertyValue m_Name = EPropertyValue::VALID;
		EPropertyValue m_Format = EPropertyValue::VALID;
		EPropertyValue m_Guid = EPropertyValue::VALID;
		WAVEFORMATEXTENSIBLE m_WaveFormat{};
		ULONG m_BlobSize = sizeof(WAVEFORMATEX);
		const wchar_t *m_pGuid = L"{8AC08E45-61A8-49A1-B6B3-790AD002D8A4}";
		int m_Releases = 0;
		int m_FormatReads = 0;
		ULONG m_References = 1;

		CPropertyStore()
		{
			m_WaveFormat.Format.wFormatTag = WAVE_FORMAT_PCM;
			m_WaveFormat.Format.nChannels = 2;
			m_WaveFormat.Format.nSamplesPerSec = 48000;
			m_WaveFormat.Format.wBitsPerSample = 16;
			m_WaveFormat.Format.nBlockAlign = 4;
			m_WaveFormat.Format.nAvgBytesPerSec = 192000;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **ppObject) override
		{
			*ppObject = nullptr;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++m_References; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			++m_Releases;
			return --m_References;
		}
		HRESULT STDMETHODCALLTYPE GetCount(DWORD *) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetAt(DWORD, PROPERTYKEY *) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE SetValue(REFPROPERTYKEY, REFPROPVARIANT) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE Commit() override { return E_NOTIMPL; }

		static HRESULT StringValue(EPropertyValue Value, const wchar_t *pText, PROPVARIANT *pResult)
		{
			if(Value == EPropertyValue::FAILURE)
				return E_FAIL;
			if(Value == EPropertyValue::MISSING)
				return S_OK;
			if(Value == EPropertyValue::WRONG_TYPE)
			{
				pResult->vt = VT_UI4;
				pResult->ulVal = 1;
				return S_OK;
			}
			pResult->vt = VT_LPWSTR;
			if(Value == EPropertyValue::NULL_POINTER)
				return S_OK;
			if(Value == EPropertyValue::EMPTY_STRING)
				pText = L"";
			const size_t Size = (std::wcslen(pText) + 1) * sizeof(wchar_t);
			pResult->pwszVal = static_cast<LPWSTR>(CoTaskMemAlloc(Size));
			if(!pResult->pwszVal)
				return E_OUTOFMEMORY;
			std::memcpy(pResult->pwszVal, pText, Size);
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetValue(REFPROPERTYKEY Key, PROPVARIANT *pResult) override
		{
			EXPECT_EQ(pResult->vt, VT_EMPTY);
			if(IsEqualGUID(Key.fmtid, SDL_PKEY_Device_FriendlyName.fmtid) && Key.pid == SDL_PKEY_Device_FriendlyName.pid)
				return StringValue(m_Name, L"扬声器 (测试设备)", pResult);
			if(IsEqualGUID(Key.fmtid, SDL_PKEY_AudioEndpoint_GUID.fmtid) && Key.pid == SDL_PKEY_AudioEndpoint_GUID.pid)
				return StringValue(m_Guid, m_pGuid, pResult);
			if(!IsEqualGUID(Key.fmtid, SDL_PKEY_AudioEngine_DeviceFormat.fmtid) || Key.pid != SDL_PKEY_AudioEngine_DeviceFormat.pid)
				return E_INVALIDARG;

			++m_FormatReads;
			if(m_Format == EPropertyValue::FAILURE)
				return E_FAIL;
			if(m_Format == EPropertyValue::MISSING)
				return S_OK;
			if(m_Format == EPropertyValue::WRONG_TYPE)
			{
				pResult->vt = VT_UI4;
				pResult->ulVal = 1;
				return S_OK;
			}
			pResult->vt = VT_BLOB;
			pResult->blob.cbSize = m_BlobSize;
			if(m_Format == EPropertyValue::NULL_POINTER)
				return S_OK;
			pResult->blob.pBlobData = static_cast<BYTE *>(CoTaskMemAlloc(m_BlobSize));
			if(!pResult->blob.pBlobData)
				return E_OUTOFMEMORY;
			std::memset(pResult->blob.pBlobData, 0, m_BlobSize);
			std::memcpy(pResult->blob.pBlobData, &m_WaveFormat, std::min<size_t>(m_BlobSize, sizeof(m_WaveFormat)));
			return S_OK;
		}
	};

	class CDevice : public IMMDevice
	{
	public:
		CPropertyStore m_Store;
		HRESULT m_OpenResult = S_OK;
		bool m_NullStore = false;

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **ppObject) override
		{
			*ppObject = nullptr;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
		ULONG STDMETHODCALLTYPE Release() override { return 1; }
		HRESULT STDMETHODCALLTYPE Activate(REFIID, DWORD, PROPVARIANT *, void **) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetId(LPWSTR *) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE GetState(DWORD *) override { return E_NOTIMPL; }
		HRESULT STDMETHODCALLTYPE OpenPropertyStore(DWORD Access, IPropertyStore **ppStore) override
		{
			EXPECT_EQ(Access, STGM_READ);
			*ppStore = nullptr;
			if(SUCCEEDED(m_OpenResult) && !m_NullStore)
			{
				m_Store.AddRef();
				*ppStore = &m_Store;
			}
			return m_OpenResult;
		}
	};

	class CSdlWindowsAudioProperties : public testing::Test
	{
	protected:
		CDevice m_Device;
		char *m_pName = nullptr;
		WAVEFORMATEXTENSIBLE m_Format{};
		GUID m_Guid{};

		void Read()
		{
			SDL_free(m_pName);
			m_pName = nullptr;
			std::memset(&m_Format, 0xcc, sizeof(m_Format));
			std::memset(&m_Guid, 0xcc, sizeof(m_Guid));
			GetMMDeviceInfo(&m_Device, &m_pName, &m_Format, &m_Guid);
			EXPECT_EQ(m_Device.m_Store.m_References, 1u);
		}

		void ExpectSkipped()
		{
			Read();
			EXPECT_EQ(m_pName, nullptr);
			const WAVEFORMATEXTENSIBLE EmptyFormat{};
			EXPECT_EQ(std::memcmp(&m_Format, &EmptyFormat, sizeof(m_Format)), 0);
			EXPECT_TRUE(IsEqualGUID(m_Guid, GUID{}));
		}

		void TearDown() override { SDL_free(m_pName); }
	};
}

TEST_F(CSdlWindowsAudioProperties, ValidPcmDevicePreservesNameFormatAndGuid)
{
	Read();
	ASSERT_NE(m_pName, nullptr);
	EXPECT_STREQ(m_pName, "扬声器 (测试设备)");
	EXPECT_EQ(m_Format.Format.nChannels, 2);
	EXPECT_EQ(m_Format.Format.nSamplesPerSec, 48000u);
	EXPECT_EQ(m_Format.Format.wBitsPerSample, 16);
	EXPECT_EQ(m_Guid.Data1, 0x8ac08e45u);
	EXPECT_EQ(m_Device.m_Store.m_Releases, 1);
}

TEST_F(CSdlWindowsAudioProperties, MissingNameWithSuccessfulResultSkipsDevice)
{
	m_Device.m_Store.m_Name = EPropertyValue::MISSING;
	ExpectSkipped();
	EXPECT_EQ(m_Device.m_Store.m_FormatReads, 0);
	EXPECT_EQ(m_Device.m_Store.m_Releases, 1);
}

TEST_F(CSdlWindowsAudioProperties, NullWideNameSkipsDevice)
{
	m_Device.m_Store.m_Name = EPropertyValue::NULL_POINTER;
	ExpectSkipped();
}

TEST_F(CSdlWindowsAudioProperties, WrongNameTypeIsNotInterpretedAsPointer)
{
	m_Device.m_Store.m_Name = EPropertyValue::WRONG_TYPE;
	ExpectSkipped();
}

TEST_F(CSdlWindowsAudioProperties, EmptyNameSkipsDevice)
{
	m_Device.m_Store.m_Name = EPropertyValue::EMPTY_STRING;
	ExpectSkipped();
}

TEST_F(CSdlWindowsAudioProperties, FailedNameReadSkipsDevice)
{
	m_Device.m_Store.m_Name = EPropertyValue::FAILURE;
	ExpectSkipped();
}

TEST_F(CSdlWindowsAudioProperties, MissingFormatDiscardsAllocatedName)
{
	m_Device.m_Store.m_Format = EPropertyValue::MISSING;
	ExpectSkipped();
	EXPECT_EQ(m_Device.m_Store.m_Releases, 1);
}

TEST_F(CSdlWindowsAudioProperties, WrongFormatTypeIsNotInterpretedAsBlob)
{
	m_Device.m_Store.m_Format = EPropertyValue::WRONG_TYPE;
	ExpectSkipped();
}

TEST_F(CSdlWindowsAudioProperties, NullFormatBlobSkipsDevice)
{
	m_Device.m_Store.m_Format = EPropertyValue::NULL_POINTER;
	ExpectSkipped();
}

TEST_F(CSdlWindowsAudioProperties, FailedFormatReadDiscardsName)
{
	m_Device.m_Store.m_Format = EPropertyValue::FAILURE;
	ExpectSkipped();
}

TEST_F(CSdlWindowsAudioProperties, TruncatedBaseFormatSkipsDevice)
{
	m_Device.m_Store.m_BlobSize = sizeof(WAVEFORMATEX) - 1;
	ExpectSkipped();
}

TEST_F(CSdlWindowsAudioProperties, MissingDeclaredFormatExtensionSkipsDevice)
{
	m_Device.m_Store.m_WaveFormat.Format.cbSize = 1;
	ExpectSkipped();
}

TEST_F(CSdlWindowsAudioProperties, TruncatedExtensibleFormatSkipsDevice)
{
	m_Device.m_Store.m_WaveFormat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	ExpectSkipped();
}

TEST_F(CSdlWindowsAudioProperties, ValidExtensibleFormatPreservesExtension)
{
	auto &Store = m_Device.m_Store;
	Store.m_BlobSize = sizeof(WAVEFORMATEXTENSIBLE);
	Store.m_WaveFormat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	Store.m_WaveFormat.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	Store.m_WaveFormat.Samples.wValidBitsPerSample = 16;
	Store.m_WaveFormat.dwChannelMask = 3;
	Read();
	ASSERT_NE(m_pName, nullptr);
	EXPECT_EQ(m_Format.Samples.wValidBitsPerSample, 16);
	EXPECT_EQ(m_Format.dwChannelMask, 3u);
}

TEST_F(CSdlWindowsAudioProperties, MissingOptionalGuidKeepsDeviceAndClearsGuid)
{
	m_Device.m_Store.m_Guid = EPropertyValue::MISSING;
	Read();
	EXPECT_NE(m_pName, nullptr);
	EXPECT_TRUE(IsEqualGUID(m_Guid, GUID{}));
}

TEST_F(CSdlWindowsAudioProperties, WrongOptionalGuidTypeKeepsDevice)
{
	m_Device.m_Store.m_Guid = EPropertyValue::WRONG_TYPE;
	Read();
	EXPECT_NE(m_pName, nullptr);
	EXPECT_TRUE(IsEqualGUID(m_Guid, GUID{}));
}

TEST_F(CSdlWindowsAudioProperties, NullOptionalGuidKeepsDevice)
{
	m_Device.m_Store.m_Guid = EPropertyValue::NULL_POINTER;
	Read();
	EXPECT_NE(m_pName, nullptr);
	EXPECT_TRUE(IsEqualGUID(m_Guid, GUID{}));
}

TEST_F(CSdlWindowsAudioProperties, InvalidGuidTextKeepsZeroGuid)
{
	m_Device.m_Store.m_pGuid = L"{not-a-guid}";
	Read();
	EXPECT_NE(m_pName, nullptr);
	EXPECT_TRUE(IsEqualGUID(m_Guid, GUID{}));
}

TEST_F(CSdlWindowsAudioProperties, UnavailablePropertyStoreClearsOutputs)
{
	m_Device.m_OpenResult = E_FAIL;
	ExpectSkipped();
	EXPECT_EQ(m_Device.m_Store.m_Releases, 0);
}

TEST_F(CSdlWindowsAudioProperties, SuccessfulOpenWithoutStoreClearsOutputs)
{
	m_Device.m_NullStore = true;
	ExpectSkipped();
}

TEST_F(CSdlWindowsAudioProperties, DeviceCanBeReadAfterItsNameBecomesAvailable)
{
	m_Device.m_Store.m_Name = EPropertyValue::MISSING;
	ExpectSkipped();
	m_Device.m_Store.m_Name = EPropertyValue::VALID;
	Read();
	EXPECT_NE(m_pName, nullptr);
	EXPECT_EQ(m_Format.Format.nSamplesPerSec, 48000u);
	EXPECT_EQ(m_Device.m_Store.m_Releases, 2);
}
