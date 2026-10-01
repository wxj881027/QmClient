#include <engine/client/backend/opengl/opengl_qm_sl_program.h>

// 同一翻译单元同时声明桌面 GL 和 GLES 类型，验证拆分后的平台类名映射。
// 这是编译合同，不需要图形上下文，也不验证 shader 的运行时效果。
#define GLES_CLASS_DEFINES_DO_DEFINE
#include <engine/client/backend/opengles/gles_class_defines.h>
#undef GLES_CLASS_DEFINES_DO_DEFINE

#define BACKEND_AS_OPENGL_ES 1
#include <engine/client/backend/opengl/opengl_qm_sl_program.h>
#undef BACKEND_AS_OPENGL_ES

#include <engine/client/backend/opengles/gles_class_defines.h>

#include <type_traits>

static_assert(std::is_base_of_v<CGLSLTWProgram, CGLSLMediaIslandSdfProgram>);
static_assert(std::is_base_of_v<CGLSLTWProgram, CGLSLRoundedRectSdfProgram>);
static_assert(std::is_base_of_v<CGLSLTWProgram, CGLSLTexturedMsdfProgram>);
static_assert(std::is_base_of_v<CGLSLTWProgram, CGLSLGaussianBlurProgram>);

static_assert(std::is_base_of_v<CGLSL_ESTWProgram, CGLSL_ESMediaIslandSdfProgram>);
static_assert(std::is_base_of_v<CGLSL_ESTWProgram, CGLSL_ESRoundedRectSdfProgram>);
static_assert(std::is_base_of_v<CGLSL_ESTWProgram, CGLSL_ESTexturedMsdfProgram>);
static_assert(std::is_base_of_v<CGLSL_ESTWProgram, CGLSL_ESGaussianBlurProgram>);

static_assert(!std::is_same_v<CGLSLMediaIslandSdfProgram, CGLSL_ESMediaIslandSdfProgram>);
static_assert(!std::is_same_v<CGLSLRoundedRectSdfProgram, CGLSL_ESRoundedRectSdfProgram>);
static_assert(!std::is_same_v<CGLSLTexturedMsdfProgram, CGLSL_ESTexturedMsdfProgram>);
static_assert(!std::is_same_v<CGLSLGaussianBlurProgram, CGLSL_ESGaussianBlurProgram>);
