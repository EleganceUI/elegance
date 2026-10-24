// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef DESIGN_CANVAS_H
#define DESIGN_CANVAS_H

#include "model.h"

#include <elg/elegence.h>

namespace design {

// The infinite canvas: the layers, selection and handles, and everything the pointer and
// keyboard do there (selecting, moving, resizing, drawing, panning, zooming).
elg::View CanvasView(const Document& doc, elg::State<elg::Size> viewport);

} // namespace design

#endif // DESIGN_CANVAS_H
