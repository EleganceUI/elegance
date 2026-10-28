// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_NAVIGATION_H
#define ELG_NAVIGATION_H

// Split views and navigation (design.md §13.5): resizable panes, a sidebar layout, and a stack
// of pages driven by a path.

#include <elg/animation.h>
#include <elg/export.h>
#include <elg/font.h>
#include <elg/prop.h>
#include <elg/reactive.h>
#include <elg/view.h>
#include <elg/views.h>

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace elg {

// How a split view's dividers look. A divider is a line that takes space between the panes; it can
// be grabbed a few points beside the line too.
struct SplitViewOptions {
    Prop<Color> divider = ThemeColor::outline;
    std::optional<Prop<Color>> active_divider; // while hovered or dragged; nullopt: the tint
    float thickness = 1;                       // the line
    float active_thickness = 3;                // drawn over the line while hovered or dragged
};

namespace detail {
ELG_API View make_split_view(Axis axis, std::vector<View> panes, SplitViewOptions options);
} // namespace detail

// Panes side by side with a divider between each two; dragging a divider resizes the panes on
// either side of it. A pane's limits come from its own layout: it shrinks to what it takes when
// offered nothing and grows to what it takes when offered everything, so
//   Sidebar().frame({.min_width = 180, .ideal_width = 240, .max_width = 400})
// stays within 180...400 and starts at 240. Panes start at their ideal size. When the split view
// grows or shrinks, the pane with the highest layout_priority() (ties: the last one) takes the
// difference, then the others as their limits allow. A pane that's an If can come and go (a
// collapsible sidebar); the others keep their sizes.
//   HSplitView({.divider = Color::clear, .thickness = 0}, Sidebar(), Editor())
template <std::convertible_to<View>... Panes>
View HSplitView(SplitViewOptions options, Panes&&... panes) {
    return detail::make_split_view(Axis::horizontal, {View(std::forward<Panes>(panes))...}, std::move(options));
}
template <std::convertible_to<View>... Panes>
View HSplitView(Panes&&... panes) {
    return HSplitView(SplitViewOptions{}, std::forward<Panes>(panes)...);
}

// Like HSplitView, with the panes stacked top to bottom.
template <std::convertible_to<View>... Panes>
View VSplitView(SplitViewOptions options, Panes&&... panes) {
    return detail::make_split_view(Axis::vertical, {View(std::forward<Panes>(panes))...}, std::move(options));
}
template <std::convertible_to<View>... Panes>
View VSplitView(Panes&&... panes) {
    return VSplitView(SplitViewOptions{}, std::forward<Panes>(panes)...);
}

// A column's width range, in points.
struct ColumnWidth {
    float min = 0;
    float ideal = 0;
    float max = 0;
};

struct NavigationSplitViewOptions {
    // Shows and hides the sidebar (animate the change with with_animation). Always shown when unset.
    std::optional<Binding<bool>> sidebar_visible;
    // How the sidebar comes and goes: by default it slides out to the leading edge, and the other
    // columns take its width.
    Transition sidebar_transition = Transition::move(Edges::leading);
    Prop<Color> sidebar_background = ThemeColor::surface; // Color::clear: none
    ColumnWidth sidebar_width{180, 240, 360};
    ColumnWidth content_width{220, 300, 480};   // the three-column form only
    ColumnWidth detail_width{200, 0, infinity}; // ideal 0: the rest of the width
    SplitViewOptions dividers;
};

// The usual layout of a desktop app: a sidebar to choose from, and the detail of what's chosen,
// with a draggable divider between: an HSplitView of framed columns. The detail takes the rest of
// the width.
//   NavigationSplitView(ScrollView(LazyVStack(ForEach(folders, &Folder::id, FolderRow))), NoteEditor(selected))
ELG_API View NavigationSplitView(View sidebar, View detail, NavigationSplitViewOptions options = {});
// Three columns: sidebar, content (e.g. a list of what's in the chosen folder), detail.
ELG_API View NavigationSplitView(View sidebar, View content, View detail, NavigationSplitViewOptions options = {});

// How a page comes in when pushed and goes when popped.
enum class PageTransition {
    slide, // over the page underneath, from the trailing edge
    fade,  // cross-fades with the page underneath
    none,  // at once
};

template <class Route>
struct NavigationStackOptions {
    // The root page's title, for the style (BarNavigationStackStyle shows it, and on the back
    // button once a page is pushed). Static or reactive: {.root_title = [=] { return folder(); }}
    Prop<std::string> root_title = std::string();
    // A pushed page's title, for the style.
    std::function<std::string(const Route&)> title;
    PageTransition transition = PageTransition::slide;
    // For pushes and pops that aren't animated themselves (with_animation picks its own).
    // nullopt: those change at once.
    std::optional<Animation> animation = Animation::smooth(0.35);
    // Alt+Left, and swiping right with two fingers on a trackpad (macOS), go back.
    bool back_gestures = true;
};

// What a NavigationStackStyle gets: the pages (put them where they go), their titles, and going
// back.
struct NavigationStackConfiguration {
    View pages;
    Signal<std::vector<std::string>> titles; // the root's, then each pushed page's
    Signal<bool> can_go_back;                // a page is pushed
    std::function<void()> back;              // pops the top page
};

class ELG_API NavigationStackStyle {
public:
    virtual ~NavigationStackStyle();
    // Called once per NavigationStack when it's built, while Environment<K>() is available.
    virtual View make_body(const NavigationStackConfiguration& stack) const = 0;

protected:
    // Styles are copied as their concrete type, never as a NavigationStackStyle.
    NavigationStackStyle() = default;
    NavigationStackStyle(const NavigationStackStyle&) = default;
    NavigationStackStyle(NavigationStackStyle&&) = default;
    NavigationStackStyle& operator=(const NavigationStackStyle&) = default;
    NavigationStackStyle& operator=(NavigationStackStyle&&) = default;
};

// Just the pages: go back from your own controls (or the back gestures). The default.
class ELG_API PlainNavigationStackStyle final : public NavigationStackStyle {
public:
    View make_body(const NavigationStackConfiguration& stack) const override;
};

struct BarNavigationStackStyleOptions {
    float height = 44;
    Prop<Color> background = Color::clear;
    Font title_font = Font::headline;
    std::optional<Prop<Color>> back_color; // the back button; nullopt: the tint
    bool shows_divider = true;             // under the bar
};

// A bar over the pages with the top page's title, centered, and once a page is pushed, a back
// button named after the page underneath.
//   NavigationStack(path, Notes(), NotePage, {.root_title = "Notes"})
//       .navigation_stack_style(BarNavigationStackStyle{{.height = 38}})
class ELG_API BarNavigationStackStyle final : public NavigationStackStyle {
public:
    explicit BarNavigationStackStyle(BarNavigationStackStyleOptions options = {}) : options_(std::move(options)) {}
    View make_body(const NavigationStackConfiguration& stack) const override;

private:
    BarNavigationStackStyleOptions options_;
};

namespace detail {
struct NavigationStackBehavior {
    PageTransition transition = PageTransition::slide;
    std::optional<Animation> animation;
    bool back_gestures = true;
};
ELG_API View make_navigation_stack(View root, std::function<std::vector<ForEachItem>()> pages,
                                   std::function<std::vector<std::string>()> titles, std::function<void()> pop,
                                   NavigationStackBehavior behavior);
} // namespace detail

// Pages on top of each other, driven by `path`: the root, then a page for each route in the path,
// built by `destination`. Only the last is shown. Pushing is adding to the path and popping is
// removing from it, from anywhere (a deep link is just a path):
//   const State<std::vector<int>> path;
//   NavigationStack(path, NotesList([=](int id) { path.update([&](auto& p) { p.push_back(id); }); }),
//                   [](int id) { return NoteView(id); }, {.root_title = "Notes"})
// Pages slide in when pushed and out when popped (NavigationStackOptions::transition). Pages under
// the top one stay alive, keeping their state and scroll positions, until they're removed from the
// path. Alt+Left goes back, and so does swiping right with two fingers on a trackpad (macOS), the
// page following them. Routes are compared with ==: a page is rebuilt when its route changes.
// What's around the pages is the NavigationStackStyle's: none by default;
// .navigation_stack_style(BarNavigationStackStyle{}) adds a bar with the title and a back button.
template <class Route>
    requires std::equality_comparable<Route>
View NavigationStack(Binding<std::vector<Route>> path, View root,
                     std::type_identity_t<std::function<View(const Route&)>> destination,
                     NavigationStackOptions<Route> options = {}) {
    bool (*equals)(const void*, const void*) = [](const void* a, const void* b) {
        return *static_cast<const Route*>(a) == *static_cast<const Route*>(b);
    };
    auto pages = [path, destination = std::move(destination), equals] {
        std::vector<detail::ForEachItem> result;
        const std::vector<Route> routes = path(); // a tracked read: the stack follows the path
        result.reserve(routes.size());
        for (std::size_t i = 0; i < routes.size(); ++i) {
            auto shared = std::make_shared<const Route>(routes[i]);
            result.push_back({static_cast<std::int64_t>(i), shared, equals,
                              [shared, destination] { return View(destination(*shared)); }});
        }
        return result;
    };
    auto titles = [path, root_title = options.root_title, title = options.title] {
        std::vector<std::string> result{root_title.get()};
        for (const Route& route : path()) {
            result.push_back(title ? title(route) : std::string());
        }
        return result;
    };
    auto pop = [path] {
        std::vector<Route> routes = untrack([&] { return path(); });
        if (!routes.empty()) {
            routes.pop_back();
            path.set(std::move(routes));
        }
    };
    return detail::make_navigation_stack(
        std::move(root), std::move(pages), std::move(titles), std::move(pop),
        {.transition = options.transition, .animation = options.animation, .back_gestures = options.back_gestures});
}

template <class Route>
    requires std::equality_comparable<Route>
View NavigationStack(const State<std::vector<Route>>& path, View root,
                     std::type_identity_t<std::function<View(const Route&)>> destination,
                     NavigationStackOptions<Route> options = {}) {
    return NavigationStack(path.binding(), std::move(root), std::move(destination), std::move(options));
}

} // namespace elg

#endif // ELG_NAVIGATION_H
