// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_PROFILE_H
#define ELG_CORE_PROFILE_H

// Elegance's profiling macros (ELGSPEC-0042). In a profiling build (ELG_TRACY) they mark zones,
// frames, plots and thread names for the Tracy profiler; otherwise they compile to nothing. This is
// the only file that includes Tracy's header.
//
//   ELG_ZONE("Layout");                     a zone from here to the end of the scope
//   ELG_ZONE_TEXT(title.data(), title.size());  text for the zone of this scope
//   ELG_FRAME();                            ends a frame of the default frame set
//   ELG_FRAME_NAMED("Render");              ends a frame of a named frame set
//   ELG_PLOT("Draw commands", count);       records an integer
//   ELG_THREAD_NAME("Elegance render");     names the calling thread
//   ELG_MESSAGE(text.data(), text.size());  a message on the timeline
//   ELG_COUNT(counts.reused);               adds 1 to a counter that only plots read
//
// Names must be string literals: Tracy keeps their addresses. Arguments are evaluated in a
// profiling build only. Code that only feeds plots goes in #if defined(ELG_HAVE_TRACY), where
// ELG_PROFILER_CONNECTED() says whether to do it.

#if defined(ELG_HAVE_TRACY)

#include <tracy/Tracy.hpp> // IWYU pragma: export

#include <cstdint>

#define ELG_ZONE(name) ZoneScopedN(name)
#define ELG_ZONE_TEXT(text, size) ZoneText(text, size)
#define ELG_FRAME() FrameMark
#define ELG_FRAME_NAMED(name) FrameMarkNamed(name)
#define ELG_PLOT(name, value) TracyPlot(name, static_cast<std::int64_t>(value))
#define ELG_THREAD_NAME(name) ::tracy::SetThreadName(name)
#define ELG_MESSAGE(text, size) TracyMessage(text, size)
#define ELG_COUNT(counter) (++(counter))
#define ELG_PROFILER_CONNECTED() TracyIsConnected

#else

#define ELG_ZONE(name) ((void)0)
#define ELG_ZONE_TEXT(text, size) ((void)0)
#define ELG_FRAME() ((void)0)
#define ELG_FRAME_NAMED(name) ((void)0)
#define ELG_PLOT(name, value) ((void)0)
#define ELG_THREAD_NAME(name) ((void)0)
#define ELG_MESSAGE(text, size) ((void)0)
#define ELG_COUNT(counter) ((void)0)

#endif

#endif // ELG_CORE_PROFILE_H
