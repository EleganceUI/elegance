// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "model.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <random>

namespace shell {

using namespace elg;

const std::vector<AppInfo>& all_apps() {
    static const std::vector<AppInfo> apps{
        {AppKind::settings, "Settings", "gear-six", {Color::hex(0x8E9AAF), Color::hex(0x4A5568)}, {860, 580}},
        {AppKind::files, "Files", "folder", {Color::hex(0x5AC8FA), Color::hex(0x2F80ED)}, {820, 520}},
        {AppKind::terminal, "Terminal", "terminal-window", {Color::hex(0x434343), Color::hex(0x111111)}, {700, 440}},
        {AppKind::notes, "Notes", "note-pencil", {Color::hex(0xFFD86F), Color::hex(0xF5A623)}, {760, 520}},
        {AppKind::calculator, "Calculator", "calculator", {Color::hex(0xFF9F6B), Color::hex(0xF2545B)}, {340, 540}},
        {AppKind::monitor, "System Monitor", "chart-line-up", {Color::hex(0x7BE495), Color::hex(0x1F9D55)}, {660, 600}},
    };
    return apps;
}

const AppInfo& app_info(AppKind kind) {
    return *std::ranges::find(all_apps(), kind, &AppInfo::kind);
}

const std::vector<Color>& accents() {
    static const std::vector<Color> colors{Color::hex(0x0A84FF), Color::hex(0xBF5AF2), Color::hex(0xFF375F),
                                           Color::hex(0xFF9F0A), Color::hex(0x30D158), Color::hex(0x64D2FF)};
    return colors;
}

const std::vector<Wallpaper>& wallpapers() {
    static const std::vector<Wallpaper> list{
        {"Aurora", {Color::hex(0x1B1F3B), Color::hex(0x3B2F63), Color::hex(0x2A6F97), Color::hex(0x52B69A)}},
        {"Sunset", {Color::hex(0x2B1055), Color::hex(0x7597DE), Color::hex(0xF6A6B2), Color::hex(0xFFD29D)}},
        {"Ocean", {Color::hex(0x0F2027), Color::hex(0x203A43), Color::hex(0x2C5364)}},
        {"Meadow", {Color::hex(0x134E5E), Color::hex(0x71B280)}},
        {"Ember", {Color::hex(0x240B36), Color::hex(0xC31432), Color::hex(0xF09819)}},
        {"Daylight", {Color::hex(0xE0EAFC), Color::hex(0xCFDEF3), Color::hex(0xA1C4FD)}},
    };
    return list;
}

Font monospace(float size) {
    static const bool registered = [] {
        const std::filesystem::path file = "C:/Windows/Fonts/consola.ttf";
        std::error_code error;
        if (!std::filesystem::exists(file, error)) {
            return false;
        }
        register_font("Consolas", file);
        return true;
    }();
    return registered ? Font::custom("Consolas", size) : Font::system(size);
}

double seconds_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string format_time(std::time_t time, const char* format) {
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[64];
    std::strftime(buffer, sizeof buffer, format, &local);
    return buffer;
}

Desktop::Desktop() {
    notes = std::vector<Note>{
        {1, "Welcome.txt",
         "Welcome to the Elegance desktop.\n\nEverything here is drawn by the Elegance UI library: the windows, the "
         "dock, the launcher, and every app.\n\nTry dragging windows by their title bars, resizing them from the "
         "corner, and changing the wallpaper in Settings."},
        {2, "Groceries.txt", "Oat milk\nCoffee beans\nLemons\nBasil\nSourdough"},
        {3, "Ideas.txt", "- A tiling mode for windows\n- Widgets on the desktop\n- Workspaces"},
    };
    // A minute of made-up history, so the graph starts full.
    std::vector<float> cpu(60);
    for (std::size_t i = 0; i < cpu.size(); ++i) {
        const double t = static_cast<double>(i);
        cpu[i] = static_cast<float>(0.2 + 0.1 * std::sin(t / 7) + 0.06 * std::sin(t * 1.7) + 0.04 * std::sin(t * 4.1));
    }
    cpu_history = cpu;
}

int Desktop::focused() const {
    const std::vector<int>& order = stack();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if (!windows->at(*it).minimized()) {
            return *it;
        }
    }
    return 0;
}

namespace {

// Where a new window goes: cascaded from the middle, so windows don't stack exactly on top of
// each other.
Rect placement(AppKind kind, int id, Size space) {
    const AppInfo& info = app_info(kind);
    const float step = static_cast<float>((id - 1) % 6) * 28.f;
    const float width = std::min(info.size.width, space.width - 40);
    const float height = std::min(info.size.height, space.height - 40);
    return {std::max(20.f, (space.width - width) / 2 - 80 + step), std::max(20.f, (space.height - height) / 2 - 60 + step),
            width, height};
}

} // namespace

void Desktop::set_area(Size size) const {
    const bool first = !*area_measured;
    *area_measured = true;
    batch([&] {
        area = size;
        if (first) {
            for (const auto& [id, w] : *windows) {
                const Rect r = placement(w.app, id, size);
                w.x = r.x;
                w.y = r.y;
                w.width = r.width;
                w.height = r.height;
            }
        }
    });
}

void Desktop::open(AppKind kind, std::string argument) const {
    const AppInfo& info = app_info(kind);
    const int id = (*next_id)++;
    const Rect r = placement(kind, id, area.peek());
    Window window{.id = id,
                  .app = kind,
                  .title = State<std::string>(info.name),
                  .x = State<float>(r.x),
                  .y = State<float>(r.y),
                  .width = State<float>(r.width),
                  .height = State<float>(r.height),
                  .argument = std::move(argument)};
    windows->emplace(id, window);
    with_animation(Animation::spring(0.35, 0.85), [&] {
        batch([&] {
            open_windows.update([&](std::vector<int>& list) { list.push_back(id); });
            stack.update([&](std::vector<int>& list) { list.push_back(id); });
            launcher_open = false;
        });
    });
}

void Desktop::close(int id) const {
    with_animation(Animation::ease_out(0.18), [&] {
        batch([&] {
            open_windows.update([&](std::vector<int>& list) { std::erase(list, id); });
            stack.update([&](std::vector<int>& list) { std::erase(list, id); });
        });
    });
    // The Window's States stay alive in the map until the close animation no longer needs them;
    // a demo can afford to keep them.
}

void Desktop::focus(int id) const {
    const Window& w = window(id);
    batch([&] {
        if (w.minimized.peek()) {
            with_animation(Animation::spring(0.35, 0.9), [&] { w.minimized = false; });
        }
        if (stack.peek().empty() || stack.peek().back() != id) {
            stack.update([&](std::vector<int>& list) {
                std::erase(list, id);
                list.push_back(id);
            });
        }
    });
}

void Desktop::minimize(int id) const {
    with_animation(Animation::spring(0.3, 1.0), [&] { window(id).minimized = true; });
}

void Desktop::toggle_maximized(int id) const {
    const Window& w = window(id);
    with_animation(Animation::spring(0.32, 0.9), [&] { w.maximized = !w.maximized.peek(); });
    focus(id);
}

bool Desktop::is_running(AppKind kind) const {
    return std::ranges::any_of(open_windows(), [&](int id) { return windows->at(id).app == kind; });
}

void Desktop::activate_app(AppKind kind) const {
    // The front-most window of that app, if any.
    const std::vector<int> order = stack.peek();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const Window& w = window(*it);
        if (w.app != kind) {
            continue;
        }
        if (*it == untrack([&] { return focused(); })) {
            minimize(*it);
        } else {
            focus(*it);
        }
        return;
    }
    open(kind);
}

void Desktop::notify(std::string title, std::string body, std::string icon) const {
    static int next = 1;
    Toast toast{next++, std::move(title), std::move(body), std::move(icon), seconds_now() + 5};
    history.update([&](std::vector<Toast>& list) { list.insert(list.begin(), toast); });
    if (settings.do_not_disturb.peek()) {
        return;
    }
    with_animation(Animation::spring(0.4, 0.8), [&] {
        toasts.update([&](std::vector<Toast>& list) { list.push_back(toast); });
    });
}

void Desktop::sign_in() const {
    with_animation(Animation::ease_in_out(0.45), [&] { locked = false; });
    notify("Welcome back, " + settings.user_name.peek().substr(0, settings.user_name.peek().find(' ')),
           "Open apps from the launcher in the corner, or double-click an icon on the desktop.", "hand-waving");
}

void Desktop::lock() const {
    with_animation(Animation::ease_in_out(0.4), [&] {
        batch([&] {
            locked = true;
            launcher_open = false;
            quick_settings_open = false;
        });
    });
}

void Desktop::sign_out() const {
    lock();
    batch([&] {
        open_windows = std::vector<int>{};
        stack = std::vector<int>{};
        toasts = std::vector<Toast>{};
    });
}

void Desktop::tick() const {
    const double t = seconds_now();
    batch([&] {
        now = std::time(nullptr);
        if (std::ranges::any_of(toasts.peek(), [&](const Toast& toast) { return toast.expires <= t; })) {
            with_animation(Animation::ease_out(0.3), [&] {
                toasts.update([&](std::vector<Toast>& list) {
                    std::erase_if(list, [&](const Toast& toast) { return toast.expires <= t; });
                });
            });
        }
        // Made-up CPU load: a slow wave, noise, and a bump per open window.
        static std::mt19937 random{42};
        std::uniform_real_distribution<float> noise(-0.08f, 0.08f);
        const float windows_load = 0.04f * static_cast<float>(open_windows.peek().size());
        const float load = std::clamp(0.18f + 0.1f * static_cast<float>(std::sin(t / 7)) + windows_load + noise(random),
                                      0.02f, 0.98f);
        cpu_history.update([&](std::vector<float>& list) {
            list.erase(list.begin());
            list.push_back(load);
        });
    });
}

Ticker::Ticker(Desktop desktop)
    : thread_([desktop](std::stop_token stop) {
          while (!stop.stop_requested()) {
              std::this_thread::sleep_for(std::chrono::seconds(1));
              if (!stop.stop_requested()) {
                  post_to_ui_thread([desktop] { desktop.tick(); });
              }
          }
      }) {}

} // namespace shell
