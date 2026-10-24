// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "todos.h"

#include "palette.h"

#include <elg/elegance.h>

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
                           .stroke_border(todo.done ? Prop<Color>(palette::green) : Prop<Color>(ThemeColor::outline), 2)
                           .frame(20, 20)
                           .content_shape(Rectangle())
                           .on_tap_gesture(std::move(toggle));

    return HStack({.spacing = 12},
                  check,
                  Text(todo.title).foreground_style(todo.done ? ThemeColor::secondary : ThemeColor::primary),
                  Spacer(),
                  Button("Remove", std::move(remove)).button_style(BorderedButtonStyle{}).tint(ThemeColor::danger))
        .padding(Edges::horizontal, 14)
        .padding(Edges::vertical, 10)
        .background(ThemeColor::surface, RoundedRectangle(10));
}

} // namespace

View TodoWindow(State<ColorScheme> scheme) {
    const auto todos = State(std::vector<Todo>{
        {1, "Design the View API", true},
        {2, "Render text with MSDF", true},
        {3, "Add State and Binding", true},
        {4, "Add If and ForEach"},
        {5, "Theme and environment (M3)"},
    });
    const auto next_id = State(6);

    // Every change to the list animates: rows slide in and out, and the rest move into place.
    const auto animate = [](auto&& change) { with_animation(Animation::spring(0.4, 0.86), change); };

    // What's typed in the new-task field.
    const auto draft = State(std::string());
    const auto add = [=] {
        animate([=] {
            const int id = next_id.peek();
            next_id = id + 1;
            const std::string title = draft.peek().empty() ? std::format("New task #{}", id) : draft.peek();
            todos.update([&](std::vector<Todo>& list) { list.push_back({id, title}); });
        });
        draft = std::string();
    };
    const auto toggle = [=](int id) {
        return [=] {
            animate([=] {
                todos.update([&](std::vector<Todo>& list) {
                    for (Todo& todo : list) {
                        if (todo.id == id) {
                            todo.done = !todo.done;
                        }
                    }
                });
            });
        };
    };
    const auto remove = [=](int id) {
        return [=] {
            animate([=] {
                todos.update([&](std::vector<Todo>& list) { std::erase_if(list, [&](const Todo& t) { return t.id == id; }); });
            });
        };
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
                                       animate([=] {
                                           todos.update([](std::vector<Todo>& list) {
                                               std::erase_if(list, [](const Todo& todo) { return todo.done; });
                                           });
                                       });
                                   })
                                .button_style(BorderedButtonStyle{}))),
                  // Enter in the field adds the task too.
                  HStack({.spacing = 10},
                         TextField("New task", draft.binding(), {.on_submit = add}),
                         Button("Add", add)),
                  Text([=] {
                      const auto left = remaining();
                      return left == 0 ? std::string("All done") : std::format("{} left", left);
                  }).foreground_style(ThemeColor::secondary),
                  // The header stays put; the list scrolls once it outgrows the window.
                  ScrollView(VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                                    If([=] { return todos().empty(); },
                                       Text("Nothing to do. Add a task to get started.")
                                           .foreground_style(ThemeColor::secondary)
                                           .padding(Edges::top, 8)),
                                    ForEach(
                                        todos, [](const Todo& todo) { return todo.id; },
                                        [=](const Todo& todo) {
                                            return TodoRow(todo, toggle(todo.id), remove(todo.id))
                                                .transition(Transition::opacity().combined(Transition::move(Edges::trailing)));
                                        }))))
        .padding(24)
        .frame({.max_width = infinity, .max_height = infinity})
        .background(ThemeColor::background)
        .theme(palette::theme)
        .color_scheme(scheme);
}
