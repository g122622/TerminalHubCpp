# TerminalHubCpp

C++20 重写的终端会话管理器，功能与 Node.js 版 TerminalHub 完全一致。

## 构建

**重要：由于 VS 2026 (v18) 的 vcpkg 兼容性问题，必须使用 `scripts/configure.bat` 来构建，它会先调用 VsDevCmd 设置环境。**

```bash
# 首次配置 + 构建（推荐）
scripts\configure.bat build

# 仅配置
scripts\configure.bat

# 仅构建（已配置过，且 VsDevCmd 环境已生效）
cmake --build build --preset windows-clang-debug

# 运行单个测试
build\Debug\test_output_buffer.exe
build\Debug\test_ipc_message.exe
build\Debug\test_config_manager.exe
build\Debug\test_json_storage.exe

# 运行全部测试（需要 VsDevCmd 环境）
ctest --preset windows-clang-debug

# 完整清理重建
rm -rf build && scripts\configure.bat build
```

### 构建失败排查

如果遇到 `fatal error: 'cstdint' file not found` 等标准库找不到的错误，说明 VsDevCmd 环境未生效：
1. 确保通过 `scripts\configure.bat` 执行构建，而非直接运行 cmake
2. 如果增量构建出问题，执行 `rm -rf build && scripts\configure.bat build` 完整重建

## 技术栈

- C++20, Clang (MSVC 后端), CMake + Ninja, vcpkg
- Win32 ConPTY API (PTY), Win32 Named Pipe + IOCP (IPC)
- JSON over Named Pipe 协议 (nlohmann-json)
- CLI11 (CLI), spdlog (日志), Google Test (测试)
- Result<T> + TRY() 宏 (错误处理，无异常)

## 项目结构

```
include/terminalhub/   # 公共头文件
  Core/                # Result, Error, Types, Logger
  IPC/                 # IpcServer, IpcClient, IpcMessage
  PTY/                 # ConPty
  Session/             # Session, SessionManager, SessionRegistry, OutputBuffer
  Storage/             # ConfigManager, JsonStorage
  CLI/                 # CliApp
src/                   # 实现文件（与 include 结构对应）
  Daemon/              # 守护进程入口
  Main.cpp             # 程序入口（CLI/Daemon 模式切换）
tests/                 # 单元测试
```

## 代码规范

详见 `docs/CODE_CONVENTIONS.md`，核心规则：

- 文件：PascalCase.hpp / PascalCase.cpp
- 成员变量：m_ 前缀 + camelCase
- 私有方法：_ 前缀
- 命名空间：th（顶层），小写子命名空间
- 注释：简体中文
- 错误处理：Result<T> + TRY()，禁止异常
- 智能指针，禁止裸 new/delete
- 禁止 using namespace std
- 禁止过度防御性编程

## CLI 命令

- `th init` — 初始化配置
- `th new [title]` — 创建新会话
- `th list` — 列出活跃会话
- `th attach <id>` — 连接到会话
- `th kill <id>` — 终止会话
- `th rename <id> <title>` — 重命名会话

## 架构

守护进程模式（`TERMINALHUB_DAEMON=1`）通过 Named Pipe 监听 CLI 请求，管理 PTY 会话。
CLI 模式发送请求到守护进程并显示结果。attach 命令设置 raw mode 转发输入输出。
