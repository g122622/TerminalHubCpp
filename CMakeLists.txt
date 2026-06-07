cmake_minimum_required(VERSION 3.26)

# vcpkg 集成（必须在 project() 之前）
# 优先使用 VS 内置 vcpkg，其次使用独立 vcpkg
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
elseif(EXISTS "D:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake" AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "D:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
elseif(EXISTS "D:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake" AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "D:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

# 项目定义
project(TerminalHub
    VERSION 1.0.0
    DESCRIPTION "Terminal session manager - C++20 rewrite"
    LANGUAGES CXX
)

# C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 构建选项
option(TH_BUILD_TESTS "Build unit tests" ON)

if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

# CMake 模块
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# 编译配置
include(CompilerWarnings)

# ============================================================
# 依赖
# ============================================================

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# CLI11 通过 FetchContent 获取（header-only，无需编译）
include(FetchContent)
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(cli11)

# Windows API
if(WIN32)
    link_libraries(advapi32)  # 进程和注册表操作
    link_libraries(rpcrt4)    # UUID 生成 (UuidCreate)
endif()

# ============================================================
# 源文件
# ============================================================

set(CORE_SOURCES
    src/Core/Error.cpp
    src/Core/Logger.cpp
)

set(IPC_SOURCES
    src/IPC/IpcMessage.cpp
    src/IPC/IpcServer.cpp
    src/IPC/IpcClient.cpp
)

set(PTY_SOURCES
    src/PTY/ConPty.cpp
)

set(SESSION_SOURCES
    src/Session/Session.cpp
    src/Session/SessionManager.cpp
    src/Session/SessionRegistry.cpp
    src/Session/OutputBuffer.cpp
)

set(STORAGE_SOURCES
    src/Storage/ConfigManager.cpp
    src/Storage/JsonStorage.cpp
)

set(CLI_SOURCES
    src/CLI/CliApp.cpp
)

set(DAEMON_SOURCES
    src/Daemon/DaemonMain.cpp
)

set(ALL_SOURCES
    ${CORE_SOURCES}
    ${IPC_SOURCES}
    ${PTY_SOURCES}
    ${SESSION_SOURCES}
    ${STORAGE_SOURCES}
    ${CLI_SOURCES}
    ${DAEMON_SOURCES}
    src/Main.cpp
)

# ============================================================
# 可执行目标
# ============================================================

add_executable(th ${ALL_SOURCES})

# 禁用 MSVC 安全警告（getenv, sprintf 等）
if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_definitions(th PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_include_directories(th PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(th PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    CLI11::CLI11
)

target_compile_features(th PRIVATE cxx_std_20)

# Windows 子系统设置：支持控制台和 GUI
if(WIN32)
    set_target_properties(th PROPERTIES
        WIN32_EXECUTABLE FALSE
    )
endif()

# ============================================================
# 测试
# ============================================================

if(TH_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)

    # 辅助函数：添加测试
    function(add_th_test test_name)
        add_executable(${test_name} tests/${test_name}.cpp)
        target_include_directories(${test_name} PRIVATE
            ${CMAKE_SOURCE_DIR}/include
        )
        target_link_libraries(${test_name} PRIVATE
            nlohmann_json::nlohmann_json
            spdlog::spdlog
            CLI11::CLI11
            GTest::gtest
            GTest::gtest_main
        )
        target_compile_features(${test_name} PRIVATE cxx_std_20)
        # 禁用 MSVC 安全警告
        if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_definitions(${test_name} PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
        # 链接核心库和存储库源文件（测试需要）
        target_sources(${test_name} PRIVATE
            ${CORE_SOURCES}
            ${STORAGE_SOURCES}
            ${SESSION_SOURCES}
            ${IPC_SOURCES}
            ${PTY_SOURCES}
        )
        # Windows 特定链接
        if(WIN32)
            target_link_libraries(${test_name} PRIVATE rpcrt4)
        endif()
        add_test(NAME ${test_name} COMMAND ${test_name})
    endfunction()

    add_th_test(test_output_buffer)
    add_th_test(test_ipc_message)
    add_th_test(test_config_manager)
    add_th_test(test_json_storage)
    add_th_test(test_session)
endif()
