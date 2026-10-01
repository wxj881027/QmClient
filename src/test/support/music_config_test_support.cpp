// 独立音乐测试只需要真实配置对象的存储，注册表通过生产接口检查字段地址。
// 完整 testrunner 由 config.cpp 提供该对象，不能重复链接本编译单元。
#include <engine/shared/config.h>

CConfig g_Config;
