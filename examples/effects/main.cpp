// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Effects: materials (background blur), blur, color adjustments, blend modes, shadows (layered,
// spread, inner), gradients, rotation and scale, and masks.

#include <elg/elegence.h>

#include <string>
#include <utility>
#include <vector>

using namespace elg;

// Something colorful to put effects over.
View Backdrop() {
    return ZStack(Rectangle().fill(AngularGradient({Color::hex(0xFF5F6D), Color::hex(0xFFC371), Color::hex(0x47CF73),
                                                     Color::hex(0x3EA6FF), Color::hex(0xA36BFF), Color::hex(0xFF5F6D)})),
                  Circle().fill(Color::white.opacity(0.85f)).frame(70, 70).offset(-90, -30),
                  Text("Elegence").font(Font::system(44, Weight::bold)).foreground_style(Color::black).offset(30, 30));
}

View Caption(View content, std::string label) {
    return VStack({.spacing = 6}, std::move(content),
                  Text(std::move(label)).font(Font::caption).foreground_style(ThemeColor::secondary));
}

View Section(std::string title, View content) {
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                  Text(std::move(title)).font(Font::headline), std::move(content));
}

View Materials() {
    const std::vector<std::pair<Material, std::string>> materials{
        {Material::ultra_thin, "ultra thin"}, {Material::thin, "thin"},       {Material::regular, "regular"},
        {Material::thick, "thick"},           {Material::ultra_thick, "ultra thick"},
    };
    std::vector<View> items;
    for (const auto& [material, name] : materials) {
        items.push_back(Text(name).font(Font::caption).padding(10).frame({.max_width = infinity, .max_height = infinity})
                            .background(material, RoundedRectangle(12)));
    }
    return Section("Materials (background blur)",
                   ZStack(Backdrop(),
                          HStack({.spacing = 8}, items[0], items[1], items[2], items[3], items[4]).padding(14))
                       .frame({.height = 140})
                       .corner_radius(16));
}

View Blur() {
    const State<float> radius(6.f);
    return Section("Blur",
                   VStack({.spacing = 8},
                          Backdrop().frame({.height = 110}).corner_radius(16).blur(radius),
                          HStack(Text("Radius"), Slider(radius.binding(), {.min = 0, .max = 30}))));
}

View Tile() {
    return ZStack(Circle().fill(LinearGradient({Color::hex(0xFF8A5B), Color::hex(0x7B5BFF)})),
                  Image("sparkle").font(Font::system(26)).foreground_style(Color::white))
        .frame(56, 56);
}

View ColorAdjustments() {
    return Section("Color adjustments",
                   HStack({.spacing = 12}, Caption(Tile(), "original"), Caption(Tile().grayscale(1), "grayscale"),
                          Caption(Tile().sepia(1), "sepia"), Caption(Tile().hue_rotation(120), "hue 120°"),
                          Caption(Tile().saturation(2.2f), "saturation"), Caption(Tile().contrast(1.8f), "contrast"),
                          Caption(Tile().brightness(0.25f), "brightness"), Caption(Tile().color_invert(), "invert"),
                          Caption(Tile().color_multiply(Color::hex(0x47CF73)), "multiply")));
}

View BlendSample(BlendMode mode, std::string name) {
    return Caption(ZStack(Rectangle().fill(LinearGradient({Color::hex(0x3EA6FF), Color::hex(0xFFC371)},
                                                          UnitPoint::leading, UnitPoint::trailing)),
                          Circle().fill(Color::hex(0xFF5F6D)).frame(38, 38).blend_mode(mode))
                       .frame(64, 52)
                       .corner_radius(10),
                   std::move(name));
}

View BlendModes() {
    return Section("Blend modes",
                   VStack({.spacing = 10},
                          HStack({.spacing = 10}, BlendSample(BlendMode::normal, "normal"),
                                 BlendSample(BlendMode::multiply, "multiply"), BlendSample(BlendMode::screen, "screen"),
                                 BlendSample(BlendMode::overlay, "overlay"), BlendSample(BlendMode::darken, "darken"),
                                 BlendSample(BlendMode::lighten, "lighten")),
                          HStack({.spacing = 10}, BlendSample(BlendMode::color_dodge, "dodge"),
                                 BlendSample(BlendMode::color_burn, "burn"),
                                 BlendSample(BlendMode::soft_light, "soft light"),
                                 BlendSample(BlendMode::difference, "difference"),
                                 BlendSample(BlendMode::hue, "hue"), BlendSample(BlendMode::luminosity, "luminosity"))));
}

View Shadows() {
    const auto card = [] { return RoundedRectangle(14).fill(Color::white).frame(84, 64); };
    return Section("Shadows",
                   HStack({.spacing = 22},
                          Caption(card()
                                      .shadow({.color = Color::black.opacity(0.18f), .radius = 2, .y = 1})
                                      .shadow({.color = Color::black.opacity(0.14f), .radius = 24, .y = 12}),
                                  "layered"),
                          Caption(card().shadow({.color = Color::black.opacity(0.3f), .radius = 12, .y = 14, .spread = -6}),
                                  "negative spread"),
                          Caption(card().shadow({.color = Color::hex(0x3EA6FF).opacity(0.9f), .radius = 18}), "glow"),
                          Caption(RoundedRectangle(14).fill(Color::hex(0xE9ECF2)).frame(84, 64).inner_shadow(),
                                  "inner shadow"),
                          Caption(Text("Pressed").font(Font::system(28, Weight::bold))
                                      .foreground_style(Color::hex(0xD6DAE3))
                                      .inner_shadow({.color = Color::black.opacity(0.5f), .radius = 3, .x = 1, .y = 2}),
                                  "inner shadow on text"))
                       .padding(18)
                       .background(Color::hex(0xF4F5F8), RoundedRectangle(16)));
}

View Gradients() {
    return Section("Gradients",
                   HStack({.spacing = 12},
                          Caption(Circle().fill(AngularGradient({Color::hex(0xFF0000), Color::hex(0xFFFF00),
                                                                 Color::hex(0x00FF00), Color::hex(0x00FFFF),
                                                                 Color::hex(0x0000FF), Color::hex(0xFF00FF),
                                                                 Color::hex(0xFF0000)}))
                                      .frame(64, 64),
                                  "angular"),
                          Caption(Capsule().fill(EllipticalGradient({Color::hex(0xFFE29F), Color::hex(0xFF719A)}))
                                      .frame(110, 64),
                                  "elliptical"),
                          Caption(RoundedRectangle(14)
                                      .fill(AngularGradient({Color::hex(0x7B5BFF), Color::hex(0x3EA6FF)},
                                                            UnitPoint::center, -90, 90))
                                      .frame(64, 64),
                                  "angular, half turn")));
}

View TransformsAndMasks() {
    const State<bool> turned(false);
    return Section(
        "Rotation, scale, masks",
        HStack({.spacing = 22},
               Caption(RoundedRectangle(12)
                           .fill(LinearGradient({Color::hex(0x47CF73), Color::hex(0x3EA6FF)}))
                           .overlay(Text("Tap").foreground_style(Color::white))
                           .frame(64, 64)
                           .rotation_effect([=] { return turned() ? 135.f : 15.f; })
                           .on_tap_gesture([=] { with_animation(Animation::spring(), [&] { turned = !turned(); }); }),
                       "rotation_effect"),
               Caption(Text("Wide").font(Font::system(28, Weight::bold)).scale_effect(1.6f, 0.8f), "scale_effect"),
               Caption(Backdrop()
                           .frame(150, 64)
                           .clipped()
                           .mask(LinearGradient({Color::black, Color::clear}, UnitPoint::leading, UnitPoint::trailing)),
                       "gradient mask"),
               Caption(Backdrop().frame(64, 64).clipped().mask(Image("heart").font(Font::system(60))), "icon mask"),
               Caption(Backdrop().frame(120, 64).clipped().compositing_group().opacity(0.4f), "group opacity")));
}

int main() {
    App app;
    app.window({.title = "Effects", .width = 1240, .height = 900}, [] {
        return ScrollView(HStack({.alignment = VerticalAlignment::top, .spacing = 32},
                                 VStack({.alignment = HorizontalAlignment::leading, .spacing = 26}, Materials(), Blur(),
                                        Gradients()),
                                 VStack({.alignment = HorizontalAlignment::leading, .spacing = 26}, ColorAdjustments(),
                                        BlendModes(), Shadows(), TransformsAndMasks()))
                              .padding(24));
    });
    return app.run();
}
