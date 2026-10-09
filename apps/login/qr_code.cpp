// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "qr_code.h"

#include <qrcodegen.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace elg;

View QrCode(const std::string& text, Color color) {
    // Encoded once, when the view is built; the canvas only draws.
    const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::HIGH);
    const int size = code.getSize();
    auto modules = std::make_shared<std::vector<bool>>(static_cast<std::size_t>(size * size));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            (*modules)[static_cast<std::size_t>(y * size + x)] = code.getModule(x, y);
        }
    }

    return Canvas([modules, size, color](GraphicsContext& context) {
        // Whole-point modules keep the code crisp; center what's left over.
        const Size area = context.size();
        const float module = std::max(1.f, std::floor(std::min(area.width, area.height) / static_cast<float>(size)));
        const float extent = module * static_cast<float>(size);
        const float left = std::floor((area.width - extent) / 2);
        const float top = std::floor((area.height - extent) / 2);
        for (int y = 0; y < size; ++y) {
            // Runs of dark modules in a row become one rect.
            int x = 0;
            while (x < size) {
                if (!(*modules)[static_cast<std::size_t>(y * size + x)]) {
                    ++x;
                    continue;
                }
                const int start = x;
                while (x < size && (*modules)[static_cast<std::size_t>(y * size + x)]) {
                    ++x;
                }
                context.fill_rect({left + static_cast<float>(start) * module, top + static_cast<float>(y) * module,
                                   static_cast<float>(x - start) * module, module},
                                  color);
            }
        }
    });
}
