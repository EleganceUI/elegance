// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_THREADS_H
#define ELG_CORE_THREADS_H

namespace elg::detail {

// The first thing a thread that Elegance starts makes, for as long as it runs: it sets the thread
// up for the system services Elegance's threads use, and undoes that as the thread ends (Windows:
// COM, in the multithreaded apartment, ELGSPEC-0019.R3). Only on such a thread: on any other, the
// app decides.
class ThreadSetup {
public:
    ThreadSetup() noexcept;
    ~ThreadSetup();
    ThreadSetup(const ThreadSetup&) = delete;
    ThreadSetup& operator=(const ThreadSetup&) = delete;
    ThreadSetup(ThreadSetup&&) = delete;
    ThreadSetup& operator=(ThreadSetup&&) = delete;

private:
    bool begun_;
};

} // namespace elg::detail

#endif // ELG_CORE_THREADS_H
