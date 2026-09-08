# HWRun OS — C 编码规范（工程质量门禁配套）

> 本规范面向 `bus/`、`include/`、各插件、`tests/` 等**用户态 C 代码**。
> `linux-src/`（Linux 内核工作区，不入库）与 `kernel/` 模块遵循各自 kbuild/内核风格，不受本规范约束。

## 1. 格式：clang-format

- 仓库根 `.clang-format` 为唯一格式基线（LLVM 派生）：4 空格缩进、列宽 100、
  Attach 大括号、指针星号贴变量名（`char *p`）、include 顺序保持现状（不排序）。
- 提交前格式化单个文件：

  ```sh
  clang-format -i path/to/file.c
  ```

- 只校验、不改动（CI `static` job 同款）：

  ```sh
  clang-format --dry-run --Werror $(git ls-files '*.c' '*.h')
  ```

- 存量代码未全量格式化前，CI 格式门禁为提示性（`continue-on-error`）；
  收敛后由维护者收紧为硬门禁。**禁止未经评审对存量文件大范围批量格式化。**

## 2. 静态检查：clang-tidy

- 仓库根 `.clang-tidy`：启用 `bugprone-*`、`clang-analyzer-*`、`performance-*`，
  不启用 `readability-*`/`llvm-*` 命名类；`WarningsAsErrors` 为空（仅提示）。
- 生成编译数据库后跑全工程：

  ```sh
  cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  run-clang-tidy build/compile_commands.json   # 或 clang-tidy <file>.c -- <flags>
  ```

## 3. 错误码约定（负 errno）

- 成功唯一返回 `0`（`HWRUN_OK`）；失败一律返回**负 errno**（`-EINVAL`、`-ENOENT`…），
  语义与 libc 一致，可用 `strerror(-rc)` 转描述。
- 无标准 errno 对应的私有错误落在 `HW_EBASE` 私有域（见 `include/hwrun.h`），
  `HWRUN_E*` 宏是负 errno 别名，源码一律使用宏、禁止裸写负数。
- 新错误码先查 `include/hwrun.h` 是否已有等价项，勿重复造码。

## 4. 测试规范（CMocka + ctest）

- 单元测试：`tests/test_<子系统>.c`，用 CMocka（`assert_int_equal` 等），
  每文件一个 `int main(void)` 汇总 `run_tests`。
- 集成测试：`tests/test_proto.c` 通过 `dlopen` 加载各插件 `.so` 做真协议调用，
  其 `WORKING_DIRECTORY` 必须固定在 `tests/` 源目录（插件产物相对路径依赖）。
- 本地跑全部测试：

  ```sh
  ctest --test-dir build --output-on-failure
  ```

- 新增子系统功能时须同步补 `tests/` 单测；CI 的 `build-test`/`sanitize` 均执行 ctest。

## 5. 构建规范（CMake 为主，Makefile 兼容）

- 主轨道为 CMake（Linux/WSL，C11/gnu11）：

  ```sh
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j2
  ctest --test-dir build --output-on-failure
  ```

- 常用开关：`-DHWRUN_ENABLE_SANITIZER=address;undefined`（消毒器）、
  `-DHWRUN_WERROR=ON`、`-DHWRUN_BUILD_TESTS=OFF`、`-DHWRUN_ENABLE_COVERAGE=ON`。
- 迁移过渡期顶层 `Makefile` 仍需可构建（CI `build-make` 保留性验证）：
  `make`（bus + 插件）后 `make test`。改动构建结构时须保持双轨可用。
- `kernel/` 独立走 kbuild（`make -C kernel PROFILE=… LINUX_SRC=…`），不在 CI 范围。
