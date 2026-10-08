// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_ENVIRONMENT_H
#define ELG_ENVIRONMENT_H

#include <elg/color.h>
#include <elg/font.h>

namespace elg {

// Values inherited down the view tree (design.md §8): set with modifiers like .font() and
// .foreground_style(), read by the views below.
// M1: a fixed set of static values. M3 turns this into typed, reactive keys with themes.
struct EnvironmentValues {
    Font font = Font::body;
    Color foreground = Color::black; // text and unfilled shapes
    Color tint = Color::hex(0x0A84FF); // controls' accent color (buttons)
};

} // namespace elg

#endif // ELG_ENVIRONMENT_H
