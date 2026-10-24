// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// HTTP and AsyncImage (design.md §13.3). Results arrive on a running App's UI thread, so these
// check what doesn't need one; the sandbox's Network cards exercise real requests.

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

using namespace elg;

TEST_CASE("responses find headers by name, ignoring case", "[http]") {
    http::Response response;
    response.status = 204;
    response.headers = {{"Content-Type", "image/png"}, {"X-Count", "3"}, {"x-count", "4"}};
    CHECK(response.ok());
    CHECK(response.header("content-type") == "image/png");
    CHECK(response.header("X-COUNT") == "3"); // the first one
    CHECK(response.header("missing").empty());
    response.status = 404;
    CHECK_FALSE(response.ok());
}

TEST_CASE("requests can be cancelled", "[http]") {
    CHECK_NOTHROW(http::Task().cancel()); // an empty task
    bool called = false;
    const http::Task task = http::fetch({.url = "https://example.invalid/"}, [&](const http::Result&) { called = true; });
    task.cancel();
    task.cancel(); // twice is fine
    CHECK_FALSE(called);
}

TEST_CASE("AsyncImage builds", "[http][image]") {
    CHECK_NOTHROW(AsyncImage("https://example.com/a.png"));
    CHECK_NOTHROW(AsyncImage("https://example.com/a.png", {.scale = 2, .content_mode = ContentMode::fill}).frame(40, 40));
    CHECK_NOTHROW(AsyncImage("https://example.com/a.png", [](const AsyncImagePhase& phase) {
        return phase.kind == AsyncImagePhase::Kind::success ? phase.image.resizable() : Text("…");
    }));
    CHECK_THROWS_AS(AsyncImage("https://example.com/a.png", std::function<View(const AsyncImagePhase&)>()), UsageError);
}
