# QTCAE

QTCAE 是面向 Windows 的计算机辅助工程仿真工作台，使用 C++20、CMake 和 Qt 6 Widgets 开发。

当前 Workbench 已完成 Qt 中文主窗口、OpenCASCADE 几何显示与选择、STEP/IGES/BREP 导入、Gmsh 四面体网格、HMASCII 网格导入导出、材料与实体截面管理，以及几何命名选择集。求解器功能尚未接入。

## 工程结构

- `QTCAE.Workbench.exe`：Qt 6 Widgets 前后处理工作台。
- `EmilCAE.Solver.exe`：独立且不依赖 GUI 的求解器进程占位程序。
- `core`：不依赖第三方库的领域数据和管理逻辑。
- `preprocessor`、`postprocessor`：前处理和后处理模块边界。
- `solver_interface`：Workbench 与求解器之间的最小公共接口。

Workbench 不链接 Solver 实现。Solver 只依赖 Core 和 SolverInterface，不得引入 Qt、OpenCASCADE、Gmsh 或 VTK。

## Windows 开发环境

要求：Windows、Visual Studio 2026（安装“使用 C++ 的桌面开发”工作负载）、CMake 3.25 或更高版本、Qt 6、OpenCASCADE 7.8，以及 Gmsh C++ SDK。

Qt、OpenCASCADE 和 Gmsh 的本机安装位置通过 CMake 缓存、`CMAKE_PREFIX_PATH`、`OpenCASCADE_DIR` 和 `Gmsh_DIR` 配置，不要将个人电脑的绝对路径提交到公共 CMake 文件。

## 构建与测试

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

Release 构建使用对应的 `windows-msvc-release` preset。

## 当前功能范围

- 几何对象和网格对象仅保存在当前内存会话中。
- 命名选择集使用几何对象 ID、拓扑类型和局部序号引用拓扑，不提供永久拓扑命名。
- 尚未实现载荷、约束、分析步、工程保存、INP 输出和求解器调用。
- 构建目录、可执行文件、动态库、调试符号和大型仿真结果不得提交到 Git。
