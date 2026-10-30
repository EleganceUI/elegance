"""Dependency-free regression tests for the static prebuilt packaging contract.

Run with python3 c/test_packaging.py (requires CMake, Ninja and a native C++ compiler).
"""

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="elg-packaging-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.source = self.base / "source"
        self.source.mkdir()
        self.install = self.base / "installed"

    def run_cmake(self, *args, success=True):
        if "-S" in args:
            # Honor the developer environment's compiler/architecture, especially Windows ARM64.
            args = (*args, "-G", "Ninja")
        result = subprocess.run(
            ["cmake", *map(str, args)],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result.stdout

    def install_manifest(self, lines, success=True):
        manifest = self.base / "native-link.txt"
        manifest.write_text("\n".join(lines) + "\n")
        script = self.base / "install.cmake"
        template = (ROOT / "cmake/ElgInstallLinkManifest.cmake.in").read_text()
        script.write_text(
            template.replace("@ELG_PACKAGE_MANIFEST@", manifest.as_posix())
        )
        return self.run_cmake(
            f"-DCMAKE_INSTALL_PREFIX={self.install.as_posix()}",
            "-P",
            script,
            success=success,
        )

    def archives(self):
        result = []
        for name in ("libelegance_c.a", "libelegance.a", "libdependency.a"):
            path = self.source / name
            path.write_bytes(b"fixture")
            result.append(f"static {path.as_posix()}")
        return result

    def test_relocation_system_names_and_unknown_archives(self):
        lines = self.archives()
        lines[-1] = lines[-1].replace("static ", "lib ", 1)
        self.install_manifest(
            lines
            + [
                "lib m",
                "lib -framework Foundation",
                "lib /sdk/System/Library/Frameworks/AppKit.framework",
                "define ELG_STATIC_DEFINE",
                "lib",
                "define ",
            ]
        )
        text = (self.install / "elegance-link.txt").read_text()
        self.assertNotIn(self.base.as_posix(), text)
        self.assertIn("static lib/libdependency.a\n", text)
        self.assertIn("lib -framework AppKit\n", text)
        self.assertIn("lib m\n", text)
        self.assertTrue((self.install / "lib/libelegance_c.a").is_file())

    def test_system_shared_libraries_remain_system_names(self):
        self.install_manifest(
            self.archives()
            + [
                "lib /usr/lib/x86_64-linux-gnu/libm.so",
                "lib /lib/aarch64-linux-gnu/libdl.so.2",
                "lib /usr/lib64/libpthread.so.0",
                "runtime /lib64/libc.so.6",
            ]
        )
        text = (self.install / "elegance-link.txt").read_text()
        for name in ("m", "dl", "pthread"):
            self.assertIn(f"lib {name}\n", text)
        self.assertNotIn(".so", text)
        self.assertFalse((self.install / "lib/libc.so.6").exists())

    def test_non_system_shared_library_is_bundled(self):
        library = self.source / "libprivate.so"
        library.write_bytes(b"fixture")
        self.install_manifest(self.archives() + [f"lib {library.as_posix()}"])
        text = (self.install / "elegance-link.txt").read_text()
        self.assertIn("lib lib/libprivate.so\n", text)
        self.assertTrue((self.install / "lib/libprivate.so").exists())

    def test_missing_dependency_fails(self):
        output = self.install_manifest(
            self.archives() + ["static /missing/library.a"], success=False
        )
        self.assertIn("Missing or non-absolute package dependency", output)

    def test_missing_static_c_api_fails(self):
        output = self.install_manifest(self.archives()[1:], success=False)
        self.assertIn("Static package must contain", output)

    def test_unresolved_target_fails(self):
        output = self.install_manifest(
            self.archives() + ["lib Missing::Target"], success=False
        )
        self.assertIn("Unresolved package dependency", output)

    def test_optional_target_wrappers_resolve_transitive_archives(self):
        self.archives()
        for name in ("boost_date_time", "boost_transitive"):
            (self.source / f"lib{name}.a").write_bytes(b"fixture")
        (self.source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 4.3)
project(optional_target_fixture LANGUAGES NONE)
foreach (name IN ITEMS elegance_c elegance boost_date_time boost_transitive)
    add_library(${{name}} STATIC IMPORTED)
    set_property(TARGET ${{name}} PROPERTY IMPORTED_LOCATION "${{CMAKE_CURRENT_SOURCE_DIR}}/lib${{name}}.a")
endforeach ()
add_library(Boost::date_time ALIAS boost_date_time)
add_library(Boost::transitive ALIAS boost_transitive)
set_property(TARGET elegance_c PROPERTY INTERFACE_LINK_LIBRARIES elegance)
set_property(TARGET elegance PROPERTY INTERFACE_LINK_LIBRARIES
    "$<TARGET_NAME_IF_EXISTS:Boost::date_time>"
    "$<LINK_ONLY:$<TARGET_NAME_IF_EXISTS:Missing::direct>>"
    "$<BUILD_INTERFACE:$<LINK_ONLY:$<TARGET_NAME_IF_EXISTS:Boost::date_time>>>"
    "$<BUILD_LOCAL_INTERFACE:$<TARGET_NAME_IF_EXISTS:Boost::date_time>>"
    "$<INSTALL_INTERFACE:Missing::installed>")
set_property(TARGET boost_date_time PROPERTY INTERFACE_LINK_LIBRARIES
    "$<LINK_ONLY:$<TARGET_NAME_IF_EXISTS:Boost::transitive>>"
    "$<TARGET_NAME_IF_EXISTS:Missing::transitive>")
include("{ROOT.as_posix()}/cmake/ElgLinkManifest.cmake")
elg_write_link_manifest(elegance_c "${{CMAKE_CURRENT_BINARY_DIR}}/native.txt")
set(ELG_PACKAGE_MANIFEST "${{CMAKE_CURRENT_BINARY_DIR}}/native.txt")
configure_file("{ROOT.as_posix()}/cmake/ElgInstallLinkManifest.cmake.in" install.cmake @ONLY)
install(SCRIPT "${{CMAKE_CURRENT_BINARY_DIR}}/install.cmake")
''')
        build = self.base / "build"
        self.run_cmake("-S", self.source, "-B", build)
        self.run_cmake("--install", build, "--prefix", self.install)
        lines = (self.install / "elegance-link.txt").read_text().splitlines()
        archives = [line for line in lines if line.startswith("static ")]
        self.assertEqual(
            archives,
            [
                "static lib/libelegance_c.a",
                "static lib/libelegance.a",
                "static lib/libboost_date_time.a",
                "static lib/libboost_transitive.a",
            ],
        )
        self.assertFalse(any("::" in line or "$<" in line for line in lines))
        for line in archives:
            self.assertTrue((self.install / line.split(" ", 1)[1]).is_file())

    def test_colliding_names_fail(self):
        other = self.source / "other"
        other.mkdir()
        duplicate = other / "libdependency.a"
        duplicate.write_bytes(b"different archive")
        output = self.install_manifest(
            self.archives() + [f"static {duplicate.as_posix()}"], success=False
        )
        self.assertIn("colliding names", output)

    def test_linux_implicit_cxx_runtimes_are_explicit(self):
        (self.source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 4.3)
project(runtime_manifest LANGUAGES CXX)
add_library(dependency UNKNOWN IMPORTED)
set_property(TARGET dependency PROPERTY IMPORTED_LOCATION "${{CMAKE_CURRENT_SOURCE_DIR}}/dependency.a")
include("{ROOT.as_posix()}/cmake/ElgLinkManifest.cmake")
# Exercise both Linux runtime selections even when these tests run on macOS or Windows.
set(CMAKE_SYSTEM_NAME Linux)
set(APPLE FALSE)
set(CMAKE_CXX_STANDARD_LIBRARIES "")
set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES stdc++ m gcc_s gcc c)
elg_write_link_manifest(dependency "${{CMAKE_CURRENT_BINARY_DIR}}/libstdcxx.txt")
set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES c++ c++abi unwind m c)
elg_write_link_manifest(dependency "${{CMAKE_CURRENT_BINARY_DIR}}/libcxx.txt")
''')
        build = self.base / "build"
        self.run_cmake("-S", self.source, "-B", build)
        for filename, libraries in (
            ("libstdcxx.txt", ("stdc++", "m")),
            ("libcxx.txt", ("c++", "c++abi", "unwind", "m")),
        ):
            text = (build / filename).read_text()
            for library in libraries:
                self.assertIn(f"lib {library}\n", text)

    def configure_static_runtime_fixture(
        self, missing=None, libcxx=False, success=True
    ):
        self.archives()
        for name in ("libstdc++.a", "libgcc.a", "libgcc_eh.a"):
            if name != missing:
                (self.source / name).write_bytes(name.encode())
        compiler = self.source / "compiler.py"
        compiler.write_text(
            "import pathlib, sys\n"
            "name = sys.argv[-1].split('=', 1)[1]\n"
            "path = pathlib.Path(__file__).parent / name\n"
            "print(path.as_posix() if path.exists() else name)\n"
        )
        implicit = "c++ c++abi unwind m c" if libcxx else "stdc++ m gcc_s gcc c"
        (self.source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 4.3)
project(static_runtime_manifest LANGUAGES CXX)
add_library(elegance UNKNOWN IMPORTED)
set_property(TARGET elegance PROPERTY IMPORTED_LOCATION "${{CMAKE_CURRENT_SOURCE_DIR}}/libelegance.a")
add_library(elegance_c UNKNOWN IMPORTED)
set_property(TARGET elegance_c PROPERTY IMPORTED_LOCATION "${{CMAKE_CURRENT_SOURCE_DIR}}/libelegance_c.a")
set_property(TARGET elegance_c PROPERTY INTERFACE_LINK_LIBRARIES elegance)
include("{ROOT.as_posix()}/cmake/ElgLinkManifest.cmake")
set(CMAKE_SYSTEM_NAME Linux)
set(APPLE FALSE)
set(CMAKE_CXX_COMPILER "{Path(sys.executable).as_posix()}")
set(CMAKE_CXX_COMPILER_ARG1 "{compiler.as_posix()}")
set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_COMPILER_TARGET "")
set(CMAKE_SYSROOT "")
set(CMAKE_CXX_IMPLICIT_LINK_LIBRARIES {implicit})
elg_write_link_manifest(elegance_c "${{CMAKE_CURRENT_BINARY_DIR}}/native.txt" STATIC_CXX_RUNTIME)
set(ELG_PACKAGE_MANIFEST "${{CMAKE_CURRENT_BINARY_DIR}}/native.txt")
configure_file("{ROOT.as_posix()}/cmake/ElgInstallLinkManifest.cmake.in" install.cmake @ONLY)
install(SCRIPT "${{CMAKE_CURRENT_BINARY_DIR}}/install.cmake")
''')
        build = self.base / "build"
        output = self.run_cmake("-S", self.source, "-B", build, success=success)
        return build, output

    def test_static_linux_runtime_archives_are_packaged_in_order(self):
        build, _ = self.configure_static_runtime_fixture()
        self.run_cmake("--install", build, "--prefix", self.install)
        relocated = self.base / "relocated"
        self.install.rename(relocated)
        shutil.rmtree(self.source)
        shutil.rmtree(build)
        lines = (relocated / "elegance-link.txt").read_text().splitlines()
        expected = [
            "static lib/libstdc++.a",
            "static lib/libgcc.a",
            "static lib/libgcc_eh.a",
            "static lib/libgcc.a",
        ]
        start = lines.index(expected[0])
        self.assertEqual(lines[start : start + len(expected)], expected)
        self.assertGreater(start, lines.index("static lib/libelegance.a"))
        for line in expected:
            path = relocated / line.split(" ", 1)[1]
            self.assertEqual(path.read_bytes(), path.name.encode())
        self.assertNotIn("lib stdc++", lines)
        self.assertNotIn("lib gcc_s", lines)
        self.assertFalse(any(".so" in line for line in lines))

    def test_missing_compiler_runtime_archive_fails(self):
        _, output = self.configure_static_runtime_fixture(
            missing="libgcc_eh.a", success=False
        )
        self.assertIn("Compiler could not locate static runtime libgcc_eh.a", output)

    def test_linux_static_libcxx_toolchain_is_rejected(self):
        _, output = self.configure_static_runtime_fixture(libcxx=True, success=False)
        self.assertIn("prebuilts require libstdc++ and libgcc", output)

    def test_unknown_windows_static_md_import_is_an_archive(self):
        (self.source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 4.3)
project(unknown_import LANGUAGES CXX)
set(VCPKG_TARGET_TRIPLET x64-windows-static-md)
add_library(dependency UNKNOWN IMPORTED)
set_property(TARGET dependency PROPERTY IMPORTED_LOCATION "${{CMAKE_CURRENT_SOURCE_DIR}}/dependency.lib")
include("{ROOT.as_posix()}/cmake/ElgLinkManifest.cmake")
elg_write_link_manifest(dependency "${{CMAKE_CURRENT_BINARY_DIR}}/manifest.txt")
''')
        build = self.base / "build"
        self.run_cmake("-S", self.source, "-B", build)
        text = (build / "manifest.txt").read_text()
        self.assertIn(f"static {self.source.as_posix()}/dependency.lib\n", text)

    def test_generated_manifest_links_after_relocation(self):
        # Real static archives and a shared dependency exercise generation, runtime closure,
        # installation, link order, loader paths, and the same independent consumer as CI.
        (self.source / "runtime.cpp").write_text(
            "#ifdef _WIN32\n__declspec(dllexport)\n#endif\n"
            'const char* dependency() { return "fixture"; }\n'
        )
        (self.source / "elegance.cpp").write_text(
            "#include <string>\n"
            "extern const char* dependency();\n"
            "extern int sdl_value();\n"
            # Force C++ runtime references in a static archive, not only the shared library.
            "const char* version() { static std::string value(dependency()); return sdl_value() == 42 ? value.c_str() : nullptr; }\n"
        )
        (self.source / "shim.cpp").write_text(
            "extern const char* version();\n"
            'extern "C" const char* elg_version() { return version(); }\n'
        )
        (self.source / "sdl.cpp").write_text("int sdl_value() { return 42; }\n")
        dependencies = self.source / "src"
        dependencies.mkdir()
        (dependencies / "CMakeLists.txt").write_text(f'''
add_library(accesskit_mock SHARED ../runtime.cpp)
add_library(sdl_mock STATIC ../sdl.cpp)
# Match find_package(SDL3): both the import and its alias are private to src/.
add_library(SDL3_import INTERFACE IMPORTED)
set_property(TARGET SDL3_import PROPERTY INTERFACE_LINK_LIBRARIES sdl_mock)
add_library(SDL3::SDL3 ALIAS SDL3_import)
add_library(elegance STATIC ../elegance.cpp)
target_link_libraries(elegance PRIVATE
    "$<LINK_ONLY:$<TARGET_NAME_IF_EXISTS:SDL3::SDL3>>"
    "$<TARGET_NAME_IF_EXISTS:Missing::optional>"
    accesskit_mock)
include("{ROOT.as_posix()}/cmake/ElgLinkManifest.cmake")
cmake_language(DEFER CALL elg_write_link_manifest elegance_c "${{ELG_C_PACKAGE_MANIFEST}}" STATIC_CXX_RUNTIME)
''')
        (self.source / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 4.3)
project(package_fixture LANGUAGES CXX)
add_library(elegance_c STATIC shim.cpp)
target_link_libraries(elegance_c PRIVATE elegance)
set(ELG_C_PACKAGE_MANIFEST "${{CMAKE_CURRENT_BINARY_DIR}}/native-$<CONFIG>.txt")
add_subdirectory(src)
if (TARGET SDL3::SDL3)
    message(FATAL_ERROR "Fixture must exercise a directory-scoped import")
endif ()
set(ELG_PACKAGE_MANIFEST "${{CMAKE_CURRENT_BINARY_DIR}}/native-Release.txt")
configure_file("{ROOT.as_posix()}/cmake/ElgInstallLinkManifest.cmake.in" install.cmake @ONLY)
install(SCRIPT "${{CMAKE_CURRENT_BINARY_DIR}}/install.cmake")
''')
        build = self.base / "build"
        self.run_cmake("-S", self.source, "-B", build, "-DCMAKE_BUILD_TYPE=Release")
        self.run_cmake("--build", build, "--config", "Release")
        self.run_cmake(
            "--install", build, "--config", "Release", "--prefix", self.install
        )
        relocated = self.base / "relocated package"
        self.install.rename(relocated)
        manifest_text = (relocated / "elegance-link.txt").read_text()
        self.assertNotIn("SDL3::SDL3", manifest_text)
        self.assertNotIn("SDL3_import", manifest_text)
        self.assertRegex(manifest_text, r"static lib/(lib)?sdl_mock\.(a|lib)")
        # Remove source libraries to prove the consumer does not link or load them in place.
        shutil.rmtree(build)
        consumer = self.base / "consumer"
        self.run_cmake(
            "-S",
            ROOT / "c/smoke",
            "-B",
            consumer,
            f"-DELG_PACKAGE_ROOT={relocated.as_posix()}",
            "-DCMAKE_BUILD_TYPE=Release",
        )
        self.run_cmake("--build", consumer, "--config", "Release")
        result = subprocess.run(
            [
                "ctest",
                "--test-dir",
                str(consumer),
                "-C",
                "Release",
                "--output-on-failure",
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self.assertEqual(result.returncode, 0, result.stdout)

        if sys.platform in ("darwin", "linux"):
            manifest = relocated / "elegance-link.txt"
            text = manifest.read_text()
            if sys.platform == "linux":
                runtimes = {
                    "static lib/libstdc++.a",
                    "static lib/libgcc.a",
                    "static lib/libgcc_eh.a",
                }
                for line in runtimes:
                    self.assertIn(line, text.splitlines())
                    self.assertTrue((relocated / line.split(" ", 1)[1]).is_file())
                self.assertNotIn("lib stdc++\n", text)
                self.assertNotIn("lib gcc_s\n", text)
            else:
                runtimes = {"lib c++"}
                self.assertIn("lib c++", text.splitlines())
            manifest.write_text(
                "\n".join(line for line in text.splitlines() if line not in runtimes)
                + "\n"
            )
            missing_runtime = self.base / "consumer-without-cxx-runtime"
            self.run_cmake(
                "-S",
                ROOT / "c/smoke",
                "-B",
                missing_runtime,
                f"-DELG_PACKAGE_ROOT={relocated.as_posix()}",
                "-DCMAKE_BUILD_TYPE=Release",
            )
            output = self.run_cmake("--build", missing_runtime, success=False)
            self.assertIn("undefined", output.lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
