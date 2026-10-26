# Elegance — notes for Claude

## Building (Windows, from the Bash tool)

The shell has no MSVC environment, so a bare `cmake --build` fails with
`Cannot open include file: 'cstdint'`. Load `vcvars64.bat` first, inside a `.bat` file run through `cmd`:

```bash
J="$CLAUDE_JOB_DIR/tmp"   # or any scratch dir
cat > "$J/build.bat" <<'EOF'
@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VS=%%i
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
cmake --build build\%1 --target %2
EOF
cmd //c "$(cygpath -w "$J/build.bat")" windows-debug sandbox 2>&1 | grep -E "error|warning|FAILED"
```

- Run it from the repository root. vcvars puts Visual Studio's CMake and Ninja on PATH.
- Build trees are `build/<preset>`: `windows-debug` from `CMakePresets.json`, or a per-machine preset from the
  gitignored `CMakeUserPresets.json` (check which `build/` folders exist). To configure from scratch:
  `cmake --preset <preset>`, then `cmake --build build/<preset>`, both inside the same vcvars `.bat`.
  `cmake/ElgBootstrap.cmake` finds or clones vcpkg, so `VCPKG_ROOT` is optional.
- Don't use `cmake-build-debug/`. That's CLion's classic-vcpkg profile, and `CMakeLists.txt` refuses to configure in it.
- Targets: `elegance` (the DLL), `sandbox`, `elegance-preview` (the hot reload host), and the tests. They output to
  `build/<preset>/bin/`.
- The examples and showcase apps live in the elegance-examples repository (`../elegance-examples`), one project per
  folder (`<name>-cpp`, `<name>-rs`). The docs read their sources from there.
- Running an exe that needs the toolchain DLLs works the same way: call vcvars64 in the `.bat`, then run the exe.
