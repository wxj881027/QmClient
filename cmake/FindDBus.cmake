find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  # imported target 保留非系统库路径及 pkg-config 提供的编译、链接选项。
  pkg_check_modules(DBUS QUIET IMPORTED_TARGET dbus-1)
else()
  # 重配置时不能沿用此前成功查询留在缓存中的 FOUND 状态。
  set(DBUS_FOUND FALSE)
endif()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DBus DEFAULT_MSG DBUS_FOUND)
