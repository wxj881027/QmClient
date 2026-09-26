#ifndef ENGINE_CLIENT_BACKEND_METAL_BACKEND_METAL_H
#define ENGINE_CLIENT_BACKEND_METAL_BACKEND_METAL_H

#include <engine/client/backend/backend_base.h>

// 创建原生 Metal 命令处理器；调用方需保持 Apple/METAL 编译保护。
CCommandProcessorFragment_GLBase *CreateMetalCommandProcessorFragment();

#endif
