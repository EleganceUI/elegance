// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Desktop: the system's file dialogs, notifications, a tray icon with a menu, and dropping
// files from other apps.

#include <elg/elegence.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <vector>

using namespace elg;

// The tray icon shows while `tray` holds it (main keeps it for the app's lifetime).
View Desktop(App& app, std::optional<TrayIcon>& tray) {
    const State<std::vector<std::filesystem::path>> files;
    const State<bool> targeted(false);
    const State<bool> paused(false);

    tray.emplace(TrayIconOptions{.tooltip = "Elegence desktop example",
                         .menu = {{.title = "Paused",
                                   .checked = false,
                                   .toggled = [=](bool on) { paused = on; }},
                                  {}, // a separator
                                  {.title = "Quit", .action = [&app] { app.quit(); }}}});

    const auto save_list = [=] {
        choose_save_file({.filters = {{"Text", {"txt"}}}, .name = "files.txt"},
                         [=](std::optional<std::filesystem::path> path) {
                             if (!path) {
                                 return;
                             }
                             std::ofstream out(*path);
                             for (const auto& file : files.peek()) {
                                 out << file.string() << '\n';
                             }
                             notify({.title = "List saved", .body = path->filename().string()});
                         });
    };

    const View drop_zone =
        VStack({.spacing = 8},
               Image("tray-arrow-down").font(Font::title),
               Text([=] { return files().empty() ? "Drop files here" : std::format("{} files", files().size()); }))
            .frame({.max_width = infinity, .min_height = 140, .max_height = 140})
            .background([=] { return targeted() ? Color::hex(0x0A84FF).opacity(0.12f) : Color::clear; },
                        RoundedRectangle(12))
            .overlay(RoundedRectangle(12).stroke(ThemeColor::outline, {.width = 2, .dash = {6, 4}}))
            .on_drop({.files =
                          [=](const std::vector<std::filesystem::path>& dropped, Point) {
                              files.update([&](auto& list) { list.insert(list.end(), dropped.begin(), dropped.end()); });
                              return true;
                          },
                      .targeted = [=](bool on) { targeted = on; }});

    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 14},
                  drop_zone,
                  HStack(Button("Add files…",
                                [=] {
                                    choose_files({.multiple = true}, [=](std::vector<std::filesystem::path> chosen) {
                                        files.update([&](auto& list) { list.insert(list.end(), chosen.begin(), chosen.end()); });
                                    });
                                }),
                         Button("Save list…", save_list)
                             .button_style(BorderedButtonStyle{})
                             .disabled([=] { return files().empty(); }),
                         Button("Notify me", [=] { notify({.title = "Hello from Elegence", .body = "Click me"}); })
                             .button_style(BorderedButtonStyle{})),
                  Text([=] { return paused() ? "Paused from the tray" : "Running"; })
                      .foreground_style(ThemeColor::secondary),
                  ScrollView(VStack({.alignment = HorizontalAlignment::leading},
                                    ForEach(files, [](const std::filesystem::path& file) { return file.string(); },
                                            [](const std::filesystem::path& file) {
                                                return Label(file.filename().string(), {.system_name = "file"});
                                            }))))
        .padding(24)
        .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top_leading});
}

int main() {
    App app;
    std::optional<TrayIcon> tray;
    app.window({.title = "Desktop", .width = 560, .height = 480}, [&] { return Desktop(app, tray); });
    return app.run();
}
