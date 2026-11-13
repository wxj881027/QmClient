include_guard(GLOBAL)

function(qm_resolve_gtest_targets libraries_variable main_variable)
  set(libraries)
  set(main)
  # 使用同一来源的成对目标，不能把外部 Google Test 与下载的 Google Mock 混用。
  if(TARGET GTest::gtest AND TARGET GTest::gmock)
    set(libraries GTest::gtest GTest::gmock)
    if(TARGET GTest::gtest_main)
      set(main GTest::gtest_main)
    endif()
  elseif(TARGET gtest AND TARGET gmock)
    set(libraries gtest gmock)
    if(TARGET gtest_main)
      set(main gtest_main)
    endif()
  elseif(GTEST_FOUND AND SERVER)
    # 已导入的外部目标不能在本次配置中被下载版的同名目标覆盖。
    message(WARNING
      "Google Test was found without a matching Google Mock target. C++ test targets are disabled. "
      "Install a complete Google Test/Google Mock package, or configure with "
      "-DCMAKE_DISABLE_FIND_PACKAGE_GTest=ON -DDOWNLOAD_GTEST=ON to use the bundled pair.")
  endif()
  set(${libraries_variable} ${libraries} PARENT_SCOPE)
  set(${main_variable} ${main} PARENT_SCOPE)
endfunction()
