cmake_minimum_required(VERSION 3.16)

project(HvectorBench)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 默认保持原来的调试配置：-O0 + ASan/UBSan，用于正确性和内存检查。
# 做性能对比时用 -DENABLE_SANITIZERS=OFF 配置一个独立的 build 目录，
# 否则 -O0 和插桩会让 Hvector / std::vector 的对比失去意义。
option(ENABLE_SANITIZERS "Build with -O0 -fsanitize=address,undefined" ON)
if(ENABLE_SANITIZERS)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g -fsanitize=address,undefined")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
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

# 新的 benchmark
add_executable(bench cpp/bench.cpp)
target_include_directories(bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bench PRIVATE benchmark::benchmark)

# 正确性验证
add_executable(matmul_main cpp/matmul_main.cpp)
target_include_directories(matmul_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(matmul_main PRIVATE -O2)

# benchmark
add_executable(matmul_bench cpp/matmul_bench.cpp)
target_include_directories(matmul_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(matmul_bench PRIVATE benchmark::benchmark)