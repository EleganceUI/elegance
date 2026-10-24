// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Login example: a modern web-style sign-in screen. A credentials form on the left, a QR code
// to scan with a phone on the right, a gradient backdrop, and a success screen after logging in.
// Uses only the public API.
//
//   login                  open the window
//   login --smoke-test     render the first frame, then exit

#include "qr_code.h"

#include <elg/elegance.h>

#include <cctype>
#include <cstdio>
#include <format>
#include <random>
#include <string>
#include <string_view>

namespace {

using namespace elg;

// --- Look ---

namespace colors {
inline constexpr Color backdrop_top = Color::hex(0x5865F2);
inline constexpr Color backdrop_bottom = Color::hex(0x2B2D6E);
inline constexpr Color card = Color::hex(0x313338);
inline constexpr Color input = Color::hex(0x1E1F22);
inline constexpr Color heading = Color::hex(0xF2F3F5);
inline constexpr Color text = Color::hex(0xDBDEE1);
inline constexpr Color muted = Color::hex(0xB5BAC1);
inline constexpr Color faint = Color::hex(0x949BA4);
inline constexpr Color link = Color::hex(0x00A8FC);
inline constexpr Color brand = Color::hex(0x5865F2);
inline constexpr Color brand_pressed = Color::hex(0x4752C4);
inline constexpr Color error = Color::hex(0xFA777C);
inline constexpr Color success = Color::hex(0x23A55A);
} // namespace colors

// Dark only, so the theme's tokens (placeholder text, focus tint, ...) match the card.
constexpr ColorTokens kTokens{
    .background = colors::backdrop_bottom,
    .surface = colors::card,
    .surface_raised = colors::input,
    .outline = Color::hex(0x3F4147),
    .primary = colors::text,
    .secondary = colors::muted,
    .tertiary = Color::hex(0x6D6F78),
    .accent = colors::brand,
    .on_accent = Color::white,
    .danger = colors::error,
    .success = colors::success,
    .warning = Color::hex(0xF0B232),
};
constexpr Theme kTheme{.light = kTokens, .dark = kTokens};

// Flat dark inputs that light up in the link color while focused.
struct InputStyle : TextFieldStyle {
    View make_body(const TextFieldConfiguration& field) const override {
        return field.field.font(Font::system(16))
            .foreground_style(colors::heading)
            .padding(EdgeInsets::symmetric(10, 10))
            .frame({.max_width = infinity})
            .background(colors::input, RoundedRectangle(4))
            .overlay(RoundedRectangle(4)
                         .stroke_border(colors::link, 1)
                         .opacity([=] { return field.is_focused() ? 1.f : 0.f; })
                         .animation(Animation::ease_out(0.15), field.is_focused));
    }
};

// The full-width brand-colored button.
struct PrimaryStyle : ButtonStyle {
    View make_body(const ButtonConfiguration& button) const override {
        return button.label.font(Font::system(16, Weight::medium))
            .foreground_style(Color::white)
            .frame({.max_width = infinity, .min_height = 44})
            .background(
                [=] {
                    if (!button.is_enabled()) {
                        return colors::brand.opacity(0.5f);
                    }
                    return button.is_pressed() || button.is_hovered() ? colors::brand_pressed : colors::brand;
                },
                RoundedRectangle(4))
            .animation(Animation::ease_out(0.12), button.is_hovered);
    }
};

// Text links: the link color, brighter while hovered.
struct LinkStyle : ButtonStyle {
    View make_body(const ButtonConfiguration& button) const override {
        return button.label.font(Font::system(14, Weight::medium)).foreground_style([=] {
            if (button.is_pressed()) {
                return colors::link.opacity(0.7f);
            }
            return button.is_hovered() ? colors::link.mix(Color::white, 0.3f) : colors::link;
        });
    }
};

// "EMAIL *", or in red with the problem after a failed attempt.
View FieldLabel(std::string name, State<std::string> problem) {
    return Text([=] {
               const std::string& message = problem();
               return message.empty() ? name : std::format("{} - {}", name, message);
           })
        .font(Font::system(12, Weight::bold))
        .foreground_style([=] { return problem().empty() ? colors::muted : colors::error; });
}

// --- The two panels ---

struct Form {
    State<std::string> email{std::string()};
    State<std::string> password{std::string()};
    State<std::string> email_problem{std::string()};
    State<std::string> password_problem{std::string()};
    State<bool> password_focused{false};
};

constexpr float kFormWidth = 416;
constexpr float kGap = 64;
constexpr float kQrWidth = 240;

View CredentialsPanel(Form form, std::function<void()> log_in) {
    const View header = VStack({.spacing = 8},
                               Text("Welcome back!").font(Font::system(24, Weight::semibold)).foreground_style(colors::heading),
                               Text("We're so excited to see you again!").font(Font::system(16)).foreground_style(colors::muted));

    const View email = VStack({.alignment = HorizontalAlignment::leading, .spacing = 8},
                              FieldLabel("EMAIL OR PHONE NUMBER", form.email_problem),
                              // Enter moves on to the password.
                              TextField("", form.email.binding(), {.on_submit = [=] { form.password_focused = true; }}));

    const View password = VStack({.alignment = HorizontalAlignment::leading, .spacing = 8},
                                 FieldLabel("PASSWORD", form.password_problem),
                                 SecureField("", form.password.binding(), {.on_submit = log_in})
                                     .focused(form.password_focused.binding()),
                                 Button("Forgot your password?", [] { std::puts("login: forgot password"); })
                                     .button_style(LinkStyle{}));

    const View footer = HStack({.spacing = 4},
                               Text("Need an account?").font(Font::system(14)).foreground_style(colors::faint),
                               Button("Register", [] { std::puts("login: register"); }).button_style(LinkStyle{}));

    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 20},
                  header.frame({.max_width = infinity}),
                  email,
                  password,
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 8},
                         Button("Log In", log_in).button_style(PrimaryStyle{}),
                         footer))
        .text_field_style(InputStyle{});
}

// A one-time login token as a QR code, with the app's mark in the middle.
View QrPanel(const std::string& token) {
    const View mark = Text("E")
                          .font(Font::system(22, Weight::bold))
                          .foreground_style(Color::white)
                          .frame(40, 40)
                          .background(colors::brand, RoundedRectangle(10))
                          .padding(4)
                          .background(Color::white, RoundedRectangle(13));

    // The QR code's quiet zone is the white padding around it.
    const View code = ZStack(QrCode(std::format("https://example.com/ra/{}", token), Color::hex(0x060607))
                                 .frame(160, 160),
                             mark)
                          .padding(8)
                          .background(Color::white, RoundedRectangle(6));

    return VStack({.spacing = 0},
                  code,
                  Text("Log in with QR Code")
                      .font(Font::system(24, Weight::semibold))
                      .foreground_style(colors::heading)
                      .padding(Edges::top, 32),
                  Text("Scan this with the mobile app to log in instantly.")
                      .font(Font::system(16))
                      .foreground_style(colors::muted)
                      .multiline_text_alignment(TextAlignment::center)
                      .padding(Edges::top, 8))
        .frame({.width = kQrWidth});
}

View SignedIn(Form form, std::function<void()> log_out) {
    // An avatar with the account's initial, and a green "online" dot.
    const View avatar =
        ZStack({.alignment = Alignment::bottom_trailing},
               Circle()
                   .fill(colors::brand)
                   .frame(72, 72)
                   .overlay(Text([=] {
                                const std::string& email = form.email();
                                if (email.empty()) {
                                    return std::string("?");
                                }
                                return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(email.front()))));
                            })
                                .font(Font::system(30, Weight::semibold))
                                .foreground_style(Color::white)),
               Circle().fill(colors::success).frame(16, 16).padding(3).background(colors::card, Circle()));
    return VStack({.spacing = 12},
                  avatar,
                  Text("You're in!").font(Font::system(24, Weight::semibold)).foreground_style(colors::heading),
                  Text([=] { return std::format("Logged in as {}", form.email()); })
                      .font(Font::system(16))
                      .foreground_style(colors::muted),
                  Button("Log out", std::move(log_out)).button_style(LinkStyle{}).padding(Edges::top, 8))
        .frame({.width = kFormWidth + kGap + kQrWidth}) // the login card's size
        .frame({.min_height = 340});
}

std::string random_token() {
    std::random_device device;
    std::mt19937_64 generator(device());
    return std::format("{:016x}{:016x}", generator(), generator());
}

View LoginScreen() {
    const Form form;
    const auto signed_in = State(false);
    const std::string token = random_token();

    const auto log_in = [=] {
        // Every problem at once, like the web form it imitates.
        form.email_problem = form.email().empty() ? std::string("This field is required") : std::string();
        form.password_problem = form.password().empty() ? std::string("This field is required") : std::string();
        if (form.email_problem().empty() && form.password_problem().empty()) {
            with_animation(Animation::spring(0.45, 0.85), [=] { signed_in = true; });
        }
    };
    const auto log_out = [=] {
        with_animation(Animation::spring(0.45, 0.85), [=] {
            signed_in = false;
            form.password = std::string();
        });
    };

    const View login = HStack({.alignment = VerticalAlignment::center, .spacing = kGap},
                              CredentialsPanel(form, log_in).frame({.width = kFormWidth}),
                              QrPanel(token));

    const View card = If(signed_in,
                         SignedIn(form, log_out).transition(Transition::opacity().combined(Transition::scale(0.9f))),
                         login.transition(Transition::opacity()))
                          .padding(32)
                          .background(colors::card, RoundedRectangle(8))
                          .shadow({.color = Color::black.opacity(0.35f), .radius = 24, .y = 8});

    // A soft gradient backdrop with a couple of glows, like a marketing page.
    const View backdrop = ZStack(Rectangle().fill(LinearGradient({colors::backdrop_top, colors::backdrop_bottom},
                                                                 UnitPoint::top_leading, UnitPoint::bottom_trailing)),
                                 Circle()
                                     .fill(RadialGradient({Color::hex(0xEB459E).opacity(0.45f), Color::hex(0xEB459E).opacity(0)},
                                                          UnitPoint::center, 0, 260))
                                     .frame(520, 520)
                                     .offset(-380, -220),
                                 Circle()
                                     .fill(RadialGradient({Color::hex(0x57F287).opacity(0.25f), Color::hex(0x57F287).opacity(0)},
                                                          UnitPoint::center, 0, 300))
                                     .frame(600, 600)
                                     .offset(420, 260));

    // The window has no system title bar (TitleBar::hidden): this one floats over the backdrop.
    // Dragging it moves the window; the caption buttons still click.
    const View title_bar = HStack({.spacing = 0},
                                  Text("Elegance")
                                      .font(Font::system(12, Weight::semibold))
                                      .foreground_style(Color::white.opacity(0.7f))
                                      .padding(Edges::leading, 14),
                                  Spacer(),
                                  WindowControls())
                               .frame({.max_width = infinity, .min_height = 32})
                               .window_drag_area();

    return ZStack(backdrop, card)
        .frame({.max_width = infinity, .max_height = infinity})
        .overlay(title_bar, Alignment::top)
        .clipped()
        .theme(kTheme)
        .color_scheme(ColorScheme::dark)
        .tint(colors::link);
}

} // namespace

int main(int argc, char** argv) {
    bool smoke_test = false;
    for (int i = 1; i < argc; ++i) {
        smoke_test |= std::string_view(argv[i]) == "--smoke-test";
    }
    try {
        App app;
        app.window({.title = "Elegance Login",
                    .width = 1080,
                    .height = 700,
                    .min_size = Size{900, 600},
                    .title_bar = TitleBar::hidden},
                   [&] {
            if (smoke_test) {
                app.quit(); // still renders the first frame
            }
            return LoginScreen();
        });
        return app.run();
    } catch (const elg::Error& e) {
        std::fprintf(stderr, "login: %s\n", e.what());
        return 1;
    }
}
