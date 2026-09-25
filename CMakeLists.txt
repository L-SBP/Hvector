cmake_minimum_required(VERSION 3.16)

project(HvectorBench LANGUAGES CXX)

# 是否作为顶层项目（用户直接配置本仓库），而不是被别的项目 FetchContent/add_subdirectory 拉进来。
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(HVECTOR_IS_TOP_LEVEL ON)
else()
  set(HVECTOR_IS_TOP_LEVEL OFF)
endif()

# 顶层时默认构建测试/benchmark；被其他项目 FetchContent 拉进来时默认只提供头文件库。
option(HVECTOR_BUILD_TESTS "Build Hvector tests and benchmarks" ${HVECTOR_IS_TOP_LEVEL})

# ============================================================================
# 对外提供的头文件库（只有 hpp/ 下的内容会被安装/导出，cpp/ 全是测试代码）
# 其他项目用法：
#   include(FetchContent)
#   FetchContent_Declare(hvector
#     GIT_REPOSITORY <你的仓库地址>
#     GIT_TAG        <tag 或 commit>)
#   FetchContent_MakeAvailable(hvector)
#   target_link_libraries(app PRIVATE hvector::hvector)
# 用法二（install 后）：
#   find_package(hvector REQUIRED)
#   target_link_libraries(app PRIVATE hvector::hvector)
# ============================================================================
add_library(hvector INTERFACE)
add_library(hvector::hvector ALIAS hvector)

target_include_directories(hvector INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/hpp>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
# 头文件库对使用者的最低标准要求由 CMake 自动传播（等价于 target_compile_features）。
target_compile_features(hvector INTERFACE cxx_std_17)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS hvector EXPORT hvectorTargets)
# 只暴露库本体；hpp/ 下的 matmul.hpp 属于测试代码，不安装。
install(FILES hpp/hvector.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT hvectorTargets
  FILE hvectorTargets.cmake
  NAMESPACE hvector::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hvector
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/hvectorConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/hvectorConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hvector
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/hvectorConfigVersion.cmake
  VERSION 1.0.0
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/hvectorConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/hvectorConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hvector
)

if(NOT HVECTOR_BUILD_TESTS)
  return()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 测试专用的编译/链接选项，只作用于本项目的可执行文件，绝不写进 CMAKE_CXX_FLAGS，
# 以免污染把本仓库 FetchContent 进来的父项目。
# 默认保持原来的调试配置：-O0 + ASan/UBSan，用于正确性和内存检查。
# 做性能对比时用 -DENABLE_SANITIZERS=OFF 配置一个独立的 build 目录，
# 否则 -O0 和插桩会让 Hvector / std::vector 的对比失去意义。
option(ENABLE_SANITIZERS "Build with -O0 -fsanitize=address,undefined" ON)
add_library(hvector_test_options INTERFACE)
if(ENABLE_SANITIZERS)
  target_compile_options(hvector_test_options INTERFACE -O0 -g -fsanitize=address,undefined)
  target_link_options(hvector_test_options INTERFACE -fsanitize=address,undefined)
else()
  target_compile_options(hvector_test_options INTERFACE -O2)
endif()

include(FetchContent)
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.8.3
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_BUILD_TYPE Release CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

# 原来的 Hvector
add_executable(Hvector cpp/main.cpp)
target_include_directories(Hvector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Hvector PRIVATE hvector::hvector hvector_test_options)

# 新的 benchmark
add_executable(bench cpp/bench.cpp)
target_include_directories(bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bench PRIVATE hvector::hvector hvector_test_options benchmark::benchmark)

# 正确性验证
add_executable(matmul_main cpp/matmul_main.cpp)
target_include_directories(matmul_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(matmul_main PRIVATE hvector::hvector hvector_test_options)

# benchmark
add_executable(matmul_bench cpp/matmul_bench.cpp)
target_include_directories(matmul_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(matmul_bench PRIVATE hvector::hvector hvector_test_options benchmark::benchmark)