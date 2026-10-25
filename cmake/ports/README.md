# vcpkg overlay ports

Ports here replace vcpkg's own (vcpkg-configuration.json lists this folder), for fixes Elegance
needs before they're upstream. Each is a copy of the vcpkg port at vcpkg.json's builtin-baseline,
plus Elegance's patches.

- **dawn**: `010-vulkan-honor-alpha-mode.patch` makes Dawn's Vulkan backend create swapchains with
  the requested composite alpha mode instead of always opaque (dawn:286), so transparent windows
  (menus, tooltips, popovers) are transparent on Linux. When updating the baseline, copy vcpkg's
  new dawn port here and reapply the patch, or drop the overlay once Dawn does this itself.
