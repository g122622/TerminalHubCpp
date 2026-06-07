#pragma once

#include "terminalhub/Core/Result.hpp"
#include "terminalhub/Core/Types.hpp"

#include <string>

namespace th {

/**
 * @brief CLI 应用入口
 *
 * 使用 CLI11 解析命令行参数，执行对应命令。
 */
class CliApp {
public:
    /**
     * @brief 运行 CLI 应用
     * @param argc 参数个数
     * @param argv 参数数组
     * @return 退出码
     */
    static int run(int argc, char* argv[]);
};

} // namespace th
