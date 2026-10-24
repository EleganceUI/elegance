// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef SHELL_MODEL_H
#define SHELL_MODEL_H

// The desktop's state: the session, open windows and their stacking, settings, notifications, and
// a clock. Everything is reactive (State), so the shell and the apps just read it.

#include <elg/elegance.h>

#include <ctime>
#include <map>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace shell {

enum class AppKind { settings, files, terminal, notes, calculator, monitor };

struct AppInfo {
    AppKind kind;
    std::string name;
    std::string icon;               // a Phosphor icon name
    std::vector<elg::Color> colors; // the tile's gradient
    elg::Size size;                 // a new window's size
};

const std::vector<AppInfo>& all_apps();
const AppInfo& app_info(AppKind kind);

// One window on the desktop. The States are shared, so copies refer to the same window.
struct Window {
    int id = 0;
    AppKind app = AppKind::settings;
    elg::State<std::string> title;
    elg::State<float> x;
    elg::State<float> y;
    elg::State<float> width;
    elg::State<float> height;
    elg::State<bool> minimized{false};
    elg::State<bool> maximized{false};
    std::string argument; // what it opened with (a file for Notes)
};

struct Toast {
    int id = 0;
    std::string title;
    std::string body;
    std::string icon;
    double expires = 0; // seconds (steady clock)
    friend bool operator==(const Toast& a, const Toast& b) { return a.id == b.id; }
};

struct Settings {
    elg::State<bool> dark{true};
    elg::State<int> accent{0};    // index into accents()
    elg::State<int> wallpaper{0}; // index into wallpapers()
    elg::State<bool> night_light{false};
    elg::State<float> night_strength{0.35f};
    elg::State<float> volume{0.6f};
    elg::State<bool> muted{false};
    elg::State<bool> wifi{true};
    elg::State<bool> bluetooth{true};
    elg::State<bool> do_not_disturb{false};
    elg::State<bool> notification_sounds{true};
    elg::State<std::string> user_name{"Alex Rivera"};
};

const std::vector<elg::Color>& accents();
struct Wallpaper {
    std::string name;
    std::vector<elg::Color> colors;
};
const std::vector<Wallpaper>& wallpapers();

// A text file in Files, opened by Notes.
struct Note {
    int id = 0;
    std::string title;
    std::string body;
    friend bool operator==(const Note&, const Note&) = default;
};

struct Desktop {
    elg::App* app = nullptr;
    Settings settings;

    // Session.
    elg::State<bool> locked{true};

    // Windows: what's open (in opening order) and the stacking order, back to front.
    elg::State<std::vector<int>> open_windows;
    elg::State<std::vector<int>> stack;
    std::shared_ptr<std::map<int, Window>> windows = std::make_shared<std::map<int, Window>>();
    std::shared_ptr<int> next_id = std::make_shared<int>(1);
    // The area windows live in (between the top bar and the dock), for maximizing and placing.
    // A guess until the first layout measures it.
    elg::State<elg::Size> area{elg::Size{1280, 720}};
    std::shared_ptr<bool> area_measured = std::make_shared<bool>(false);

    // Shell panels.
    elg::State<bool> launcher_open{false};
    elg::State<bool> quick_settings_open{false};
    elg::State<std::string> launcher_search;

    // Notifications: on screen now, and everything since signing in.
    elg::State<std::vector<Toast>> toasts;
    elg::State<std::vector<Toast>> history;

    // Notes, shared by Notes and Files.
    elg::State<std::vector<Note>> notes;

    // The time, ticking once a second; and a sample of fake CPU load per tick (System Monitor).
    elg::State<std::time_t> now{std::time(nullptr)};
    elg::State<std::vector<float>> cpu_history;

    Desktop();

    const Window& window(int id) const { return windows->at(id); }
    // The front window that isn't minimized, or 0.
    int focused() const;

    void open(AppKind app, std::string argument = {}) const;
    void close(int id) const;
    void focus(int id) const;
    void minimize(int id) const;
    void toggle_maximized(int id) const;
    // Dock click: open it, bring it forward, or minimize it if it's already in front.
    void activate_app(AppKind app) const;
    bool is_running(AppKind app) const;

    void notify(std::string title, std::string body, std::string icon = "bell") const;
    void sign_in() const;
    void lock() const;
    void sign_out() const;

    void tick() const; // once a second, on the UI thread
    // The window area was measured: windows opened before then are placed again.
    void set_area(elg::Size size) const;
};

// Posts Desktop::tick() once a second while it lives.
class Ticker {
public:
    explicit Ticker(Desktop desktop);

private:
    std::jthread thread_;
};

// Consolas (registered on first use), or the system font where it's missing.
elg::Font monospace(float size);

double seconds_now();
std::string format_time(std::time_t time, const char* format);

} // namespace shell

#endif // SHELL_MODEL_H
