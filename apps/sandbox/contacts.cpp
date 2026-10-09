// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "contacts.h"

#include "palette.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace elg;

struct Contact {
    int id;
    std::string name;
    std::string email;
    std::string lowercase; // for filtering

    bool operator==(const Contact&) const = default;
};

std::string lowercase(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Deterministic made-up people.
std::vector<Contact> make_contacts(int count) {
    static constexpr std::array first{"Ada", "Grace", "Alan", "Linus", "Margaret", "Dennis", "Barbara", "Ken",
                                      "Edsger", "Frances", "John", "Radia", "Bjarne", "Hedy", "Tim", "Katherine"};
    static constexpr std::array last{"Lovelace", "Hopper", "Turing", "Torvalds", "Hamilton", "Ritchie", "Liskov",
                                     "Thompson", "Dijkstra", "Allen", "Backus", "Perlman", "Stroustrup", "Lamarr",
                                     "Berners-Lee", "Johnson", "Knuth", "Wirth", "Kay", "Shannon"};
    std::vector<Contact> contacts;
    contacts.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::string given = first[static_cast<std::size_t>(i) % first.size()];
        const std::string family = last[static_cast<std::size_t>(i / 7) % last.size()];
        std::string name = std::format("{} {}", given, family);
        std::string email = std::format("{}.{}{}@example.com", lowercase(given), lowercase(family), i);
        std::string key = lowercase(name + " " + email);
        contacts.push_back({i + 1, std::move(name), std::move(email), std::move(key)});
    }
    return contacts;
}

View ContactRow(const Contact& contact) {
    static constexpr std::array colors{palette::blue, palette::mauve, palette::green, palette::peach, palette::sky,
                                       palette::red};
    const Color color = colors[static_cast<std::size_t>(contact.id) % colors.size()];
    const std::string initial(1, contact.name.front());
    return HStack({.spacing = 12},
                  Circle()
                      .fill(color)
                      .frame(36, 36)
                      .overlay(Text(initial).font(Font::headline).foreground_style(Color::hex(0x24273A))),
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 2},
                         Text(contact.name).font(Font::headline),
                         Text(contact.email).font(Font::subheadline).foreground_style(ThemeColor::secondary)),
                  Spacer(),
                  Text(std::format("#{}", contact.id)).font(Font::subheadline).foreground_style(ThemeColor::tertiary));
}

} // namespace

View ContactsWindow(State<ColorScheme> scheme) {
    const auto all = std::make_shared<const std::vector<Contact>>(make_contacts(10'000));
    const auto query = State(std::string());
    // Recomputed per keystroke; the list keeps the rows whose contact is unchanged.
    const auto shown = computed([=] {
        const std::string needle = lowercase(query());
        if (needle.empty()) {
            return *all;
        }
        std::vector<Contact> matches;
        for (const Contact& contact : *all) {
            if (contact.lowercase.find(needle) != std::string::npos) {
                matches.push_back(contact);
            }
        }
        return matches;
    });

    // Scrolls the list from the buttons.
    const auto position = ScrollPosition();

    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                  HStack({.spacing = 12},
                         Text("Contacts").font(Font::title2),
                         Spacer(),
                         Text([=] { return std::format("{} of {}", shown().size(), all->size()); })
                             .foreground_style(ThemeColor::secondary)),
                  HStack({.spacing = 8},
                         TextField("Filter by name or email", query.binding()),
                         Button("Top", [=] { position.scroll_to_top(); }),
                         Button("#5000", [=] { position.scroll_to(5000); }),
                         Button("End", [=] { position.scroll_to_end(); }))
                      .button_style(BorderedButtonStyle{}),
                  // Only the rows near the visible part are built, however long the list is.
                  List({.position = position},
                       ForEach(shown, &Contact::id, [](const Contact& contact) { return ContactRow(contact); })))
        .padding(20)
        .frame({.max_width = infinity, .max_height = infinity})
        .background(ThemeColor::background)
        .theme(palette::theme)
        .color_scheme(scheme);
}
