# Elegance — notes for Claude

## Building (Windows, from the Bash tool)

The shell has no MSVC environment, so a bare `cmake --build` fails with
`Cannot open include file: 'cstdint'`. Load `vcvars64.bat` first, inside a `.bat` file run through `cmd`:

```bash
J="$CLAUDE_JOB_DIR/tmp"   # or any scratch dir
cat > "$J/build.bat" <<'EOF'
@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=C:\Users\nepge\AppData\Local\Programs\CLion\bin\ninja\win\x64;%PATH%
"C:\Users\nepge\AppData\Local\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe" --build G:\Elegance\build\dev-debug --target %1
EOF
cmd //c "$(cygpath -w "$J/build.bat")" example_animation 2>&1 | grep -E "error|warning|FAILED"
```

- CMake and Ninja come from CLion's bundle, not PATH.
- Use the build tree `build/dev-debug` (preset `dev-debug` in `CMakeUserPresets.json`, vcpkg manifest mode).
  To configure from scratch: `cmake --preset dev-debug`, then `cmake --build --preset dev-debug`, both inside the same vcvars `.bat`.
- Don't use `cmake-build-debug/`. That's CLion's classic-vcpkg profile, and `CMakeLists.txt` refuses to configure in it.
- Example targets are named `example_<folder>` (e.g. `example_animation`). They output `build/dev-debug/bin/<folder>.exe`.
  Other targets include `elegance` (the DLL) and `sandbox`.
- Running an exe that needs the toolchain DLLs works the same way: call vcvars64 in the `.bat`, then run the exe.
