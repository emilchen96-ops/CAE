# QTCAE

QTCAE 是面向 Windows 的计算机辅助工程仿真工作台，使用 C++20、CMake 和 Qt 6 Widgets 开发。

当前 Workbench 已完成 Qt 中文主窗口、OpenCASCADE 几何显示与选择、STEP/IGES/BREP 导入、Gmsh 四面体网格、HMASCII 网格导入导出、材料与实体截面管理，以及几何命名选择集。求解器功能尚未接入。

## 下载运行

普通用户应从仓库的
[Releases](https://github.com/emilchen96-ops/CAE/releases)
页面下载 `QTCAE-<版本>-windows-x64.zip`，解压后运行
`QTCAE.Workbench.exe`。GitHub 自动生成的 `Source code.zip`
只包含源码，不能直接运行。

首次发布前仍需确认项目许可证和第三方组件再分发条款，具体流程见
[`docs/发布说明.md`](docs/发布说明.md)。

## 工程结构

- `QTCAE.Workbench.exe`：Qt 6 Widgets 前后处理工作台。
- `EmilCAE.Solver.exe`：独立且不依赖 GUI 的求解器进程占位程序。
- `core`：不依赖第三方库的领域数据和管理逻辑。
- `preprocessor`、`postprocessor`：前处理和后处理模块边界。
- `solver_interface`：Workbench 与求解器之间的最小公共接口。

Workbench 不链接 Solver 实现。Solver 只依赖 Core 和 SolverInterface，不得引入 Qt、OpenCASCADE、Gmsh 或 VTK。

## Windows 开发环境

要求：Windows、Visual Studio 2026（安装“使用 C++ 的桌面开发”工作负载）、CMake 3.25 或更高版本、Qt 6、OpenCASCADE 7.8、VTK 9.4，以及 Gmsh C++ SDK。

Qt、OpenCASCADE 和 Gmsh 的本机安装位置通过 CMake 缓存、`CMAKE_PREFIX_PATH`、`OpenCASCADE_DIR` 和 `Gmsh_DIR` 配置，不要将个人电脑的绝对路径提交到公共 CMake 文件。Gmsh C++ SDK 必须使用与 QTCAE 一致的 MSVC 工具链和 Release 配置；MinGW C++ 二进制或链接 Debug CRT 的 Gmsh DLL 不能用于 Windows Release 包。

## 构建与测试

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

Release 构建使用对应的 `windows-msvc-release` preset。

## 本地生成发布包

先使用 `CMakeUserPresets.json` 或 CMake 缓存配置 Qt、OpenCASCADE、
VTK 和 Gmsh 的 Release 依赖路径，也可以使用脚本参数或
`QTCAE_QT6_DIR`、`QTCAE_OCCT_DIR`、`QTCAE_VTK_DIR`、
`QTCAE_GMSH_DIR` 环境变量，然后执行：

```powershell
.\scripts\package-windows.ps1
```

脚本会依次执行 Release 配置、编译、测试、安装、Debug DLL 与 Debug
CRT 依赖检查、独立启动冒烟测试和 ZIP 打包。生成的文件位于 `dist`，
不会进入 Git。

## 当前功能范围

- 几何对象和网格对象仅保存在当前内存会话中。
- 命名选择集使用几何对象 ID、拓扑类型和局部序号引用拓扑，不提供永久拓扑命名。
- 尚未实现载荷、约束、分析步、工程保存、INP 输出和求解器调用。
- 构建目录、可执行文件、动态库、调试符号和大型仿真结果不得提交到 Git。
