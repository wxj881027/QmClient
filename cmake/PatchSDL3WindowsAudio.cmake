# 在预编译 Windows SDL3 前替换设备属性读取函数；客户端链接阶段无法修复旧 DLL。
if(NOT DEFINED SDL3_SOURCE_DIR)
  message(FATAL_ERROR "SDL3_SOURCE_DIR is required")
endif()

set(QM_SDL_AUDIO_FILE "${SDL3_SOURCE_DIR}/src/core/windows/SDL_immdevice.c")
set(QM_SDL_AUDIO_HEADER "${CMAKE_CURRENT_LIST_DIR}/../qmclient_scripts/sdl3/SDL_qm_immdevice_info.h")
if(NOT EXISTS "${QM_SDL_AUDIO_HEADER}")
  message(FATAL_ERROR "missing SDL3 Windows audio patch implementation")
endif()
file(READ "${QM_SDL_AUDIO_FILE}" QM_SDL_AUDIO_SOURCE)
string(FIND "${QM_SDL_AUDIO_SOURCE}" "\r\n" QM_SDL_AUDIO_CRLF)
string(REPLACE "\r\n" "\n" QM_SDL_AUDIO_SOURCE "${QM_SDL_AUDIO_SOURCE}")
set(QM_SDL_AUDIO_ORIGINAL [=[static void GetMMDeviceInfo(IMMDevice *device, char **utf8dev, WAVEFORMATEXTENSIBLE *fmt, GUID *guid)
{
    /* PKEY_Device_FriendlyName gives you "Speakers (SoundBlaster Pro)" which drives me nuts. I'd rather it be
       "SoundBlaster Pro (Speakers)" but I guess that's developers vs users. Windows uses the FriendlyName in
       its own UIs, like Volume Control, etc. */
    IPropertyStore *props = NULL;
    *utf8dev = NULL;
    SDL_zerop(fmt);
    if (SUCCEEDED(IMMDevice_OpenPropertyStore(device, STGM_READ, &props))) {
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(IPropertyStore_GetValue(props, &SDL_PKEY_Device_FriendlyName, &var))) {
            *utf8dev = WIN_StringToUTF8W(var.pwszVal);
        }
        PropVariantClear(&var);
        if (SUCCEEDED(IPropertyStore_GetValue(props, &SDL_PKEY_AudioEngine_DeviceFormat, &var))) {
            SDL_memcpy(fmt, var.blob.pBlobData, SDL_min(var.blob.cbSize, sizeof(WAVEFORMATEXTENSIBLE)));
        }
        PropVariantClear(&var);
        if (SUCCEEDED(IPropertyStore_GetValue(props, &SDL_PKEY_AudioEndpoint_GUID, &var))) {
            (void)CLSIDFromString(var.pwszVal, guid);
        }
        PropVariantClear(&var);
        IPropertyStore_Release(props);
    }
}]=])
set(QM_SDL_AUDIO_PATCHED "#include \"SDL_qm_immdevice_info.h\"")

string(FIND "${QM_SDL_AUDIO_SOURCE}" "${QM_SDL_AUDIO_PATCHED}" QM_SDL_AUDIO_PATCH_POSITION)
if(QM_SDL_AUDIO_PATCH_POSITION EQUAL -1)
  string(FIND "${QM_SDL_AUDIO_SOURCE}" "${QM_SDL_AUDIO_ORIGINAL}" QM_SDL_AUDIO_ORIGINAL_POSITION)
  if(QM_SDL_AUDIO_ORIGINAL_POSITION EQUAL -1)
    message(FATAL_ERROR "unexpected SDL3 Windows audio source; review the dependency patch")
  endif()
  string(REPLACE "${QM_SDL_AUDIO_ORIGINAL}" "${QM_SDL_AUDIO_PATCHED}" QM_SDL_AUDIO_SOURCE "${QM_SDL_AUDIO_SOURCE}")
  if(NOT QM_SDL_AUDIO_CRLF EQUAL -1)
    string(REPLACE "\n" "\r\n" QM_SDL_AUDIO_SOURCE "${QM_SDL_AUDIO_SOURCE}")
  endif()
  file(WRITE "${QM_SDL_AUDIO_FILE}" "${QM_SDL_AUDIO_SOURCE}")
endif()

# 重复构建时也同步实现，避免缓存源码保留旧修复。
configure_file("${QM_SDL_AUDIO_HEADER}" "${SDL3_SOURCE_DIR}/src/core/windows/SDL_qm_immdevice_info.h" COPYONLY)
