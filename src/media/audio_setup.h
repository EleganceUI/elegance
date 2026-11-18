// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_AUDIO_SETUP_H
#define ELG_MEDIA_AUDIO_SETUP_H

#include <functional>

namespace elg::detail {

// Runs `work` on the thread that makes and tears down every miniaudio context, engine and device,
// and lists devices, and returns once it has (rethrowing what it threw). On Windows miniaudio sets
// COM up on the thread that makes a context, for as long as the context lives, and Elegance sets
// COM up on no thread it didn't start (ELGSPEC-0019.R1): this one is Elegance's, set up for it
// (core/threads.h). Any thread; on that one, `work` runs at once.
void on_audio_setup_thread(const std::function<void()>& work);

} // namespace elg::detail

#endif // ELG_MEDIA_AUDIO_SETUP_H
