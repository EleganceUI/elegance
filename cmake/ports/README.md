# vcpkg overlay ports

Ports here replace vcpkg's own (vcpkg-configuration.json lists this folder), for fixes Elegance
needs before they're upstream, or add libraries vcpkg doesn't have. A replacement is a copy of the
vcpkg port at vcpkg.json's builtin-baseline, plus Elegance's patches.

- **accesskit-c**: AccessKit's C library (screen readers, design.md §12), which vcpkg has no port
  for. It installs the prebuilt libraries of AccessKit's release archive, or builds them from the
  archive's Rust sources with cargo where there are none (Linux on ARM64, which then needs Rust 1.87
  or later). On Linux it links the shared library from the prebuilt static one, as the prebuilt
  shared one needs glibc 2.39. Both its static and its shared library are installed whatever the
  triplet's linkage, as `accesskit-c::static` and `accesskit-c::shared`: Rust apps link the shared
  one (`ELG_ACCESSKIT_SHARED`). On Windows the static library is `accesskit-static.lib`, as the
  import library takes `accesskit.lib`. Not upstream: no vcpkg port exists. To update AccessKit,
  change the version and SHA512 in the port, or drop the overlay once vcpkg has a port.
- **dawn**: `010-vulkan-honor-alpha-mode.patch` makes Dawn's Vulkan backend create swapchains with
  the requested composite alpha mode instead of always opaque (dawn:286), so transparent windows
  (menus, tooltips, popovers) are transparent on Linux. When updating the baseline, copy vcpkg's
  new dawn port here and reapply the patch, or drop the overlay once Dawn does this itself.
- **sdl3**: `100-close-file-dialogs.patch` adds `SDL_CloseFileDialogs()`, which closes the file
  dialogs still open, each calling its callback as cancelled, and returns once they have (a dialog
  thread that doesn't end within 5 seconds is left to it): on Windows by closing the dialog windows
  of the threads showing them, on macOS by ending the sheets as cancelled, on Linux by closing the
  portal's requests or killing zenity. Elegance closes its dialogs when `App::run` returns
  (ELGSPEC-0018.R4); SDL has no way to close one. Not upstream. When updating the baseline, copy
  vcpkg's new sdl3 port here and reapply the patch, or drop the overlay once SDL can do this itself.
- **abseil**: a copy of vcpkg's port at vcpkg.json's builtin-baseline, with no change of Elegance's: on
  Mikros it builds the Mikros project's fork instead of the release (below). Drop the overlay once the
  fork is no longer needed there.

**Mikros** (the `x64-mikros` triplet, ELGSPEC-0040.R16): the abseil, dawn and sdl3 ports build the forks that
the Mikros project maintains, each pinned by commit and archive hash in its portfile, instead of the
upstream release. Elegance's patches above are commits of the forks, so the Mikros branch of dawn
leaves out `010` and the one of sdl3 applies no patch of its own. The vcpkg patches of the abseil and dawn
ports still apply to their forks. The sdl3 and dawn ports turn on what vcpkg's default features leave off for a
system it doesn't know: Wayland and Vulkan (sdl3 in its portfile, dawn through its features, whose platforms
name the systems vcpkg doesn't know).
To move to a new fork commit, change `REF` and set `SHA512` to `0`: vcpkg's error prints the right hash.
