#ifndef ENGINE_CLIENT_BACKEND_METAL_METAL_SHADER_MANIFEST_H
#define ENGINE_CLIENT_BACKEND_METAL_METAL_SHADER_MANIFEST_H

#include <array>

// 逻辑 shader family 必须在 OpenGL、Vulkan 和 Metal 资源中保持可追踪。
struct SMetalShaderFamilyManifest
{
	const char *m_pOpenGLName;
	const char *m_pVulkanName;
};

inline constexpr std::array<SMetalShaderFamilyManifest, 12> g_aMetalShaderFamilies{{
	{"prim", "prim"},
	{"pipeline", "prim3d"},
	{"text", "text"},
	{"tile", "tile"},
	{"tile_border", "tile_border"},
	{"quad", "quad"},
	{"primex", "primex"},
	{"spritemulti", "spritemulti"},
	{"textured_msdf", "textured_msdf"},
	{"rounded_rect_sdf", "rounded_rect_sdf"},
	{"media_island_sdf", "media_island_sdf"},
	{"gaussian_blur", "gaussian_blur"},
}};

inline constexpr std::array<const char *, 28> g_aMetalShaderEntrypoints{{
	"qmclient_vertex",
	"qmclient_fragment",
	"qmclient_textured_fragment",
	"qmclient_textured_msdf_fragment",
	"qmclient_rounded_rect_sdf_fragment",
	"qmclient_media_island_sdf_fragment",
	"qmclient_gaussian_blur_fragment",
	"qmclient_tex_array_vertex",
	"qmclient_tex_array_fragment",
	"qmclient_text_fragment",
	"qmclient_tile_vertex",
	"qmclient_tile_border_vertex",
	"qmclient_tile_plain_vertex",
	"qmclient_tile_fragment",
	"qmclient_tile_textured_fragment",
	"qmclient_tile_border_textured_fragment",
	"qmclient_quad_vertex_grouped",
	"qmclient_quad_vertex_ungrouped",
	"qmclient_quad_plain_vertex_grouped",
	"qmclient_quad_plain_vertex_ungrouped",
	"qmclient_quad_fragment",
	"qmclient_quad_textured_fragment",
	"qmclient_quad_container_ex_vertex",
	"qmclient_quad_container_ex_fragment",
	"qmclient_quad_container_ex_textured_fragment",
	"qmclient_sprite_multiple_vertex",
	"qmclient_sprite_multiple_fragment",
	"qmclient_sprite_multiple_textured_fragment",
}};

#endif
