/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#ifndef SDL_QM_IMMDEVICE_INFO_H
#define SDL_QM_IMMDEVICE_INFO_H

/* QmClient 修改版：由 SDL_immdevice.c 包含，校验设备属性后再读取联合体成员。 */
static void GetMMDeviceInfo(IMMDevice *device, char **utf8dev, WAVEFORMATEXTENSIBLE *fmt, GUID *guid)
{
    IPropertyStore *props = NULL;
    PROPVARIANT var;
    bool valid_format = false;

    *utf8dev = NULL;
    SDL_zerop(fmt);
    SDL_zerop(guid);
    if (FAILED(IMMDevice_OpenPropertyStore(device, STGM_READ, &props))) {
        return;
    }
    if (!props) {
        return;
    }

    /* GetValue 成功时仍可能返回 VT_EMPTY，不能直接把联合体解释为字符串。 */
    PropVariantInit(&var);
    if (SUCCEEDED(IPropertyStore_GetValue(props, &SDL_PKEY_Device_FriendlyName, &var)) &&
        var.vt == VT_LPWSTR && var.pwszVal && var.pwszVal[0]) {
        *utf8dev = WIN_StringToUTF8W(var.pwszVal);
    }
    PropVariantClear(&var);
    if (!*utf8dev) {
        IPropertyStore_Release(props);
        return;
    }

    /* 缺失或截断的格式不能交给后续设备注册；先复制基础结构，避免未对齐读取。 */
    PropVariantInit(&var);
    if (SUCCEEDED(IPropertyStore_GetValue(props, &SDL_PKEY_AudioEngine_DeviceFormat, &var)) &&
        var.vt == VT_BLOB && var.blob.pBlobData && var.blob.cbSize >= sizeof(WAVEFORMATEX)) {
        WAVEFORMATEX base_format;
        SDL_memcpy(&base_format, var.blob.pBlobData, sizeof(base_format));
        valid_format = base_format.cbSize <= var.blob.cbSize - sizeof(WAVEFORMATEX);
        if (base_format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            valid_format = valid_format && base_format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) &&
                           var.blob.cbSize >= sizeof(WAVEFORMATEXTENSIBLE);
        }
        if (valid_format) {
            SDL_memcpy(fmt, var.blob.pBlobData, SDL_min(var.blob.cbSize, sizeof(*fmt)));
        }
    }
    PropVariantClear(&var);
    if (!valid_format) {
        SDL_free(*utf8dev);
        *utf8dev = NULL;
        IPropertyStore_Release(props);
        return;
    }

    /* DirectSound GUID 为可选属性；失败时保留已初始化的零值。 */
    PropVariantInit(&var);
    if (SUCCEEDED(IPropertyStore_GetValue(props, &SDL_PKEY_AudioEndpoint_GUID, &var)) &&
        var.vt == VT_LPWSTR && var.pwszVal && var.pwszVal[0]) {
        GUID parsed_guid;
        if (SUCCEEDED(CLSIDFromString(var.pwszVal, &parsed_guid))) {
            *guid = parsed_guid;
        }
    }
    PropVariantClear(&var);
    IPropertyStore_Release(props);
}

#endif
