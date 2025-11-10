cmake_minimum_required(VERSION 3.10.0)
project(MemoryPool VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用 Release 模式
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 添加优化标志
if(CMAKE_BUILD_TYPE MATCHES Release)
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
    message(STATUS "编译模式: Release (优化: -O3)")
else()
    message(STATUS "编译模式: ${CMAKE_BUILD_TYPE}")
endif()

# 设置头文件包含目录
include_directories(${CMAKE_SOURCE_DIR}/src)

set(SRC
    src/main.cpp
    src/MemoryPool_v2/PageCache.cpp
    src/MemoryPool_v2/PageSpan.cpp
    src/MemoryPool_v2/CentralCache.cpp
    src/MemoryPool_v2/ThreadCache.cpp
)

# MemoryPool 源文件
set(MEMORYPOOL_LIB_SRC
    src/MemoryPool_v2/PageCache.cpp
    src/MemoryPool_v2/PageSpan.cpp
    src/MemoryPool_v2/CentralCache.cpp
    src/MemoryPool_v2/ThreadCache.cpp
)

# ============ 主程序可执行文件 ============
add_executable(${PROJECT_NAME} ${SRC})

# ============ Google Test 配置 ============
option(BUILD_PERFORMANCE_TEST "Build performance tests" ON)

if(BUILD_PERFORMANCE_TEST)
    # 首先尝试查找系统已安装的 Google Test
    find_package(GTest QUIET)
    
    if(NOT GTest_FOUND)
        message(STATUS "系统未找到 Google Test，尝试使用本地源码...")
        # 检查是否有本地的 googletest 目录
        if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/googletest")
            message(STATUS "使用本地 Google Test: ${CMAKE_SOURCE_DIR}/third_party/googletest")
            add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/googletest EXCLUDE_FROM_ALL)
            set(GTEST_FOUND TRUE)
        else()
            message(WARNING "未找到 Google Test")
            message(STATUS "请选择以下方式之一：")
            message(STATUS "  1. 安装 Google Test: sudo apt-get install libgtest-dev")
            message(STATUS "  2. 手动下载到: ${CMAKE_SOURCE_DIR}/third_party/googletest")
            message(STATUS "  3. 禁用性能测试: cmake -DBUILD_PERFORMANCE_TEST=OFF ..")
            set(GTEST_FOUND FALSE)
        endif()
    else()
        message(STATUS "使用系统已安装的 Google Test")
        set(GTEST_FOUND TRUE)
    endif()

    if(GTEST_FOUND)
        enable_testing()

        # ============ 性能测试可执行文件 ============
        add_executable(performance_test 
            tests/performance_test.cpp
            ${MEMORYPOOL_LIB_SRC}
        )

        # 链接 Google Test
        if(TARGET GTest::gtest)
            target_link_libraries(performance_test 
                GTest::gtest
                GTest::gtest_main
            )
        else()
            target_link_libraries(performance_test 
                gtest
                gtest_main
            )
        endif()

        # 添加测试
        include(GoogleTest)
        gtest_discover_tests(performance_test)
        
        message(STATUS "性能测试目标创建成功 (performance_test)")
    endif()
else()
    message(STATUS "性能测试已禁用 (BUILD_PERFORMANCE_TEST=OFF)")
endif()
