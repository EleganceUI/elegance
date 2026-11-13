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

/// How a split view's dividers look. A divider is a line that takes space between the panes; it can
/// be grabbed a few points beside the line too.
struct SplitViewOptions {
    Prop<Color> divider = ThemeColor::outline; ///< The line's color.
    std::optional<Prop<Color>> active_divider; ///< while hovered or dragged; nullopt: the tint
    float thickness = 1;                       ///< the line, in points; negative counts as 0
    float active_thickness = 3;                ///< drawn over the line while hovered or dragged
};

namespace detail {
ELG_API View make_split_view(Axis axis, std::vector<View> panes, SplitViewOptions options);
} // namespace detail

/// Panes side by side with a divider between each two; dragging a divider resizes the panes on
/// either side of it. A pane's limits come from its own layout: it shrinks to what it takes when
/// offered nothing and grows to what it takes when offered everything, so
///
/// @code
/// Sidebar().frame({.min_width = 180, .ideal_width = 240, .max_width = 400})
/// @endcode
///
/// stays within 180...400 and starts at 240. Panes start at their ideal size. When the split view
/// grows or shrinks, the pane with the highest layout_priority() (ties: the last one) takes the
/// difference, then the others as their limits allow. A pane that's an If can come and go (a
/// collapsible sidebar); the others keep their sizes.
///
/// @code
/// HSplitView({.divider = Color::clear, .thickness = 0}, Sidebar(), Editor())
/// @endcode
///
/// @param options How the dividers look.
/// @param panes The panes, left to right.
/// @return The split view.
template <std::convertible_to<View>... Panes>
View HSplitView(SplitViewOptions options, Panes&&... panes) {
    return detail::make_split_view(Axis::horizontal, {View(std::forward<Panes>(panes))...}, std::move(options));
}
/// Like HSplitView(SplitViewOptions, Panes&&...), with the default dividers.
///
/// @param panes The panes, left to right.
/// @return The split view.
template <std::convertible_to<View>... Panes>
View HSplitView(Panes&&... panes) {
    return HSplitView(SplitViewOptions{}, std::forward<Panes>(panes)...);
}

/// Like HSplitView, with the panes stacked top to bottom.
///
/// @param options How the dividers look.
/// @param panes The panes, top to bottom.
/// @return The split view.
template <std::convertible_to<View>... Panes>
View VSplitView(SplitViewOptions options, Panes&&... panes) {
    return detail::make_split_view(Axis::vertical, {View(std::forward<Panes>(panes))...}, std::move(options));
}
/// Like VSplitView(SplitViewOptions, Panes&&...), with the default dividers.
///
/// @param panes The panes, top to bottom.
/// @return The split view.
template <std::convertible_to<View>... Panes>
View VSplitView(Panes&&... panes) {
    return VSplitView(SplitViewOptions{}, std::forward<Panes>(panes)...);
}

/// A column's width range, in points.
struct ColumnWidth {
    float min = 0;   ///< The narrowest the column gets.
    float ideal = 0; ///< The width it starts at.
    float max = 0;   ///< The widest the column gets.
};

/// Options for NavigationSplitView().
struct NavigationSplitViewOptions {
    /// Shows and hides the sidebar (animate the change with with_animation). Always shown when unset.
    std::optional<Binding<bool>> sidebar_visible;
    /// How the sidebar comes and goes: by default it slides out to the leading edge, and the other
    /// columns take its width.
    Transition sidebar_transition = Transition::move(Edges::leading);
    Prop<Color> sidebar_background = ThemeColor::surface; ///< Color::clear: none
    ColumnWidth sidebar_width{180, 240, 360};             ///< The sidebar's width range.
    ColumnWidth content_width{220, 300, 480};             ///< the three-column form only
    ColumnWidth detail_width{200, 0, infinity};           ///< ideal 0: the rest of the width
    SplitViewOptions dividers;                            ///< How the dividers between the columns look.
};

/// The usual layout of a desktop app: a sidebar to choose from, and the detail of what's chosen,
/// with a draggable divider between: an HSplitView of framed columns. The detail takes the rest of
/// the width, and size changes first. Every column is as tall as the split view; the sidebar's
/// content sits at its top leading corner, the detail's in its center.
///
/// @code
/// NavigationSplitView(ScrollView(LazyVStack(ForEach(folders, &Folder::id, FolderRow))), NoteEditor(selected))
/// @endcode
///
/// @param sidebar The sidebar column's content.
/// @param detail The detail column's content.
/// @param options Column widths, the sidebar's visibility and background, and the dividers.
/// @return The split view.
ELG_API View NavigationSplitView(const View& sidebar, const View& detail, const NavigationSplitViewOptions& options = {});
/// Three columns: sidebar, content (e.g. a list of what's in the chosen folder), detail. The content
/// column's view sits at its top leading corner, like the sidebar's.
///
/// @param sidebar The sidebar column's content.
/// @param content The middle column's content.
/// @param detail The detail column's content.
/// @param options Column widths, the sidebar's visibility and background, and the dividers.
/// @return The split view.
ELG_API View NavigationSplitView(const View& sidebar, const View& content, const View& detail, const NavigationSplitViewOptions& options = {});

/// How a page comes in when pushed and goes when popped.
enum class PageTransition {
    slide, ///< over the page underneath, from the trailing edge
    fade,  ///< cross-fades with the page underneath
    none,  ///< at once
};

/// Options for NavigationStack().
///
/// @tparam Route The type of the routes in the stack's path.
template <class Route>
struct NavigationStackOptions {
    /// The root page's title, for the style (BarNavigationStackStyle shows it, and on the back
    /// button once a page is pushed). Static or reactive: `{.root_title = [=] { return folder(); }}`
    Prop<std::string> root_title = std::string();
    /// A pushed page's title, for the style. Unset: pushed pages have empty titles.
    std::function<std::string(const Route&)> title;
    PageTransition transition = PageTransition::slide; ///< How pages come and go.
    /// For pushes and pops that aren't animated themselves (with_animation picks its own).
    /// nullopt: those change at once.
    std::optional<Animation> animation = Animation::smooth(0.35);
    /// Alt+Left, and swiping right with two fingers on a trackpad (macOS), go back.
    bool back_gestures = true;
};

/// What a NavigationStackStyle gets: the pages (put them where they go), their titles, and going
/// back.
struct NavigationStackConfiguration {
    View pages;                              ///< The stack's pages, to place in the style's body.
    Signal<std::vector<std::string>> titles; ///< the root's, then each pushed page's
    Signal<bool> can_go_back;                ///< a page is pushed
    std::function<void()> back;              ///< pops the top page
};

/// Decides what's around a NavigationStack's pages (a bar with the title and a back button, say).
/// Set one with .navigation_stack_style(); PlainNavigationStackStyle is the default.
class ELG_API NavigationStackStyle {
public:
    virtual ~NavigationStackStyle();
    /// Called once per NavigationStack when it's built, while Environment<K>() is available.
    ///
    /// @param stack The pages, their titles, and going back.
    /// @return The stack's body, with `stack.pages` in it.
    [[nodiscard]] virtual View make_body(const NavigationStackConfiguration& stack) const = 0;

protected:
    // Styles are copied as their concrete type, never as a NavigationStackStyle.
    NavigationStackStyle() = default;
    NavigationStackStyle(const NavigationStackStyle&) = default;
    NavigationStackStyle(NavigationStackStyle&&) = default;
    NavigationStackStyle& operator=(const NavigationStackStyle&) = default;
    NavigationStackStyle& operator=(NavigationStackStyle&&) = default;
};

/// Just the pages: go back from your own controls (or the back gestures). The default.
class ELG_API PlainNavigationStackStyle final : public NavigationStackStyle {
public:
    /// @param stack The pages, their titles, and going back.
    /// @return `stack.pages`, as they are.
    [[nodiscard]] View make_body(const NavigationStackConfiguration& stack) const override;
};

/// Options for BarNavigationStackStyle.
struct BarNavigationStackStyleOptions {
    float height = 44;                     ///< The bar's height in points.
    Prop<Color> background = Color::clear; ///< The bar's background.
    Font title_font = Font::headline;      ///< The title's font.
    std::optional<Prop<Color>> back_color; ///< the back button; nullopt: the tint
    bool shows_divider = true;             ///< under the bar
};

/// A bar over the pages with the top page's title, centered, and once a page is pushed, a back
/// button named after the page underneath ("Back" when that page has no title).
///
/// @code
/// NavigationStack(path, Notes(), NotePage, {.root_title = "Notes"})
///     .navigation_stack_style(BarNavigationStackStyle{{.height = 38}})
/// @endcode
class ELG_API BarNavigationStackStyle final : public NavigationStackStyle {
public:
    /// @param options The bar's height, colors, title font and divider.
    explicit BarNavigationStackStyle(BarNavigationStackStyleOptions options = {}) : options_(std::move(options)) {}
    /// @param stack The pages, their titles, and going back.
    /// @return The bar (and a Separator under it unless `shows_divider` is false) over `stack.pages`.
    [[nodiscard]] View make_body(const NavigationStackConfiguration& stack) const override;

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
                                   std::function<std::vector<std::string>()> titles, const std::function<void()>& pop,
                                   NavigationStackBehavior behavior);
} // namespace detail

/// Pages on top of each other, driven by `path`: the root, then a page for each route in the path,
/// built by `destination`. Only the last is shown. Pushing is adding to the path and popping is
/// removing from it, from anywhere (a deep link is just a path):
///
/// @code
/// const State<std::vector<int>> path;
/// NavigationStack(path, NotesList([=](int id) { path.update([&](auto& p) { p.push_back(id); }); }),
///                 [](int id) { return NoteView(id); }, {.root_title = "Notes"})
/// @endcode
///
/// Pages slide in when pushed and out when popped (NavigationStackOptions::transition). Pages under
/// the top one stay alive, keeping their state and scroll positions, until they're removed from the
/// path. Alt+Left goes back, and so does swiping right with two fingers on a trackpad (macOS), the
/// page following them. Routes are compared with ==: a page is rebuilt when its route changes.
/// What's around the pages is the NavigationStackStyle's: none by default;
/// .navigation_stack_style(BarNavigationStackStyle{}) adds a bar with the title and a back button.
///
/// @param path The routes of the pages above the root, bottom to top. Going back pops its last one.
/// @param root The page at the bottom, shown when `path` is empty.
/// @param destination Builds the page for a route.
/// @param options Titles, the transition and animation, and the back gestures.
/// @return The stack.
template <class Route>
    requires std::equality_comparable<Route>
View NavigationStack(Binding<std::vector<Route>> path, View root,
                     std::type_identity_t<std::function<View(const Route&)>> destination,
                     const NavigationStackOptions<Route>& options = {}) {
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

/// Like NavigationStack(Binding<std::vector<Route>>, ...), driven by a State's binding.
///
/// @param path The routes of the pages above the root, bottom to top. Going back pops its last one.
/// @param root The page at the bottom, shown when `path` is empty.
/// @param destination Builds the page for a route.
/// @param options Titles, the transition and animation, and the back gestures.
/// @return The stack.
template <class Route>
    requires std::equality_comparable<Route>
View NavigationStack(const State<std::vector<Route>>& path, View root,
                     std::type_identity_t<std::function<View(const Route&)>> destination,
                     const NavigationStackOptions<Route>& options = {}) {
    return NavigationStack(path.binding(), std::move(root), std::move(destination), std::move(options));
}

} // namespace elg

#endif // ELG_NAVIGATION_H
