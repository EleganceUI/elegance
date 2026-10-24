// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Design: a Figma-style editor on an infinite canvas. Frames, rectangles, ellipses, lines and
// text; selecting, moving with snapping, resizing, panning and zooming; layers; a properties
// inspector with a color picker; alignment, arrangement, copy and paste, undo and redo.
// Uses only the public API.
//
//   design                 open the window, with a sample document
//   design --empty         start with an empty canvas
//   design --select <n>    start with the n-th layer selected (for screenshots)
//   design --smoke-test    render the first frame, then exit

#include "canvas.h"
#include "model.h"
#include "panels.h"

#include <elg/elegence.h>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

using namespace elg;

int main(int argc, char** argv) {
    bool smoke_test = false;
    bool empty = false;
    int select = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        smoke_test |= arg == "--smoke-test";
        empty |= arg == "--empty";
        if (arg == "--select" && i + 1 < argc) {
            select = std::atoi(argv[++i]);
        }
    }
    try {
        App app;
        const design::Document doc;
        app.window({.title = "Elegence Design", .width = 1440, .height = 900, .min_size = Size{900, 560}},
                   [&] {
                       if (!empty) {
                           doc.load_sample();
                       }
                       if (select > 0 && select <= static_cast<int>(doc.nodes.peek().size())) {
                           doc.selection = std::vector<int>{doc.nodes.peek()[static_cast<std::size_t>(select - 1)].id};
                       }
                       const State<Size> viewport(Size{0, 0});
                       // Fits the document once the canvas knows its size.
                       const auto fitted = std::make_shared<bool>(false);
                       effect([=] {
                           const Size size = viewport();
                           if (!*fitted && size.width > 0) {
                               *fitted = true;
                               untrack([&] { doc.zoom_to_fit(size); });
                           }
                       });
                       if (smoke_test) {
                           app.quit(); // still renders the first frame
                       }
                       return VStack({.spacing = 0}, design::Toolbar(doc, viewport),
                                     HStack({.spacing = 0}, design::LayersPanel(doc),
                                            ZStack({.alignment = Alignment::bottom},
                                                   design::CanvasView(doc, viewport).frame({.max_width = infinity, .max_height = infinity}),
                                                   design::ToolPalette(doc).padding(EdgeInsets{.bottom = 18})),
                                            design::PropertiesPanel(doc)))
                           .color_scheme(ColorScheme::dark)
                           .tint(Color::hex(0x0D99FF));
                   });
        return app.run();
    } catch (const elg::Error& e) {
        std::fprintf(stderr, "design: %s\n", e.what());
        return 1;
    }
}
