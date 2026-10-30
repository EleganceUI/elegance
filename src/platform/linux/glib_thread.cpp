// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#if defined(ELG_HAVE_GIO)

#include "platform/linux/glib_thread.h"

#include "core/log.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <thread>

namespace elg::detail::platform::linux_desktop {

namespace {

class GLibThread {
public:
    static GLibThread& instance() {
        static GLibThread thread;
        return thread;
    }

    void invoke(std::function<void(GDBusConnection*)> work) {
        auto* task = new Task{this, std::move(work)};
        g_main_context_invoke(context_, &GLibThread::run, task);
    }

    ~GLibThread() {
        g_main_loop_quit(loop_);
        if (thread_.joinable()) {
            thread_.join();
        }
        g_main_loop_unref(loop_);
        g_main_context_unref(context_);
    }

private:
    struct Task {
        GLibThread* self;
        std::function<void(GDBusConnection*)> work;
    };

    GLibThread() : context_(g_main_context_new()), loop_(g_main_loop_new(context_, FALSE)) {
        thread_ = std::thread([this] {
            // Signals subscribed to on this thread arrive in its context.
            g_main_context_push_thread_default(context_);
            GError* error = nullptr;
            bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
            if (!bus_) {
                log_warning("linux: no session bus: {}", error ? error->message : "unknown error");
                g_clear_error(&error);
            }
            g_main_loop_run(loop_);
            if (bus_) {
                g_object_unref(bus_);
            }
            g_main_context_pop_thread_default(context_);
        });
    }

    static gboolean run(gpointer data) {
        auto* task = static_cast<Task*>(data);
        task->work(task->self->bus_);
        delete task;
        return G_SOURCE_REMOVE;
    }

    GMainContext* context_;
    GMainLoop* loop_;
    std::thread thread_;
    GDBusConnection* bus_ = nullptr; // GLib thread
};

} // namespace

void on_glib_thread(std::function<void(GDBusConnection* bus)> work) {
    GLibThread::instance().invoke(std::move(work));
}

std::string app_id() {
    if (const char* hint = SDL_GetHint(SDL_HINT_APP_ID); hint && *hint) {
        return hint;
    }
    std::error_code error;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::string("elegance") : exe.filename().string();
}

} // namespace elg::detail::platform::linux_desktop

#endif
