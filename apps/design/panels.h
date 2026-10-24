// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef DESIGN_PANELS_H
#define DESIGN_PANELS_H

#include "model.h"

#include <elg/elegence.h>

namespace design {

// The bar across the top: the document's name, undo/redo, zoom, share.
elg::View Toolbar(const Document& doc, elg::State<elg::Size> viewport);
// The tools, floating at the bottom of the canvas.
elg::View ToolPalette(const Document& doc);
// The layers list on the left.
elg::View LayersPanel(const Document& doc);
// The selection's properties on the right.
elg::View PropertiesPanel(const Document& doc);

} // namespace design

#endif // DESIGN_PANELS_H
