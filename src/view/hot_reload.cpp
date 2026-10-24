// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Hot reload (design.md §9.6): a views module is loaded from a copy, its content is the single
// item of a ForEach keyed by the module's generation, so a reload unmounts the old tree (and
// disposes its scope) and mounts the new module's views, like any other ForEach change.

#include <elg/hot_reload.h>

#include "core/log.h"
#include "core/ui_tasks.h"
#include "platform/dynamic_library.h"

#include <elg/error.h>
#include <elg/reactive.h>
#include <elg/shapes.h>
#include <elg/text.h>
#include <elg/theme.h>
#include <elg/views.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#define elg_popen _popen
#define elg_pclose _pclose
#define elg_getpid _getpid
#else
#include <unistd.h>
#define elg_popen popen
#define elg_pclose pclose
#define elg_getpid getpid
#endif

namespace elg::detail {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

enum class Phase { idle, building, failed, reloaded };

struct Status {
    Phase phase = Phase::idle;
    std::string message;
    bool operator==(const Status&) const = default;
};

struct LoadedModule {
    std::unique_ptr<platform::DynamicLibrary> library;
    const ModuleEntry* entry = nullptr;
};

// Last-write times of every file under the watched paths.
std::map<fs::path, fs::file_time_type> scan(const std::vector<fs::path>& watch) {
    std::map<fs::path, fs::file_time_type> files;
    std::error_code error;
    for (const fs::path& root : watch) {
        if (fs::is_regular_file(root, error)) {
            files[root] = fs::last_write_time(root, error);
            continue;
        }
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end;
             it != end; it.increment(error)) {
            if (error) {
                break;
            }
            if (it->is_regular_file(error)) {
                files[it->path()] = it->last_write_time(error);
            }
        }
    }
    return files;
}

std::optional<fs::file_time_type> modified(const fs::path& file) {
    std::error_code error;
    const auto time = fs::last_write_time(file, error);
    return error ? std::nullopt : std::optional(time);
}

// Runs `command` through the shell; returns its exit code and output (stdout and stderr).
std::pair<int, std::string> run(const std::string& command) {
    std::string output;
#if defined(_WIN32)
    // cmd /c strips the outer quotes of a line that starts with one; keep the command's own.
    const std::string line = "\"" + command + " 2>&1\"";
#else
    const std::string line = command + " 2>&1";
#endif
    FILE* pipe = elg_popen(line.c_str(), "r");
    if (!pipe) {
        return {-1, "couldn't start the build command"};
    }
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    return {elg_pclose(pipe), std::move(output)};
}

// The first line of build output that reports an error, for the status badge.
std::string first_error(const std::string& output) {
    std::size_t start = 0;
    while (start < output.size()) {
        std::size_t end = output.find('\n', start);
        if (end == std::string::npos) {
            end = output.size();
        }
        const std::string_view line(output.data() + start, end - start);
        if (line.find("error") != std::string_view::npos) {
            constexpr std::size_t kMax = 140;
            std::string text(line.substr(0, kMax));
            while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) {
                text.pop_back();
            }
            return text;
        }
        start = end + 1;
    }
    return "the build failed (see the console)";
}

class ModuleHost : public std::enable_shared_from_this<ModuleHost> {
public:
    explicit ModuleHost(HotReloadOptions options) : options_(std::move(options)) {
        std::error_code error;
        copies_ = fs::temp_directory_path(error) / "elegance-reload" / std::to_string(elg_getpid());
        fs::remove_all(copies_, error);
        fs::create_directories(copies_, error);
    }

    ~ModuleHost() {
        watcher_.request_stop();
        if (watcher_.joinable()) {
            watcher_.join();
        }
        // Loaded modules stay loaded: elements built from them may still be torn down after this.
        for (LoadedModule& module : modules_) {
            [[maybe_unused]] auto* leaked = module.library.release();
        }
    }

    const HotReloadOptions& options() const noexcept { return options_; }

    void start() {
        if (!modified(options_.module)) {
            initial_build_ = !options_.build_command.empty();
            status_ = Status{Phase::failed, std::format("{} isn't built yet", options_.module.filename().string())};
        } else {
            load();
        }
        watcher_ = std::jthread([weak = weak_from_this(), options = options_, initial = initial_build_,
                                 loaded = loaded_time_](std::stop_token stop) {
            watch(stop, weak, options, initial, loaded);
        });
    }

    // UI thread: loads the module file (from a fresh copy) and shows its views.
    void load() {
        const std::optional<fs::file_time_type> time = modified(options_.module);
        if (!time) {
            status_ = Status{Phase::failed, std::format("{} is missing", options_.module.filename().string())};
            return;
        }
        const auto started = Clock::now();
        try {
            const fs::path copy =
                copies_ / std::format("{}-{}{}", options_.module.stem().string(), ++copies_made_,
                                      options_.module.extension().string());
            fs::copy_file(options_.module, copy, fs::copy_options::overwrite_existing);
            auto library = std::make_unique<platform::DynamicLibrary>(copy);
            using EntryFunction = const ModuleEntry* (*)();
            const auto entry_function = reinterpret_cast<EntryFunction>(library->symbol("elg_module_entry"));
            if (!entry_function) {
                throw ResourceError(std::format("{} has no ELG_MODULE(...)", options_.module.filename().string()));
            }
            const ModuleEntry* entry = entry_function();
            if (!entry || entry->abi != module_abi) {
                throw ResourceError(std::format("{} was built against another Elegance version",
                                                options_.module.filename().string()));
            }
            modules_.push_back({std::move(library), entry});
        } catch (const std::exception& error) {
            log_warning("hot reload: {}", error.what());
            status_ = Status{Phase::failed, error.what()};
            return;
        }
        loaded_time_ = *time;

        // Previews, copied: their names live in the module.
        std::vector<std::string> names;
        if (const std::vector<PreviewEntry>* previews = modules_.back().entry->previews) {
            for (const PreviewEntry& preview : *previews) {
                names.emplace_back(preview.name ? preview.name : "Preview");
            }
        }
        if (std::ranges::find(names, selected_.peek()) == names.end()) {
            selected_ = names.empty() ? std::string() : names.front();
        }
        preview_names_ = std::move(names);

        // Older modules than the previous one are unloaded: their views left long ago (the
        // previous module's may still be fading out).
        while (modules_.size() > 2) {
            modules_.erase(modules_.begin());
        }
        const bool first = generation_.peek() == 0;
        generation_ += 1;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        if (!first) {
            log_info("hot reload: {} reloaded", options_.module.filename().string());
            status_ = Status{Phase::reloaded, std::format("Reloaded ({} ms)", ms)};
            clear_status_at_ = Clock::now() + std::chrono::milliseconds(2500);
        } else {
            status_ = Status{};
        }
    }

    void set_status(Status status) { status_ = std::move(status); }
    void clear_status_if_due() {
        if (status_.peek().phase == Phase::reloaded && Clock::now() >= clear_status_at_) {
            status_ = Status{};
        }
    }

    // The current module's content: its root view, or the preview browser.
    View content() const {
        if (modules_.empty()) {
            return Text("Waiting for the views module…").foreground_style(ThemeColor::secondary);
        }
        const ModuleEntry& entry = *modules_.back().entry;
        if (options_.show == HotReloadShow::root) {
            if (!entry.root) {
                return Text("This module has no root view (ELG_MODULE(nullptr)); open it in the preview host")
                    .foreground_style(ThemeColor::secondary);
            }
            return entry.root();
        }
        return preview_browser();
    }

    View preview(const std::string& name) const {
        if (!modules_.empty() && modules_.back().entry->previews) {
            for (const PreviewEntry& preview : *modules_.back().entry->previews) {
                if (name == (preview.name ? preview.name : "Preview")) {
                    return preview.body();
                }
            }
        }
        return Text("No previews in this module; add ELG_PREVIEW(\"Name\") { ... }")
            .foreground_style(ThemeColor::secondary);
    }

    State<std::int64_t> generation_{0};
    State<Status> status_{Status{}};
    State<std::vector<std::string>> preview_names_{std::vector<std::string>()};
    State<std::string> selected_{std::string()};

private:
    View preview_browser() const {
        auto self = std::const_pointer_cast<ModuleHost>(shared_from_this());
        const View list = ScrollView(VStack(
            {.alignment = HorizontalAlignment::leading, .spacing = 2},
            ForEach([self] { return self->preview_names_(); },
                    [self](const std::string& name) {
                        return Text(name)
                            .line_limit(1)
                            .padding(EdgeInsets{.top = 6, .leading = 10, .bottom = 6, .trailing = 10})
                            .frame(FlexibleFrameOptions{.max_width = infinity, .alignment = Alignment::leading})
                            .background(RoundedRectangle(6).fill(ThemeColor::accent).opacity(
                                [self, name] { return self->selected_() == name ? 0.25f : 0.f; }))
                            .content_shape(Rectangle())
                            .on_tap_gesture([self, name] { self->selected_ = name; });
                    })));
        const View sidebar = VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                                    Text("Previews").font(Font::headline), list)
                                 .padding(14)
                                 .frame(FlexibleFrameOptions{.min_width = 200, .max_width = 200,
                                                             .max_height = infinity, .alignment = Alignment::top_leading})
                                 .background(Rectangle().fill(ThemeColor::surface));
        // The selected preview, rebuilt when the selection or the module changes.
        const View shown =
            ForEach([self] { return std::vector<std::string>{std::format("{}/{}", self->generation_(), self->selected_())}; },
                    [self](const std::string&) { return self->preview(self->selected_.peek()); });
        return HStack({.spacing = 0}, sidebar,
                      ZStack(shown).frame(FlexibleFrameOptions{.max_width = infinity, .max_height = infinity}));
    }

    // The watcher thread: rebuilds when sources change, and reloads when the module file changes.
    static void watch(std::stop_token stop, std::weak_ptr<ModuleHost> weak, HotReloadOptions options, bool build_now,
                      std::optional<fs::file_time_type> loaded) {
        const auto post = [weak](auto action) {
            post_ui_task([weak, action = std::move(action)] {
                if (const std::shared_ptr<ModuleHost> host = weak.lock()) {
                    action(*host);
                }
            });
        };
        const auto build = [&] {
            post([](ModuleHost& host) { host.set_status({Phase::building, "Building…"}); });
            const auto started = Clock::now();
            auto [code, output] = run(options.build_command);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
            if (code != 0) {
                log_warning("hot reload: the build failed ({} ms):\n{}", ms, output);
                post([message = first_error(output)](ModuleHost& host) { host.set_status({Phase::failed, message}); });
            } else {
                log_debug("hot reload: built in {} ms", ms);
            }
        };

        std::map<fs::path, fs::file_time_type> sources = scan(options.watch);
        std::optional<fs::file_time_type> pending; // a new module file, waiting to settle
        if (build_now) {
            build();
        }
        std::mutex mutex;
        std::condition_variable_any sleeper;
        while (!stop.stop_requested()) {
            {
                std::unique_lock lock(mutex);
                sleeper.wait_for(lock, stop, std::chrono::milliseconds(250), [] { return false; });
            }
            if (stop.stop_requested()) {
                break;
            }
            post([](ModuleHost& host) { host.clear_status_if_due(); });

            // Sources changed: rebuild (the new module file is picked up below).
            if (!options.build_command.empty()) {
                std::map<fs::path, fs::file_time_type> now = scan(options.watch);
                if (now != sources) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(150)); // editors save in steps
                    sources = scan(options.watch);
                    build();
                }
            }

            // The module file changed (our build, or an IDE's): reload once it has settled.
            const std::optional<fs::file_time_type> module_time = modified(options.module);
            if (module_time && module_time != loaded) {
                if (pending == module_time) {
                    loaded = module_time;
                    pending.reset();
                    post([](ModuleHost& host) { host.load(); });
                } else {
                    pending = module_time;
                }
            }
        }
    }

    HotReloadOptions options_;
    fs::path copies_;
    int copies_made_ = 0;
    std::vector<LoadedModule> modules_;
    std::optional<fs::file_time_type> loaded_time_;
    bool initial_build_ = false;
    Clock::time_point clear_status_at_{};
    std::jthread watcher_;
};

View StatusBadge(std::shared_ptr<ModuleHost> host) {
    const auto color = [host]() -> Color {
        switch (host->status_().phase) {
        case Phase::building: return Color::hex(0x8AADF4);
        case Phase::failed: return Color::hex(0xED8796);
        case Phase::reloaded: return Color::hex(0xA6DA95);
        case Phase::idle: break;
        }
        return Color::clear;
    };
    return HStack({.spacing = 8}, Circle().fill(color).frame(8, 8),
                  Text([host] { return host->status_().message; }).line_limit(2))
        .font(Font::callout)
        .padding(EdgeInsets{.top = 7, .leading = 12, .bottom = 7, .trailing = 14})
        .background(Capsule().fill(ThemeColor::surface_raised))
        .shadow({.color = Color::black.opacity(0.25f), .radius = 8, .y = 2})
        .frame(FlexibleFrameOptions{.max_width = 520, .alignment = Alignment::trailing})
        .opacity([host] { return host->status_().phase == Phase::idle ? 0.f : 1.f; })
        .allows_hit_testing(false)
        .padding(14);
}

} // namespace

} // namespace elg::detail

namespace elg {

View HotReload(HotReloadOptions options) {
    auto host = std::make_shared<detail::ModuleHost>(std::move(options));
    host->start();
    const View content = ForEach([host] { return std::vector<std::int64_t>{host->generation_()}; },
                                 [host](std::int64_t) { return host->content(); });
    return ZStack({.alignment = Alignment::bottom_trailing},
                  ZStack(content).frame(FlexibleFrameOptions{.max_width = infinity, .max_height = infinity}),
                  detail::StatusBadge(host));
}

} // namespace elg
