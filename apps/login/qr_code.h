// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef LOGIN_QR_CODE_H
#define LOGIN_QR_CODE_H

#include <elg/elegance.h>

#include <string>

// `text` as a QR code (high error correction, so a logo can cover the middle), drawn with a
// Canvas: dark modules on a transparent background, scaled to whole points.
elg::View QrCode(const std::string& text, elg::Color color = elg::Color::black);

#endif // LOGIN_QR_CODE_H
