# TerminalHub 代码规范 v1.0

## 1. 总则

### 1.1 规范目的

本规范旨在确保项目代码的**一致性**、**可维护性**、**可读性**和**安全性**，降低团队协作成本，提高代码质量。

### 1.2 八荣八耻

以瞎猜接口为耻，以认真查询为荣。
以模糊执行为耻，以寻求确认为荣。
以臆想业务为耻，以人类确认为荣。
以自造轮子为耻，以复用现有为荣。
以跳过验证为耻，以主动测试为荣。
以破坏架构为耻，以遵循规范为荣，
以假装理解为耻，以诚实无知为荣。
以盲目修改为耻，以谨慎重构为荣。

### 1.3 例外处理

如有特殊情况需要违反本规范，必须：
1. 立即停止，向用户询问，征得同意之后方可继续
2. 在代码中添加明确注释说明原因
3. 结束任务之后给用户明确反馈

### 1.4 有关防御性编程

不要过度防御性编程，这会导致代码臃肿、不易发现真正的 bug 和架构缺陷。只在必要的边界和不受信任的输入处进行防御性检查，其他地方可以假设前置条件已经满足。

---

## 2. C++ 语言特性规范

### 2.1 C++ 标准

强制使用 C++20 标准。

### 2.2 允许使用的 C++20 特性

| 特性 | 使用建议 | 示例 |
|------|----------|------|
| `auto` | ✅ 推荐 | `auto iter = vec.begin();` |
| 结构化绑定 | ✅ 推荐 | `auto [x, y] = getPosition();` |
| `std::optional` | ✅ 推荐 | `std::optional<Session> findSession();` |
| `std::variant` | ✅ 推荐 | `std::variant<A, B> result;` |
| `if constexpr` | ✅ 推荐 | 模板元编程 |
| `std::string_view` | ✅ 推荐 | 只读字符串参数 |
| `std::filesystem` | ✅ 推荐 | 文件操作 |
| `std::format` | ✅ 推荐 | 字符串格式化 |
| Concepts | ✅ 推荐 | 模板约束 |

### 2.3 禁止使用的特性

```cpp
// ❌ 禁止：C 风格强制转换
int* ptr = (int*)malloc(sizeof(int));

// ✅ 推荐：C++ 风格强制转换
auto* ptr = static_cast<int*>(malloc(sizeof(int)));

// ❌ 禁止：原始数组
int arr[10];

// ✅ 推荐：std::array 或 std::vector
std::array<int, 10> arr;

// ❌ 禁止：裸指针管理内存
int* ptr = new int(5);
delete ptr;

// ✅ 推荐：智能指针
auto ptr = std::make_unique<int>(5);

// ❌ 禁止：宏定义常量
#define MAX_SESSIONS 100

// ✅ 推荐：constexpr 常量
inline constexpr int MAX_SESSIONS = 100;

// ❌ 禁止：异常
try { ... } catch (...) { ... }

// ✅ 推荐：Result<T> + TRY()
Result<void> result = doSomething();
TRY(doSomething());
```

### 2.4 类设计规范

```cpp
class Session {
public:
    Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) noexcept;
    Session& operator=(Session&&) noexcept;
    ~Session();

private:
    std::string m_id;
    OutputBuffer m_outputBuffer;
    void _internalUpdate();  // 私有方法以 _ 前缀区分
};
```

### 2.5 函数设计规范

```cpp
// ✅ 必要参数在前，可选参数在后
Result<std::unique_ptr<Session>> createSession(const CreateSessionOptions& opts);

// ✅ 使用 string_view 传递只读字符串
void processCommand(std::string_view command);

// ✅ const 引用传递大对象
void handleRequest(const IpcRequest& request);

// ✅ [[nodiscard]] 标记必须检查返回值的函数
[[nodiscard]] Result<void> initialize();

// ✅ [[nodiscard]] 标记可能产生新资源的函数
[[nodiscard]] std::unique_ptr<ConPty> createPty(const ConPty::Options& opts);
```

---

## 3. 命名规范

### 3.1 文件命名

| 类型 | 规范 | 示例 |
|------|------|------|
| 头文件 | `PascalCase.hpp` | `SessionManager.hpp` |
| 源文件 | `PascalCase.cpp` | `SessionManager.cpp` |
| 测试文件 | `test_*.cpp` | `test_output_buffer.cpp` |

### 3.2 类型命名

```cpp
// 类/结构体：PascalCase
class SessionManager;
struct SessionMetadata;
enum class CommandType;

// 类型别名：PascalCase
using SessionId = std::string;

// 接口类以 I 开头
class IIpcTransport;
```

### 3.3 变量命名

```cpp
// 成员变量：m_ 前缀 + camelCase
class Session {
private:
    std::string m_id;
    i32 m_connectedClients;
};

// 局部变量：camelCase
void update() {
    i32 clientCount = 0;
}

// 全局常量：UPPER_SNAKE_CASE
inline constexpr i32 DEFAULT_BUFFER_LINES = 1000;

// 静态成员：s_ 前缀
class Logger {
private:
    static LogLevel s_level;
};

// 枚举值：PascalCase
enum class CommandType {
    List,
    New,
    Attach,
    Kill,
};
```

### 3.4 命名空间

```cpp
namespace th {
namespace ipc {
namespace detail {
}}}

// ❌ 禁止：using namespace std
using namespace std;  // ❌
```

匿名命名空间用于只在单个 .cpp 文件中使用的类、函数、全局变量。

---

## 4. 注释与文档规范

### 4.1 Doxygen 文档注释

只在头文件中使用 Doxygen 风格的文档注释。

### 4.2 行内注释

推荐使用简体中文行内注释解释逻辑、算法步骤、设计决策。

### 4.3 注释必须全部使用简体中文

### 4.4 禁止出现参考其他项目的注释

---

## 5. 内存管理规范

- 优先使用 `std::unique_ptr`
- 共享所有权使用 `std::shared_ptr`
- 使用 `std::make_unique<>` / `std::make_shared<>`
- 禁止裸 `new`/`delete`

---

## 6. 错误处理规范

使用 Result<T> + TRY() 宏替代异常：

```cpp
Result<void> initialize() {
    TRY(createPipes());
    TRY(startServer());
    return Result<void>::ok();
}

Result<Session*> findSession(std::string_view id) {
    auto it = m_sessions.find(std::string(id));
    if (it == m_sessions.end()) {
        return Result<Session*>::err(Error::sessionNotFound(id));
    }
    return Result<Session*>::ok(it->second.get());
}
```

---

## 7. include 路径规范

```cpp
// ✅ 推荐：使用模块相对路径
#include "terminalhub/Core/Result.hpp"

// ❌ 禁止：使用 ../ 跳转到上级目录
#include "../Core/Result.hpp"
```

---

## 8. 性能相关规范

```cpp
// ✅ 返回 const 引用
const std::string& getId() const { return m_id; }

// ✅ 使用 string_view
void processCommand(std::string_view command);

// ✅ 使用 emplace_back
sessions.emplace_back(args...);
```

---

## 9. Windows API 使用规范

- 使用 `UNICODE` 宏确保宽字符 API
- 使用 `std::wstring` 处理 Windows API 字符串
- 使用 RAII 包装 Windows 句柄（`std::unique_ptr` + 自定义 deleter）
- 错误处理使用 `GetLastError()` + `FormatMessageW()`
