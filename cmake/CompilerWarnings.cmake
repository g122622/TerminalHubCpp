# Windows 特定的编译器警告配置
# 适用于 Clang 编译器

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Werror
        -pedantic

        # 禁用部分警告
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-private-field
        -Wno-unused-lambda-capture
        -Wno-defaulted-function-deleted
        -Wno-sign-conversion
        -Wno-implicit-int-conversion
        -Wno-implicit-float-conversion
        -Wno-enum-enum-conversion
        -Wno-float-conversion

        # 颜色输出
        -fcolor-diagnostics
    )

    # Release 构建优化
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(
            -O3
            -march=native
            -mtune=native
            -ffast-math
            -fno-finite-math-only
            -fno-trapping-math
            -flto=thin
        )
    endif()

    # RelWithDebInfo 优化
    if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_compile_options(
            -O2
            -g
            -flto=thin
        )
    endif()
endif()
