// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "media/audio_setup.h"

#include "core/profile.h"
#include "core/threads.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

namespace elg::detail {

namespace {

class AudioSetupThread {
public:
    // Never destroyed, nor its thread ended: static objects tear their devices down on it as the
    // process exits (the audio engine's).
    static AudioSetupThread& instance() {
        static auto* const setup = new AudioSetupThread();
        return *setup;
    }

    void run(const std::function<void()>& work) {
        if (std::this_thread::get_id() == thread_.get_id()) {
            work();
            return;
        }
        std::exception_ptr failure;
        bool done = false;
        std::unique_lock lock(mutex_);
        jobs_.emplace_back([&] {
            try {
                work();
            } catch (...) {
                failure = std::current_exception();
            }
            const std::lock_guard finished(mutex_);
            done = true;
        });
        wake_.notify_all();
        finished_.wait(lock, [&] { return done; });
        lock.unlock();
        if (failure) {
            std::rethrow_exception(failure);
        }
    }

    AudioSetupThread(const AudioSetupThread&) = delete;
    AudioSetupThread& operator=(const AudioSetupThread&) = delete;
    AudioSetupThread(AudioSetupThread&&) = delete;
    AudioSetupThread& operator=(AudioSetupThread&&) = delete;

private:
    AudioSetupThread() : thread_([this] { loop(); }) {}
    ~AudioSetupThread() = default;

    void loop() {
        ELG_THREAD_NAME("Elegance audio setup");
        const ThreadSetup thread_setup;
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return !jobs_.empty(); });
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            job();
            finished_.notify_all();
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable finished_;
    std::deque<std::function<void()>> jobs_;
    std::thread thread_; // last: starts once the rest exists
};

} // namespace

void on_audio_setup_thread(const std::function<void()>& work) {
    AudioSetupThread::instance().run(work);
}

} // namespace elg::detail
