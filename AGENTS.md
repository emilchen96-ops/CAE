# QTCAE 开发规则

- 保持已定义的模块边界，不得引入循环依赖。
- 保持 `EmilCAE_Core` 独立于第三方库。
- 不得向 Solver 引入 GUI、Qt、OpenCASCADE、Gmsh 或 VTK 依赖。
- 每次修改后都要编译工程并运行测试。
- 不得提交构建目录、可执行文件、DLL、PDB 或大型仿真文件。
- 不得伪造测试结果或仿真结果。
