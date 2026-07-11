# EmilCAE

EmilCAE is a Windows-oriented CAE application skeleton built with C++20 and CMake. This first stage establishes compile-time module boundaries and two independent processes; it does not implement geometry, meshing, solving, or result visualization.

## Architecture

- `EmilCAE.Workbench.exe` is the future pre/post-processing application. It is currently a console placeholder.
- `EmilCAE.Solver.exe` is an independent, GUI-free solver process placeholder.
- `core` contains third-party-independent domain types.
- `preprocessor` and `postprocessor` define their respective application boundaries.
- `solver_interface` contains the minimal shared solver contract.

The Workbench never links a Solver implementation. The Solver depends only on Core and SolverInterface.

## Build on Windows

Requirements: Windows 11, Visual Studio 2026 with Desktop development with C++, and CMake 3.25 or newer.

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug --output-on-failure
```

Release builds use the corresponding `windows-msvc-release` preset.

## Current status

This repository contains architecture scaffolding only. No real CAE functionality or third-party CAE integration is present.
