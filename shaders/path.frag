// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#version 450

// Path coverage: full coverage per sample; the MSAA resolve averages it into anti-aliased edges.
layout(location = 0) out vec4 out_coverage;

void main() {
    out_coverage = vec4(1.0);
}
