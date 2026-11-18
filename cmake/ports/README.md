# vcpkg overlay ports

Ports here replace vcpkg's own (vcpkg-configuration.json lists this folder), for fixes Elegance
needs before they're upstream. Each is a copy of the vcpkg port at vcpkg.json's builtin-baseline,
plus Elegance's patches.

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
