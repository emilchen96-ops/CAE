# EmilCAE contribution rules

- Preserve the documented module boundaries and do not introduce circular dependencies.
- Keep `EmilCAE_Core` independent of third-party libraries.
- Never add GUI, Qt, OpenCASCADE, Gmsh, or VTK dependencies to the Solver.
- Compile the project and run its tests after every change.
- Do not commit build directories, executables, DLLs, PDBs, or large simulation files.
- Never fabricate test results or simulation results.
