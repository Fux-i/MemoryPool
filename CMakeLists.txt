cmake_minimum_required(VERSION 3.10.0)
project(MemoryPool VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置头文件包含目录
include_directories(${CMAKE_SOURCE_DIR})

set(SRC
    src/main.cpp
    src/MemoryPool_v2/PageCache.cpp
    src/MemoryPool_v2/PageSpan.cpp
    src/MemoryPool_v2/CentralCache.cpp
    src/MemoryPool_v2/ThreadCache.cpp
)

# ============ 主程序可执行文件 ============
add_executable(${PROJECT_NAME} ${SRC})
