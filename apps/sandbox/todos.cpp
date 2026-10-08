// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "todos.h"

#include "palette.h"

#include <elg/elegence.h>

#include <algorithm>
#include <format>
#include <string>
#include <vector>

using namespace elg;

namespace {

struct Todo {
    int id = 0;
    std::string title;
    bool done = false;

    // ForEach rebuilds a row only when its item changed.
    friend bool operator==(const Todo&, const Todo&) = default;
};

View TodoRow(const Todo& todo, std::function<void()> toggle, std::function<void()> remove) {
    // Tapping the check circle toggles the todo.
    const View check = Circle()
                           .fill(todo.done ? palette::green : Color::clear)
                           .stroke_border(todo.done ? palette::green : palette::outline, 2)
                           .frame(20, 20)
                           .content_shape(Rectangle())
                           .on_tap_gesture(std::move(toggle));

    return HStack({.spacing = 12},
                  check,
                  Text(todo.title).foreground_style(todo.done ? palette::subtext : palette::text),
                  Spacer(),
                  Button("Remove", std::move(remove)).tint(palette::muted))
        .padding(Edges::horizontal, 14)
        .padding(Edges::vertical, 10)
        .background(palette::surface, RoundedRectangle(10));
}

} // namespace

View TodoWindow() {
    const auto todos = State(std::vector<Todo>{
        {1, "Design the View API", true},
        {2, "Render text with MSDF", true},
        {3, "Add State and Binding", true},
        {4, "Add If and ForEach"},
        {5, "Theme and environment (M3)"},
    });
    const auto next_id = State(6);

    const auto add = [=] {
        const int id = next_id.peek();
        next_id = id + 1;
        todos.update([&](std::vector<Todo>& list) { list.push_back({id, std::format("New task #{}", id)}); });
    };
    const auto toggle = [=](int id) {
        return [=] {
            todos.update([&](std::vector<Todo>& list) {
                for (Todo& todo : list) {
                    if (todo.id == id) {
                        todo.done = !todo.done;
                    }
                }
            });
        };
    };
    const auto remove = [=](int id) {
        return [=] { todos.update([&](std::vector<Todo>& list) { std::erase_if(list, [&](const Todo& t) { return t.id == id; }); }); };
    };
    const auto remaining = computed([=] {
        return std::ranges::count_if(todos(), [](const Todo& todo) { return !todo.done; });
    });

    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                  HStack({.spacing = 12},
                         Text("Todos").font(Font::title2),
                         Spacer(),
                         If([=] { return std::ranges::any_of(todos(), &Todo::done); },
                            Button("Clear done",
                                   [=] {
                                       todos.update([](std::vector<Todo>& list) {
                                           std::erase_if(list, [](const Todo& todo) { return todo.done; });
                                       });
                                   })
                                .tint(palette::muted)),
                         Button("Add", add)),
                  Text([=] {
                      const auto left = remaining();
                      return left == 0 ? std::string("All done") : std::format("{} left", left);
                  }).foreground_style(palette::subtext),
                  If([=] { return todos().empty(); },
                     Text("Nothing to do. Add a task to get started.").foreground_style(palette::subtext).padding(Edges::top, 8)),
                  ForEach(
                      todos, [](const Todo& todo) { return todo.id; },
                      [=](const Todo& todo) { return TodoRow(todo, toggle(todo.id), remove(todo.id)); }),
                  Spacer())
        .padding(24)
        .frame({.max_width = infinity, .max_height = infinity})
        .foreground_style(palette::text);
}
