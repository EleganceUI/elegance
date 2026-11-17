// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What the test modules' root views and the host that shows them share: data the host owns,
// which the views reach through the environment, as an app's views reach its stores.

#ifndef ELG_TESTS_HOT_RELOAD_PROBE_H
#define ELG_TESTS_HOT_RELOAD_PROBE_H

#include <elg/environment.h>
#include <elg/reactive.h>
#include <elg/text.h>
#include <elg/views.h>

#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace elg_tests {

struct Probe {
    elg::State<int> host_count{0};      // the host's data
    std::vector<std::string> events;    // "<module> mounted", "<module> unmounted"
    std::function<void()> increment;    // adds 1 to the count of the root view mounted last
};

struct ProbeKey {
    using Value = std::shared_ptr<Probe>;
    static constexpr std::string_view name = "elg_tests.hot_reload_probe";
    static std::shared_ptr<Probe> default_value(const elg::EnvironmentValues&) { return nullptr; }
};

// A module's root view: "<module> <count>", with a count of its own. With a probe in the
// environment it also shows the host's count ("host <count>"), and records in the probe when it's
// mounted and unmounted.
struct ProbedRoot {
    std::string module;

    [[nodiscard]] elg::View body() const {
        const elg::State<int> count(0);
        const std::shared_ptr<Probe> probe = elg::Environment<ProbeKey>()();
        elg::View own = elg::Text([module = module, count] { return std::format("{} {}", module, count()); });
        if (!probe) {
            return own;
        }
        probe->events.push_back(module + " mounted");
        probe->increment = [count] { count += 1; };
        elg::detail::current_scope()->on_end([probe, module = module] { probe->events.push_back(module + " unmounted"); });
        return elg::VStack(own, elg::Text([probe] { return std::format("host {}", probe->host_count()); }));
    }
};

} // namespace elg_tests

#endif // ELG_TESTS_HOT_RELOAD_PROBE_H
