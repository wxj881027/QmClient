#ifndef IOS_IOS_MAIN_H
#define IOS_IOS_MAIN_H

#include <base/detect.h>

#if !defined(CONF_PLATFORM_IOS)
#error "This header should only be included when compiling for iOS"
#endif

struct SDL_Window;

void IosDisplayCutoutInsets(SDL_Window *pWindow, int *pLeft, int *pRight);

const char *InitIos();

#endif
