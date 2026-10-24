// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The sample apps: Settings, Files, Terminal, Notes, Calculator, System Monitor.

#include "chrome.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>
#include <string>
#include <vector>

namespace shell {

using namespace elg;

namespace {

// --- Shared pieces ---

// Items left to right, wrapping onto new rows.
class WrapLayout final : public Layout {
public:
    explicit WrapLayout(float spacing = 10) : spacing_(spacing) {}

    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override {
        const float width = proposal.width && std::isfinite(*proposal.width) ? *proposal.width : 600.f;
        return {width, arrange(width, children, nullptr)};
    }
    void place_children(Rect bounds, ProposedSize, std::span<const LayoutChild> children) const override {
        arrange(bounds.width, children, &bounds);
    }

private:
    // Returns the height; places the children too when `bounds` is given.
    float arrange(float width, std::span<const LayoutChild> children, const Rect* bounds) const {
        float x = 0;
        float y = 0;
        float row = 0;
        for (const LayoutChild& child : children) {
            const Size size = child.size_that_fits({});
            if (x > 0 && x + size.width > width) {
                x = 0;
                y += row + spacing_;
                row = 0;
            }
            if (bounds) {
                child.place({bounds->x + x, bounds->y + y}, ProposedSize{size.width, size.height});
            }
            x += size.width + spacing_;
            row = std::max(row, size.height);
        }
        return y + row;
    }

    float spacing_;
};

View Heading(std::string text) {
    return Text(std::move(text)).font(Font::system(22, Weight::bold)).padding(EdgeInsets{.bottom = 4});
}

View SectionTitle(std::string text) {
    return Text(std::move(text))
        .font(Font::system(13, Weight::semibold))
        .foreground_style(ThemeColor::secondary)
        .padding(EdgeInsets{.top = 10});
}

// A rounded group of rows, as in system settings.
template <class... Rows>
View Card(Rows&&... rows) {
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, std::forward<Rows>(rows)...)
        .padding(16)
        .frame({.max_width = infinity, .alignment = Alignment::leading})
        .background(ThemeColor::surface, RoundedRectangle(10))
        .overlay(RoundedRectangle(10).stroke_border([](const EnvironmentValues& env) { return env.color(ThemeColor::outline); }));
}

View Row(std::string title, std::string detail, View control) {
    return HStack({.spacing = 12},
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 2},
                         Text(std::move(title)).font(Font::system(14)),
                         If(!detail.empty(), Text(detail).font(Font::system(12)).foreground_style(ThemeColor::secondary))),
                  Spacer(), std::move(control));
}

View ToggleRow(std::string title, std::string detail, State<bool> value) {
    return Row(std::move(title), std::move(detail), Toggle("", value.binding()).fixed_size());
}

View SidebarItem(std::string title, std::string icon, std::function<bool()> selected, std::function<void()> action) {
    return Button(HStack({.spacing = 10}, Image(std::move(icon)).foreground_style(ThemeColor::accent).frame({.width = 20}),
                         Text(std::move(title)).font(Font::system(14)), Spacer())
                      .padding(EdgeInsets::symmetric(10, 7)),
                  std::move(action))
        .button_style(HoverButtonStyle(7, std::move(selected)));
}

View Sidebar(View content) {
    return std::move(content)
        .padding(10)
        .frame({.min_width = 210, .max_width = 210, .max_height = infinity, .alignment = Alignment::top_leading})
        .background(ThemeColor::surface)
        .overlay(Rectangle().fill(ThemeColor::outline).frame({.min_width = 1, .max_width = 1, .max_height = infinity}), Alignment::trailing);
}

// --- Settings ---

struct SettingsPage {
    std::string title;
    std::string icon;
};

const std::vector<SettingsPage>& settings_pages() {
    static const std::vector<SettingsPage> pages{
        {"Appearance", "palette"}, {"Display", "monitor"},  {"Sound", "speaker-high"}, {"Network", "wifi-high"},
        {"Notifications", "bell"}, {"Users", "user-circle"}, {"About", "info"},
    };
    return pages;
}

View AccentSwatch(const Settings& s, int index) {
    const Color color = accents()[static_cast<std::size_t>(index)];
    return Circle()
        .fill(color)
        .frame(28, 28)
        .padding(3)
        .overlay(Circle().stroke_border([=](const EnvironmentValues& env) {
            return s.accent() == index ? env.color(ThemeColor::primary) : Color::clear;
        }, 2))
        .content_shape(Circle())
        .on_tap_gesture([=] { s.accent = index; })
        .accessibility_label("Accent color " + std::to_string(index + 1));
}

View WallpaperThumb(const Settings& s, int index) {
    const Wallpaper& paper = wallpapers()[static_cast<std::size_t>(index)];
    return VStack({.spacing = 6},
                  RoundedRectangle(8)
                      .fill(Gradient(paper.colors))
                      .frame(150, 90)
                      .overlay(RoundedRectangle(8).stroke_border([=](const EnvironmentValues& env) {
                          return s.wallpaper() == index ? env.color(ThemeColor::accent) : Color::clear;
                      }, 3)),
                  Text(paper.name).font(Font::system(12)))
        .content_shape(Rectangle())
        .on_tap_gesture([=] { s.wallpaper = index; });
}

View SettingsContent(const Desktop& d, int page) {
    const Settings s = d.settings;
    switch (page) {
    case 0: {
        std::vector<int> swatches(accents().size());
        std::vector<int> papers(wallpapers().size());
        for (std::size_t i = 0; i < swatches.size(); ++i) {
            swatches[i] = static_cast<int>(i);
        }
        for (std::size_t i = 0; i < papers.size(); ++i) {
            papers[i] = static_cast<int>(i);
        }
        return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, Heading("Appearance"),
                      Card(Row("Mode", "Light or dark, for the whole desktop",
                               Picker(s.dark.binding(), {{false, "Light"}, {true, "Dark"}}).frame({.width = 160})),
                           Row("Accent color", "Buttons, selection and highlights",
                               HStack({.spacing = 4}, ForEach(swatches, [=](int i) { return AccentSwatch(s, i); })))),
                      SectionTitle("Wallpaper"),
                      Card(WrapLayout(14)(ForEach(papers, [=](int i) { return WallpaperThumb(s, i); }))));
    }
    case 1:
        return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, Heading("Display"),
                      Card(ToggleRow("Night light", "Warmer colors, easier on the eyes at night", s.night_light),
                           Row("Strength", "", Slider(s.night_strength.binding(), {.min = 0.1f, .max = 1}).frame({.width = 220}))),
                      Card(Row("Resolution", "", Text("Follows the display").foreground_style(ThemeColor::secondary)),
                           Row("Refresh rate", "", Text("Synchronized to the display").foreground_style(ThemeColor::secondary))));
    case 2:
        return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, Heading("Sound"),
                      Card(Row("Volume", "", Slider(s.volume.binding()).frame({.width = 260})),
                           ToggleRow("Mute", "", s.muted),
                           ToggleRow("Notification sounds", "Play a sound when a notification arrives", s.notification_sounds)),
                      Card(Row("Output", "", Text("Speakers (Built-in)").foreground_style(ThemeColor::secondary)),
                           Row("Input", "", Text("Microphone Array").foreground_style(ThemeColor::secondary))));
    case 3: {
        const auto network = [](std::string name, std::string detail, bool connected) {
            return HStack({.spacing = 10}, Image("wifi-high").foreground_style(ThemeColor::accent),
                          VStack({.alignment = HorizontalAlignment::leading, .spacing = 1}, Text(std::move(name)),
                                 Text(std::move(detail)).font(Font::system(12)).foreground_style(ThemeColor::secondary)),
                          Spacer(),
                          If(connected, Text("Connected").font(Font::system(12)).foreground_style(ThemeColor::success),
                             Image("lock-simple").foreground_style(ThemeColor::secondary)));
        };
        return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, Heading("Network"),
                      Card(ToggleRow("Wi-Fi", "", s.wifi),
                           If([=] { return s.wifi(); },
                              VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, Divider(),
                                     network("Home-5G", "Secured, 5 GHz", true), network("Cafe Guest", "Secured", false),
                                     network("Neighbor's Network", "Secured", false)))),
                      Card(ToggleRow("Bluetooth", "", s.bluetooth),
                           If([=] { return s.bluetooth(); },
                              VStack({.alignment = HorizontalAlignment::leading, .spacing = 10}, Divider(),
                                     Row("Wireless Headphones", "Connected", Image("headphones").foreground_style(ThemeColor::accent)),
                                     Row("Keyboard", "Battery 64%", Image("keyboard").foreground_style(ThemeColor::accent))))));
    }
    case 4:
        return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, Heading("Notifications"),
                      Card(ToggleRow("Do not disturb", "Notifications go to the list without popping up", s.do_not_disturb),
                           Row("Try it", "", Button("Send a test notification", [=] {
                                                d.notify("Test notification", "This is what notifications look like.", "bell-ringing");
                                            }))));
    case 5:
        return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, Heading("Users"),
                      Card(HStack({.spacing = 16},
                                  Text([=] {
                                      std::string initials;
                                      const std::string& name = s.user_name();
                                      for (std::size_t i = 0; i < name.size(); ++i) {
                                          if ((i == 0 || name[i - 1] == ' ') && name[i] != ' ' && initials.size() < 2) {
                                              initials += name[i];
                                          }
                                      }
                                      return initials;
                                  })
                                      .font(Font::system(24, Weight::semibold))
                                      .foreground_style(Color::white)
                                      .frame(64, 64)
                                      .background(ThemeColor::accent, Circle()),
                                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 4},
                                         Text([=] { return s.user_name(); }).font(Font::system(18, Weight::semibold)),
                                         Text("Administrator").foreground_style(ThemeColor::secondary))),
                           Row("Name", "Shown on the login screen and in the launcher",
                               TextField("Your name", s.user_name.binding()).frame({.width = 220}))));
    default:
        return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12}, Heading("About"),
                      Card(HStack({.spacing = 16},
                                  Image("squares-four.fill")
                                      .font(Font::system(34))
                                      .foreground_style(Color::white)
                                      .frame(64, 64)
                                      .background(ShapeStyle(Gradient({Color::hex(0x5E5CE6), Color::hex(0x0A84FF)})),
                                                  RoundedRectangle(16)),
                                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 4},
                                         Text("Elegance Desktop").font(Font::system(20, Weight::bold)),
                                         Text("A desktop environment demo, drawn entirely with the Elegance UI library.")
                                             .foreground_style(ThemeColor::secondary)))),
                      Card(Row("Elegance", "", Text(std::string("Version ") + elg::version()).foreground_style(ThemeColor::secondary)),
                           Row("Renderer", "", Text("WebGPU").foreground_style(ThemeColor::secondary)),
                           Row("Windows open", "",
                               Text([=] { return std::to_string(d.open_windows().size()); }).foreground_style(ThemeColor::secondary))));
    }
}

View SettingsApp(const Desktop& d) {
    const State<int> page(0);
    std::vector<int> indices(settings_pages().size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = static_cast<int>(i);
    }
    return HStack({.spacing = 0},
                  Sidebar(VStack({.alignment = HorizontalAlignment::leading, .spacing = 2}, ForEach(indices, [=](int i) {
                      const SettingsPage& p = settings_pages()[static_cast<std::size_t>(i)];
                      return SidebarItem(p.title, p.icon, [=] { return page() == i; }, [=] { page = i; });
                  }))),
                  ForEach([=] { return std::vector<int>{page()}; },
                          [=](int p) {
                              return ScrollView(SettingsContent(d, p).padding(24).frame(
                                  {.max_width = infinity, .alignment = Alignment::top_leading}));
                          }));
}

// --- Files ---

struct FileItem {
    std::string name;
    std::string icon;
    std::string detail;
    std::string folder;  // set for folders: where it leads
    int note = 0;        // set for text files: the note it opens
    friend bool operator==(const FileItem&, const FileItem&) = default;
};

std::vector<FileItem> folder_items(const Desktop& d, const std::string& path) {
    if (path == "Home") {
        return {{"Documents", "folder", "Folder", "Documents"}, {"Pictures", "folder", "Folder", "Pictures"},
                {"Music", "folder", "Folder", "Music"},         {"Downloads", "folder", "Folder", "Downloads"},
                {"todo.txt", "file-text", "1 KB", "", 2}};
    }
    if (path == "Documents") {
        std::vector<FileItem> items;
        for (const Note& note : d.notes()) {
            items.push_back({note.title, "file-text", std::format("{} B", note.body.size()), "", note.id});
        }
        items.push_back({"Budget.xlsx", "file-xls", "24 KB"});
        items.push_back({"Report.pdf", "file-pdf", "1.2 MB"});
        return items;
    }
    if (path == "Pictures") {
        return {{"Vacation", "folder", "Folder", "Vacation"}, {"Sunset.jpg", "file-image", "3.4 MB"},
                {"Cat.png", "file-image", "820 KB"}, {"Screenshot.png", "file-image", "1.1 MB"}};
    }
    if (path == "Vacation") {
        return {{"Beach.jpg", "file-image", "4.0 MB"}, {"Mountains.jpg", "file-image", "3.8 MB"}};
    }
    if (path == "Music") {
        return {{"Grid Runner.flac", "file-audio", "32 MB"}, {"Paper Moons.mp3", "file-audio", "8 MB"}};
    }
    if (path == "Downloads") {
        return {{"elegance-0.1.zip", "file-zip", "18 MB"}, {"invoice.pdf", "file-pdf", "96 KB"}};
    }
    return {};
}

View FileTile(const Desktop& d, const FileItem& item, State<std::vector<std::string>> path, State<std::string> selected) {
    const bool folder = !item.folder.empty();
    const std::string name = item.name;
    const auto open = [=] {
        if (folder) {
            path.update([&](std::vector<std::string>& p) { p.push_back(item.folder); });
        } else if (item.note != 0) {
            d.open(AppKind::notes, std::to_string(item.note));
        } else {
            d.notify("Can't open " + name, "This demo only opens text files and folders.", "warning-circle");
        }
    };
    return VStack({.spacing = 6},
                  Image(item.icon + ".fill")
                      .font(Font::system(42))
                      .foreground_style([=](const EnvironmentValues& env) {
                          return folder ? Color::hex(0x5AC8FA) : env.color(ThemeColor::secondary);
                      }),
                  Text(item.name).font(Font::system(12)).line_limit(2),
                  Text(item.detail).font(Font::system(11)).foreground_style(ThemeColor::tertiary))
        .padding(8)
        .frame({.width = 112})
        .background([=](const EnvironmentValues& env) {
            return selected() == name ? ShapeStyle(env.color(ThemeColor::accent).opacity(0.2f)) : ShapeStyle(Color::clear);
        }, RoundedRectangle(8))
        .content_shape(Rectangle())
        .on_tap_gesture(open, 2)
        .on_tap_gesture([=] { selected = name; })
        .accessibility_element()
        .accessibility_action(open);
}

View FilesApp(const Desktop& d) {
    const State<std::vector<std::string>> path(std::vector<std::string>{"Home"});
    const State<std::string> selected;
    const auto place = [=](std::string name, std::string icon) {
        return SidebarItem(name, std::move(icon), [=] { return path().size() == 1 && path().front() == name; },
                           [=] { path = std::vector<std::string>{name}; });
    };
    const View toolbar =
        HStack({.spacing = 6},
               Button(Image("caret-left").padding(6),
                      [=] {
                          if (path.peek().size() > 1) {
                              path.update([](std::vector<std::string>& p) { p.pop_back(); });
                          }
                      })
                   .button_style(HoverButtonStyle(6))
                   .disabled([=] { return path().size() <= 1; })
                   .help("Back"),
               Text([=] {
                   std::string text;
                   for (const std::string& part : path()) {
                       text += (text.empty() ? "" : "  /  ") + part;
                   }
                   return text;
               }).font(Font::system(13, Weight::medium)),
               Spacer(),
               Text([=] { return std::format("{} items", folder_items(d, path().back()).size()); })
                   .font(Font::system(12))
                   .foreground_style(ThemeColor::secondary))
            .padding(EdgeInsets::symmetric(12, 8))
            .overlay(Rectangle().fill(ThemeColor::outline).frame({.max_width = infinity, .min_height = 1, .max_height = 1}), Alignment::bottom);
    const View grid = ForEach([=] { return std::vector<std::string>{path().back()}; }, [=](const std::string& folder) {
        return ScrollView(WrapLayout(8)(ForEach([=] { return folder_items(d, folder); }, &FileItem::name,
                                                [=](const FileItem& item) { return FileTile(d, item, path, selected); }))
                              .padding(14)
                              .frame({.max_width = infinity, .alignment = Alignment::top_leading}));
    });
    return HStack({.spacing = 0},
                  Sidebar(VStack({.alignment = HorizontalAlignment::leading, .spacing = 2}, place("Home", "house"),
                                 place("Documents", "file-text"), place("Pictures", "image"), place("Music", "music-notes"),
                                 place("Downloads", "download-simple"))),
                  VStack({.spacing = 0}, toolbar, grid));
}

// --- Terminal ---

std::vector<std::string> split_words(const std::string& line) {
    std::vector<std::string> words;
    std::istringstream stream(line);
    for (std::string word; stream >> word;) {
        words.push_back(word);
    }
    return words;
}

struct TerminalLine {
    std::size_t index;
    std::string text;
    friend bool operator==(const TerminalLine&, const TerminalLine&) = default;
};

View TerminalApp(const Desktop& d, const Window& w) {
    const State<std::vector<std::string>> lines(std::vector<std::string>{
        "Elegance Shell 0.1 - type 'help' for commands.", ""});
    const State<std::string> input;
    const State<bool> focused(true);
    const Settings s = d.settings;
    const auto prompt = [=] {
        std::string user = s.user_name.peek().substr(0, s.user_name.peek().find(' '));
        std::ranges::transform(user, user.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return user + "@elegance:~$ ";
    };
    const auto run = [=] {
        const std::string line = input.peek();
        std::vector<std::string> out{prompt() + line};
        const std::vector<std::string> words = split_words(line);
        const std::string command = words.empty() ? "" : words.front();
        std::string rest = line.size() > command.size() ? line.substr(line.find(command) + command.size()) : "";
        rest.erase(0, rest.find_first_not_of(' '));
        if (command.empty()) {
        } else if (command == "help") {
            out.insert(out.end(), {"  help              this list", "  ls                list files", "  date              the time",
                                   "  whoami            who you are", "  echo <text>       print text",
                                   "  open <app>        settings, files, notes, calculator, monitor",
                                   "  notify <text>     send a notification", "  theme light|dark  switch the theme",
                                   "  neofetch          system info", "  clear             clear the screen",
                                   "  exit              close this window"});
        } else if (command == "ls") {
            out.push_back("Documents  Downloads  Music  Pictures  todo.txt");
        } else if (command == "date") {
            out.push_back(format_time(std::time(nullptr), "%A %d %B %Y, %H:%M:%S"));
        } else if (command == "whoami") {
            out.push_back(s.user_name.peek());
        } else if (command == "echo") {
            out.push_back(rest);
        } else if (command == "clear") {
            batch([&] {
                lines = std::vector<std::string>{};
                input = std::string();
            });
            return;
        } else if (command == "exit") {
            d.close(w.id);
            return;
        } else if (command == "notify") {
            d.notify("Terminal", rest.empty() ? "Hello from the terminal!" : rest, "terminal-window");
        } else if (command == "theme") {
            if (rest == "light" || rest == "dark") {
                s.dark = rest == "dark";
                out.push_back("Theme set to " + rest + ".");
            } else {
                out.push_back("usage: theme light|dark");
            }
        } else if (command == "open") {
            bool found = false;
            for (const AppInfo& info : all_apps()) {
                std::string name = info.name;
                std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (!rest.empty() && name.starts_with(rest)) {
                    d.open(info.kind);
                    out.push_back("Opened " + info.name + ".");
                    found = true;
                    break;
                }
            }
            if (!found) {
                out.push_back("open: no app called '" + rest + "'");
            }
        } else if (command == "neofetch") {
            out.insert(out.end(), {"   ____   " + prompt().substr(0, prompt().size() - 4), "  / __/   ---------------",
                                   " / _/     OS: Elegance Desktop (demo)",
                                   "/___/     UI: Elegance " + std::string(elg::version()),
                                   "          Renderer: WebGPU",
                                   "          Windows: " + std::to_string(d.open_windows.peek().size()),
                                   "          Theme: " + std::string(s.dark.peek() ? "dark" : "light")});
        } else {
            out.push_back(command + ": command not found");
        }
        batch([&] {
            lines.update([&](std::vector<std::string>& all) {
                all.insert(all.end(), out.begin(), out.end());
                all.push_back("");
            });
            input = std::string();
        });
    };
    const Font mono = monospace(14);
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 0},
                  ScrollView({.sticks_to_end = true},
                             VStack({.alignment = HorizontalAlignment::leading, .spacing = 2},
                                    ForEach(
                                        [=] {
                                            std::vector<TerminalLine> result;
                                            const std::vector<std::string>& all = lines();
                                            for (std::size_t i = 0; i < all.size(); ++i) {
                                                result.push_back({i, all[i]});
                                            }
                                            return result;
                                        },
                                        &TerminalLine::index, [=](const TerminalLine& line) { return Text(line.text.empty() ? " " : line.text).font(mono); }))
                                 .padding(EdgeInsets{.top = 12, .leading = 14, .trailing = 14})
                                 .frame({.max_width = infinity, .alignment = Alignment::leading})),
                  HStack({.spacing = 0}, Text([=] { return prompt(); }).font(mono).foreground_style(Color::hex(0x7EE787)),
                         TextField("", input.binding(), {.on_submit = run})
                             .text_field_style(PlainTextFieldStyle{})
                             .font(mono)
                             .focused(focused.binding()))
                      .padding(EdgeInsets{.leading = 14, .bottom = 12, .trailing = 14}))
        .foreground_style(Color::hex(0xD8DEE9))
        .background(Color::hex(0x161B22))
        .tint(Color::hex(0x7EE787));
}

// --- Notes ---

View NotesApp(const Desktop& d, const Window& w) {
    const State<std::vector<Note>> notes = d.notes;
    int first = notes.peek().empty() ? 0 : notes.peek().front().id;
    if (!w.argument.empty()) {
        first = std::stoi(w.argument);
    }
    const State<int> selected(first);
    // The window's title follows the note.
    effect([=] {
        for (const Note& note : notes()) {
            if (note.id == selected()) {
                w.title = "Notes - " + note.title;
            }
        }
    });
    const auto field = [=](std::string Note::*member) {
        return Binding<std::string>(
            [=] {
                for (const Note& note : notes()) {
                    if (note.id == selected()) {
                        return note.*member;
                    }
                }
                return std::string();
            },
            [=](std::string value) {
                notes.update([&](std::vector<Note>& all) {
                    for (Note& note : all) {
                        if (note.id == selected.peek()) {
                            note.*member = std::move(value);
                        }
                    }
                });
            });
    };
    const auto add = [=] {
        int id = 1;
        for (const Note& note : notes.peek()) {
            id = std::max(id, note.id + 1);
        }
        notes.update([&](std::vector<Note>& all) { all.push_back({id, "Untitled.txt", ""}); });
        selected = id;
    };
    const View list = Sidebar(VStack({.alignment = HorizontalAlignment::leading, .spacing = 2},
                                     HStack(Text("Notes").font(Font::system(13, Weight::semibold)).foreground_style(ThemeColor::secondary),
                                            Spacer(),
                                            Button(Image("plus").padding(4), add).button_style(HoverButtonStyle(6)).help("New note"))
                                         .padding(EdgeInsets{.leading = 8, .bottom = 6}),
                                     ForEach([=] { return notes(); }, &Note::id, [=](const Note& note) {
                                         const int id = note.id;
                                         return Button(VStack({.alignment = HorizontalAlignment::leading, .spacing = 2},
                                                              Text(note.title).font(Font::system(14, Weight::medium)).line_limit(1),
                                                              Text(note.body.empty() ? "No text" : note.body)
                                                                  .font(Font::system(12))
                                                                  .foreground_style(ThemeColor::secondary)
                                                                  .line_limit(1))
                                                           .padding(EdgeInsets::symmetric(10, 7))
                                                           .frame({.max_width = infinity, .alignment = Alignment::leading}),
                                                       [=] { selected = id; })
                                             .button_style(HoverButtonStyle(7, [=] { return selected() == id; }));
                                     })));
    const View editor = ForEach([=] { return std::vector<int>{selected()}; }, [=](int) {
        return VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                      TextField("Title", field(&Note::title)).text_field_style(PlainTextFieldStyle{}).font(Font::system(20, Weight::bold)),
                      Divider(),
                      ScrollView(TextField("Start writing...", field(&Note::body), {.max_lines = 400})
                                     .text_field_style(PlainTextFieldStyle{})
                                     .font(Font::system(15))
                                     .frame({.max_width = infinity, .alignment = Alignment::top_leading})))
            .padding(20)
            .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top_leading});
    });
    return HStack({.spacing = 0}, list, editor);
}

// --- Calculator ---

struct Calculator {
    State<std::string> display{"0"};
    State<double> stored{0};
    State<char> op{0};
    State<bool> fresh{true}; // the next digit starts a new number

    static std::string format(double value) {
        if (!std::isfinite(value)) {
            return "Error";
        }
        std::string text = std::format("{:.10g}", value);
        return text;
    }
    double value() const { return display.peek() == "Error" ? 0 : std::stod(display.peek()); }
    void digit(const std::string& d) const {
        if (fresh.peek() || display.peek() == "0" || display.peek() == "Error") {
            display = d == "." ? "0." : d;
            fresh = false;
        } else if (d != "." || display.peek().find('.') == std::string::npos) {
            if (display.peek().size() < 14) {
                display = display.peek() + d;
            }
        }
    }
    double apply() const {
        const double a = stored.peek();
        const double b = value();
        switch (op.peek()) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return b == 0 ? NAN : a / b;
        default: return b;
        }
    }
    void operation(char next) const {
        batch([&] {
            if (op.peek() && !fresh.peek()) {
                display = format(apply());
            }
            stored = value();
            op = next;
            fresh = true;
        });
    }
    void equals() const {
        batch([&] {
            display = format(apply());
            op = 0;
            fresh = true;
        });
    }
};

class CalcButtonStyle final : public ButtonStyle {
public:
    explicit CalcButtonStyle(int kind) : kind_(kind) {}
    View make_body(const ButtonConfiguration& button) const override {
        const int kind = kind_;
        return button.label.font(Font::system(22, Weight::medium))
            .foreground_style([=](const EnvironmentValues& env) {
                return kind == 2 ? Color::white : env.color(ThemeColor::primary);
            })
            .frame({.max_width = infinity, .max_height = infinity})
            .background(
                [=](const EnvironmentValues& env) {
                    Color color = kind == 2   ? env.color(ThemeColor::accent)
                                  : kind == 1 ? env.color(ThemeColor::surface_raised)
                                              : env.color(ThemeColor::surface);
                    if (button.is_pressed()) {
                        color = color.opacity(0.7f);
                    } else if (button.is_hovered()) {
                        color = color.opacity(0.85f);
                    }
                    return ShapeStyle(color);
                },
                RoundedRectangle(10));
    }

private:
    int kind_; // 0: digit, 1: function, 2: operator
};

View CalculatorApp() {
    const Calculator calc;
    const auto key = [=](std::string label, int kind, std::function<void()> action) {
        return Button(Text(std::move(label)), std::move(action)).button_style(CalcButtonStyle(kind));
    };
    const auto digit = [=](std::string d) { return key(d, 0, [=] { calc.digit(d); }); };
    const auto op = [=](std::string label, char c) { return key(std::move(label), 2, [=] { calc.operation(c); }); };
    return VStack({.spacing = 8},
                  Text([=] { return calc.display(); })
                      .font(Font::system(44, Weight::light))
                      .line_limit(1)
                      .frame({.max_width = infinity, .alignment = Alignment::trailing})
                      .padding(EdgeInsets{.top = 10, .bottom = 6, .trailing = 6}),
                  HStack({.spacing = 8},
                         key("C", 1, [=] {
                             batch([&] {
                                 calc.display = std::string("0");
                                 calc.stored = 0.0;
                                 calc.op = char{0};
                                 calc.fresh = true;
                             });
                         }),
                         key("+/-", 1, [=] { calc.display = Calculator::format(-calc.value()); }),
                         key("%", 1, [=] { calc.display = Calculator::format(calc.value() / 100); }), op("/", '/')),
                  HStack({.spacing = 8}, digit("7"), digit("8"), digit("9"), op("x", '*')),
                  HStack({.spacing = 8}, digit("4"), digit("5"), digit("6"), op("-", '-')),
                  HStack({.spacing = 8}, digit("1"), digit("2"), digit("3"), op("+", '+')),
                  HStack({.spacing = 8}, digit("0"), digit("."), key("=", 2, [=] { calc.equals(); })))
        .padding(14)
        .frame({.max_width = infinity, .max_height = infinity});
}

// --- System Monitor ---

View CpuGraph(const Desktop& d) {
    // Rebuilt once a second with the new sample.
    return ForEach([=] { return std::vector<std::time_t>{d.now()}; }, [=](std::time_t) {
        const std::vector<float> samples = d.cpu_history.peek();
        return Canvas([samples](GraphicsContext& g) {
                   const Size size = g.size();
                   for (int i = 1; i < 4; ++i) {
                       const float y = size.height * static_cast<float>(i) / 4;
                       g.fill_rect({0, y, size.width, 1}, Color::hex(0x8B949E).opacity(0.2f));
                   }
                   if (samples.size() < 2) {
                       return;
                   }
                   const float step = size.width / static_cast<float>(samples.size() - 1);
                   Path line;
                   Path area;
                   area.move_to({0, size.height});
                   for (std::size_t i = 0; i < samples.size(); ++i) {
                       const Point p{static_cast<float>(i) * step, size.height * (1 - samples[i])};
                       if (i == 0) {
                           line.move_to(p);
                       } else {
                           line.line_to(p);
                       }
                       area.line_to(p);
                   }
                   area.line_to({size.width, size.height}).close();
                   g.fill_path(area, LinearGradient({Color::hex(0x30D158).opacity(0.45f), Color::hex(0x30D158).opacity(0.02f)}));
                   g.stroke_path(line, Color::hex(0x30D158), {.width = 2});
               })
            .frame({.max_width = infinity, .min_height = 160, .max_height = 160});
    });
}

View StatCard(std::string title, std::function<std::string()> value, std::function<float()> fraction) {
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 6},
                  Text(std::move(title)).font(Font::system(12)).foreground_style(ThemeColor::secondary),
                  Text([=] { return value(); }).font(Font::system(20, Weight::semibold)),
                  ProgressView([=] { return fraction(); }))
        .padding(14)
        .frame({.max_width = infinity, .alignment = Alignment::leading})
        .background(ThemeColor::surface, RoundedRectangle(10));
}

struct Process {
    int id;
    std::string name;
    std::string icon;
    friend bool operator==(const Process&, const Process&) = default;
};

View MonitorApp(const Desktop& d) {
    const auto cpu = [=] { return d.cpu_history().back(); };
    const auto memory = [=] { return 0.32f + 0.05f * static_cast<float>(d.open_windows().size()); };
    const auto processes = [=] {
        std::vector<Process> list{{-1, "elegance-shell", "squares-four"}, {-2, "compositor", "monitor"}};
        for (int id : d.open_windows()) {
            const Window& w = d.window(id);
            list.push_back({id, app_info(w.app).name, app_info(w.app).icon});
        }
        return list;
    };
    return ScrollView(
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 14},
               HStack({.spacing = 12},
                      StatCard("CPU", [=] { return std::format("{:.0f}%", cpu() * 100); }, cpu),
                      StatCard("Memory", [=] { return std::format("{:.1f} of 16 GB", memory() * 16); }, memory),
                      StatCard("Disk", [] { return std::string("212 of 512 GB"); }, [] { return 0.41f; })),
               VStack({.alignment = HorizontalAlignment::leading, .spacing = 8},
                      Text("CPU, last minute").font(Font::system(13, Weight::semibold)), CpuGraph(d))
                   .padding(14)
                   .background(ThemeColor::surface, RoundedRectangle(10)),
               Text("Processes").font(Font::system(13, Weight::semibold)),
               VStack({.spacing = 0}, ForEach(processes, &Process::id, [=](const Process& p) {
                          const int id = p.id;
                          return HStack({.spacing = 10}, Image(p.icon).foreground_style(ThemeColor::accent).frame({.width = 20}),
                                        Text(p.name), Spacer(),
                                        Text(std::format("{} MB", 40 + (id + 7) * 37 % 300))
                                            .font(Font::system(12))
                                            .foreground_style(ThemeColor::secondary),
                                        If(id > 0,
                                           Button("End task", [=] { d.close(id); }).button_style(BorderedButtonStyle{}),
                                           Text("system").font(Font::system(12)).foreground_style(ThemeColor::tertiary)))
                              .padding(EdgeInsets::symmetric(12, 8))
                              .overlay(Rectangle().fill(ThemeColor::outline).frame({.max_width = infinity, .min_height = 1, .max_height = 1}),
                                       Alignment::bottom);
                      }))
                   .background(ThemeColor::surface, RoundedRectangle(10))
                   .clip_shape(RoundedRectangle(10)))
            .padding(18));
}

} // namespace

View AppContent(const Desktop& desktop, const Window& window) {
    switch (window.app) {
    case AppKind::settings: return SettingsApp(desktop);
    case AppKind::files: return FilesApp(desktop);
    case AppKind::terminal: return TerminalApp(desktop, window);
    case AppKind::notes: return NotesApp(desktop, window);
    case AppKind::calculator: return CalculatorApp();
    case AppKind::monitor: return MonitorApp(desktop);
    }
    return EmptyView();
}

} // namespace shell
