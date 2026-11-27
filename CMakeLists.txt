cmake_minimum_required(VERSION 3.10)
project(lyrics_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 核心库（纯头文件 + 实现，无第三方依赖）
add_library(lyrics_engine STATIC
    src/lyrics_engine.cpp
    include/lyrics_engine.h
)
target_include_directories(lyrics_engine PUBLIC include)

# 控制台演示
add_executable(demo src/main.cpp)
target_link_libraries(demo PRIVATE lyrics_engine)

# 单元测试
add_executable(test_engine tests/test_engine.cpp)
target_link_libraries(test_engine PRIVATE lyrics_engine)

enable_testing()
add_test(NAME engine_tests COMMAND test_engine)
