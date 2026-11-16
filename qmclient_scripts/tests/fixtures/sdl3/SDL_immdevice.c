/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
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
/* SDL 3.4.18 构建补丁 fixture，仅保留替换目标及周围标记。 */
/* fixture prefix */
static void GetMMDeviceInfo(IMMDevice *device, char **utf8dev, WAVEFORMATEXTENSIBLE *fmt, GUID *guid)
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
}
/* fixture suffix */
