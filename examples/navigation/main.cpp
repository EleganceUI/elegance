// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Navigation: a notes app's layout. NavigationSplitView with a sidebar that hides, a
// NavigationStack driven by a path (a folder's notes, then a note), and a VSplitView.

#include <elg/elegence.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace elg;

struct Note {
    int id = 0;
    std::string folder;
    std::string title;
    std::string body;

    friend bool operator==(const Note&, const Note&) = default;
};

const std::vector<std::string> kFolders{"Inbox", "Ideas", "Recipes"};

const std::vector<Note> kNotes{
    {1, "Inbox", "Groceries", "Oat milk, lemons, basil, a good loaf of bread."},
    {2, "Inbox", "Call the dentist", "Ask about moving the appointment to Thursday afternoon."},
    {3, "Ideas", "A plant-watering app", "Reminders based on the weather, not a fixed schedule."},
    {4, "Ideas", "Weekend project", "Build a tiny synth with a few knobs and a step sequencer."},
    {5, "Recipes", "Lemon pasta", "Zest, juice, butter, parmesan, and plenty of black pepper."},
};

std::vector<Note> notes_in(const std::string& folder) {
    std::vector<Note> result;
    std::ranges::copy_if(kNotes, std::back_inserter(result), [&](const Note& n) { return n.folder == folder; });
    return result;
}

const Note& note_with(int id) {
    return *std::ranges::find(kNotes, id, &Note::id);
}

// A row that opens something: the whole row is the button.
View Row(std::string title, std::function<void()> open) {
    return Button(HStack(Text(std::move(title)).foreground_style(ThemeColor::primary), Spacer(),
                         Image("caret-right").foreground_style(ThemeColor::tertiary))
                      .frame({.max_width = infinity}),
                  std::move(open))
        .button_style(PlainButtonStyle{});
}

View FolderButton(const std::string& name, State<std::string> folder, State<std::vector<int>> path) {
    const auto selected = [=] { return folder() == name; };
    const auto choose = [=] {
        folder = name;
        path = std::vector<int>(); // back to the folder's list
    };
    return Button(Label(name, {.system_name = "folder"})
                      .foreground_style(ThemeColor::primary)
                      .frame({.max_width = infinity, .alignment = Alignment::leading}),
                  choose)
        .button_style(PlainButtonStyle{})
        .padding(EdgeInsets::symmetric(10, 6))
        .background(
            [=](const EnvironmentValues& env) {
                return selected() ? env.color(ThemeColor::accent).opacity(0.18f) : Color::clear;
            },
            RoundedRectangle(6));
}

View NotePage(const Note& note) {
    // Top: the note. Bottom: a scratch area; drag the divider between them.
    return VSplitView(ScrollView(VStack({.alignment = HorizontalAlignment::leading, .spacing = 12},
                                        Text(note.title).font(Font::title2), Text(note.body))
                                     .padding(20)
                                     .frame({.max_width = infinity, .alignment = Alignment::leading}))
                          .layout_priority(1), // takes the height the scratch area doesn't
                      Text("Scratch area")
                          .foreground_style(ThemeColor::tertiary)
                          .frame({.max_width = infinity, .min_height = 60, .ideal_height = 120, .max_height = infinity}));
}

View NotesApp() {
    const State<std::string> folder("Inbox");
    const State<std::vector<int>> path; // the notes opened, one page each
    const State<bool> sidebar(true);

    const View sidebar_view = VStack({.alignment = HorizontalAlignment::leading, .spacing = 2},
                                     ForEach(kFolders, [=](const std::string& name) { return FolderButton(name, folder, path); }),
                                     Spacer())
                                  .padding(8);

    const auto open = [=](int id) { path.update([&](std::vector<int>& p) { p.push_back(id); }); };
    const View notes = ScrollView(
        VStack({.spacing = 0},
               ForEach([=] { return notes_in(folder()); }, &Note::id,
                       [=](const Note& note) { return Row(note.title, [=] { open(note.id); }).padding(12); }))
            .frame({.max_width = infinity}));

    const View detail = NavigationStack(path, notes, [](int id) { return NotePage(note_with(id)); },
                                        {.root_title = [=] { return folder(); },
                                         .title = [](const int& id) { return note_with(id).title; }});

    const View toolbar =
        HStack(Button(Image("sidebar-simple"), [=] { with_animation(Animation::smooth(), [&] { sidebar = !sidebar(); }); })
                   .button_style(PlainButtonStyle{})
                   .accessibility_label("Toggle sidebar"),
               Spacer())
            .padding(EdgeInsets::symmetric(12, 8));

    return VStack({.spacing = 0}, toolbar, Divider(),
                  NavigationSplitView(sidebar_view, detail, {.sidebar_visible = sidebar.binding()}));
}

int main() {
    App app;
    app.window({.title = "Navigation", .width = 900, .height = 560}, NotesApp);
    return app.run();
}
