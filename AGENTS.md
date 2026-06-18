# 仓库工作说明

## 项目结构

- `source/` 是主要的 C++17 Nutrimatic 项目。项目由 `conanfile.py`
  配置，使用 Meson/Ninja 编译，产物位于 `build/`。
- `build_index/` 是用于下载、提取 Wikipedia 数据并制作索引的附属
  Python 3.10 项目。它不属于常规 C++ 构建流程，使用 `uv` 单独管理环境。
- `cgi_scripts/` 和 `web_static/` 包含 Web 界面的配套文件。

以下 C++ 命令均应在仓库根目录下的 MSYS2 MinGW64 终端中运行。工作区
路径可能包含空格，因此从 PowerShell 启动终端时必须正确引用路径。

## C++ 工具链要求

仓库中的所有 C++ 源码都必须在 64 位 MSYS2 MinGW 终端内，使用安装在
`C:\msys64\mingw64` 下的 GCC 工具链编译。不要从普通 PowerShell 或 cmd
环境直接编译，不要使用 MSVC，也不要混用 MSVC 与 MinGW 生成的对象文件或
库。除非通过 Conan 选项覆盖，OpenFST 应位于 `C:\msys64\usr\local`。

在仓库根目录下使用以下命令启动所需终端：

```powershell
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -mingw64
```

后续所有 C++ 命令都在这个终端中运行。首先激活仓库内的 MinGW Python
环境，并指定项目本地的 Conan 缓存：

```bash
source mingw-venv/bin/activate
export CONAN_HOME="$PWD/conan.tmp"
```

不要使用 `mise`：当前 MSYS2 环境不支持它。Conan 主机配置必须包含
`os=Windows`、`compiler=gcc`、`arch=x86_64` 和 `compiler.cppstd=17`。
编译前先检查配置：

```bash
conan profile show
```

如果 Conan 检测到 MSVC，立即停止，不要继续编译。

## 配置并完整编译 C++ 项目

安装或配置构建依赖，然后编译所有目标：

```bash
conan install . --build=missing --settings=build_type=Release
conan build .
```

可执行文件会写入 `build/`。`conan build` 会在必要时配置 Meson 并调用
Ninja。修改工具链、Conan 配置、Meson 选项或依赖后，应重新执行以上两条
命令，不要复用过期的构建元数据。

## 编译单个目标或单个 C++ 文件

日常增量开发应优先编译包含改动文件的最小 Ninja 目标。这会重新编译发生
变化的翻译单元，并完成目标所需的链接：

```bash
ninja -C build test-expr.exe
ninja -C build find-expr.exe
ninja -C build make-index.exe
```

不要直接调用 MSYS2 自带的 `meson compile`；它的版本可能与 Conan 配置
`build/` 时使用的 Meson 不一致。需要重新配置时使用 `conan build .`，让
Conan 选择配套的 Meson 和 Ninja 版本。

其他目标名定义在 `source/meson.build` 中，包括 `index`、`search`、
`expr`、`remove-markup`、`merge-indexes`、`dump-index`、`explore-index`、
`find-anagrams` 和 `find-phone-words`。

如果只需要编译一个翻译单元，先查看 `build/compile_commands.json`。其中的
命令是当前构建配置下包含目录、宏定义、警告选项、语言标准和对象文件路径的
权威来源。例如，使用以下命令查看 `test-expr.cpp` 的实际编译命令：

```bash
python -c 'import json; print(next(x for x in json.load(open("build/compile_commands.json")) if x["file"].endswith("test-expr.cpp"))["command"])'
```

也可以显式执行一次独立的 MinGW 单文件编译检查：

```bash
g++ -std=c++17 \
  -D_USE_MATH_DEFINES \
  -Isource \
  -I/c/msys64/usr/local/include \
  -c source/test-expr.cpp \
  -o build/test-expr.manual.obj
```

编译其他 `.cpp` 文件时，替换输入和输出路径，并从该文件对应的
`compile_commands.json` 条目复制额外参数。带 `-c` 的命令只生成对象文件，
不会链接可执行文件，也不能代替编译其所属的 Meson 目标。

## 测试 C++ 项目

C++ 回归测试入口是 `test-expr` 可执行文件。请在仓库根目录构建并运行它，
确保其临时索引文件生成在可预期的位置：

```bash
ninja -C build test-expr.exe
./build/test-expr.exe
```

测试成功时不输出内容，退出码为 0。C++ 改动至少需要编译其所属目标，并运行
`test-expr.exe`。如果改动影响共享静态库、构建配置或依赖，则应先执行完整的
`conan build .`，再运行测试。

## 附属 Python 索引制作工具

只有在修改或使用索引制作流程时才进入 `build_index/`。该项目要求原生
Windows Python 3.10（`>=3.10,<3.11`），不要使用 C++ 构建所用的
`mingw-venv` Python。退出 MSYS2 终端后，在普通 PowerShell 中同步锁定的
环境并运行测试：

```powershell
Push-Location '.\build_index'
try {
  uv sync --frozen
  uv run --frozen pytest
} finally {
  Pop-Location
}
```

仅修改 `build_index/` 时必须通过这套 pytest 测试。普通 `source/` 改动不
需要安装该 Python 项目的依赖，也不要求运行其测试。

## 完成前检查

报告工作完成前必须确认：

- 使用 MSYS2 MinGW 编译了所有受影响的 C++ 目标；
- C++ 改动已运行 `build/test-expr.exe`；
- `build_index/` 改动已运行 `uv run --frozen pytest`；
- 已运行 `git diff --check`，且没有修改无关的用户改动。
