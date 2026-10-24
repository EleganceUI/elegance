// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Grids: a VGrid of cards that fits as many columns as the window allows, and a Grid table with
// columns sized to their content, a spanning cell, and a full-width divider.

#include <elg/elegence.h>

#include <format>
#include <string>
#include <vector>

using namespace elg;

struct Game {
    int id = 0;
    std::string name;
    int mods = 0;
    float size_gb = 0;

    friend bool operator==(const Game&, const Game&) = default;
};

View Card(const Game& game) {
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 4},
                  Text(game.name).font(Font::headline),
                  Text(std::format("{} mods", game.mods)).foreground_style(ThemeColor::secondary))
        .padding(14)
        .frame({.max_width = infinity, .alignment = Alignment::leading})
        .background(ThemeColor::surface, RoundedRectangle(10));
}

View GridsApp() {
    const std::vector<Game> games{{1, "Skyrim", 118, 12.4f},       {2, "Minecraft", 42, 1.1f},
                                  {3, "Stardew Valley", 9, 0.6f},  {4, "Fallout 4", 77, 31.0f},
                                  {5, "Terraria", 15, 0.4f},       {6, "Cyberpunk 2077", 23, 70.2f}};

    const View cards = VGrid({.columns = {GridItem::adaptive(150)}, .spacing = 12},
                             ForEach(games, &Game::id, Card));

    const View table = Grid(
        {.alignment = Alignment::leading, .horizontal_spacing = 24, .vertical_spacing = 8},
        GridRow(Text("Game").font(Font::headline), Text("Mods").font(Font::headline),
                Text("Size").font(Font::headline).grid_column_alignment(HorizontalAlignment::trailing)),
        Divider(),
        ForEach(games, &Game::id,
                [](const Game& game) {
                    return GridRow(Text(game.name), Text(std::format("{}", game.mods)),
                                   Text(std::format("{:.1f} GB", game.size_gb)));
                }),
        Divider(),
        GridRow(Text("Sizes include downloaded mods.").foreground_style(ThemeColor::secondary).grid_cell_columns(3)));

    return ScrollView(VStack({.alignment = HorizontalAlignment::leading, .spacing = 24},
                             Text("Library").font(Font::large_title), cards,
                             Text("Details").font(Font::title2), table)
                          .padding(24));
}

int main() {
    App app;
    app.window({.title = "Grids", .width = 640, .height = 640}, GridsApp);
    return app.run();
}
