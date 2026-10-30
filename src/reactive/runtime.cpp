// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The reactive graph runtime (design.md §6), in the style of the "Reactively" algorithm:
// writes push CHECK/DIRTY marks down the graph, reads pull: a node marked CHECK first brings
// its sources up to date and only re-runs if one of them actually changed.

#include <elg/reactive.h>

#include "reactive/runtime.h"

#include <elg/error.h>

#include <algorithm>
#include <deque>
#include <optional>
#include <thread>

namespace elg::detail {

namespace {

enum Status : int { clean = 0, check = 1, dirty = 2 };

// UI-thread state. The UI thread is fixed while an App runs; until then any thread may build
// views (e.g. tests).
ReactiveNode* g_observer = nullptr;
// Observers hidden by untrack(); restored when it ends.
std::vector<ReactiveNode*> g_hidden_observers;
Scope* g_scope = nullptr;
int g_batch_depth = 0;
// Effects waiting to run, in order. Disposed nodes remove themselves, so the queue never holds
// a dangling pointer, even when running one effect disposes others (e.g. If unmounting a
// branch).
std::deque<ReactiveNode*> g_pending;
std::optional<std::thread::id> g_ui_thread;

// Animated transactions (design.md §9.5).
std::shared_ptr<const Animation> g_transaction;      // with_animation in progress
const Animation* g_running_animation = nullptr;      // the transaction of the effect running now
std::shared_ptr<const Animation> g_flush_animation;  // latest transaction run in a flush
std::uint64_t g_flush = 0;

// Effect runs within one flush; beyond this, effects are almost certainly re-triggering each
// other in a cycle.
constexpr int kMaxEffectRuns = 100'000;

void remove_one(std::vector<ReactiveNode*>& nodes, ReactiveNode* node) {
    if (auto it = std::ranges::find(nodes, node); it != nodes.end()) {
        *it = nodes.back();
        nodes.pop_back();
    }
}

} // namespace

struct Runtime {
    static void dequeue(ReactiveNode* node) noexcept { node->queued_ = false; }
    static std::shared_ptr<const Animation> take_transaction(ReactiveNode* node) noexcept {
        return std::exchange(node->transaction_, nullptr);
    }
    static void set_priority(ReactiveNode* node) noexcept { node->priority_ = true; }

    static void mark(ReactiveNode* node, int status) {
        if (node->status_ >= status || node->disposed_) {
            return;
        }
        const bool was_clean = node->status_ == clean;
        node->status_ = status;
        if (node->kind_ == ReactiveNode::Kind::effect) {
            if (g_transaction) {
                node->transaction_ = g_transaction;
            }
            if (!node->queued_) {
                node->queued_ = true;
                if (node->priority_) {
                    g_pending.push_front(node);
                } else {
                    g_pending.push_back(node);
                }
            }
        }
        if (was_clean) {
            for (ReactiveNode* observer : node->observers_) {
                mark(observer, check);
            }
        }
    }

    static void unlink_sources(ReactiveNode* node) {
        for (ReactiveNode* source : node->sources_) {
            remove_one(source->observers_, node);
        }
        node->sources_.clear();
    }

    static void run(ReactiveNode* node) {
        unlink_sources(node); // dependencies are re-tracked on every run
        ReactiveNode* const previous = g_observer;
        g_observer = node;
        bool changed = false;
        try {
            changed = node->compute();
        } catch (...) {
            g_observer = previous;
            node->status_ = clean;
            throw;
        }
        g_observer = previous;
        if (changed && node->kind_ == ReactiveNode::Kind::computed) {
            for (ReactiveNode* observer : node->observers_) {
                observer->status_ = dirty;
            }
        }
    }
};

// --- ReactiveNode ---

ReactiveNode::ReactiveNode(Kind kind) noexcept : kind_(kind), status_(kind == Kind::signal ? clean : dirty) {}

ReactiveNode::~ReactiveNode() {
    dispose();
}

void ReactiveNode::track() {
    if (kind_ != Kind::signal) {
        update();
    }
    ReactiveNode* const observer = g_observer;
    if (!observer || observer == this) {
        return;
    }
    if (std::ranges::find(observer->sources_, this) == observer->sources_.end()) {
        observer->sources_.push_back(this);
        observers_.push_back(observer);
    }
}

void ReactiveNode::notify_changed() {
    for (std::size_t i = 0; i < observers_.size(); ++i) {
        Runtime::mark(observers_[i], dirty);
    }
}

void ReactiveNode::update() {
    if (disposed_) {
        return;
    }
    if (status_ == check) {
        // Bring sources up to date; one that changed marks this node dirty.
        for (std::size_t i = 0; i < sources_.size() && status_ != dirty; ++i) {
            sources_[i]->update();
        }
    }
    if (status_ == dirty) {
        Runtime::run(this);
    }
    status_ = clean;
}

void ReactiveNode::dispose() noexcept {
    if (disposed_) {
        return;
    }
    disposed_ = true;
    Runtime::unlink_sources(this);
    for (ReactiveNode* observer : observers_) {
        remove_one(observer->sources_, this);
    }
    observers_.clear();
    if (queued_) {
        std::erase(g_pending, this);
        queued_ = false;
    }
}

// --- Scope ---

Scope::~Scope() {
    for (auto& node : nodes_) {
        node->dispose();
    }
    for (auto it = cleanups_.rbegin(); it != cleanups_.rend(); ++it) {
        (*it)();
    }
}

void Scope::adopt(std::shared_ptr<ReactiveNode> node) {
    nodes_.push_back(std::move(node));
}

void Scope::on_end(std::function<void()> cleanup) {
    cleanups_.push_back(std::move(cleanup));
}

Scope::Use::Use(Scope& scope) noexcept : previous_(g_scope) {
    g_scope = &scope;
}

Scope::Use::~Use() {
    g_scope = previous_;
}

Scope* current_scope() noexcept {
    return g_scope;
}

// --- Free functions ---

void start_effect(std::shared_ptr<EffectNode> effect, bool priority) {
    check_ui_thread();
    if (priority) {
        Runtime::set_priority(effect.get());
    }
    if (!g_scope) {
        throw UsageError("effect() must be called while building views (in a body or a reactive "
                         "argument), so it has an owner that ends it");
    }
    EffectNode* const node = effect.get();
    g_scope->adopt(std::move(effect));
    node->update(); // first run, establishing its dependencies
}

void check_ui_thread() {
    if (g_ui_thread && *g_ui_thread != std::this_thread::get_id()) {
        throw UsageError("reactive state was used off the UI thread; state belongs to the UI thread "
                         "(design.md §6)");
    }
}

void begin_batch() noexcept {
    ++g_batch_depth;
}

void end_batch(bool flush) {
    if (--g_batch_depth == 0 && flush) {
        flush_effects();
    }
}

// Untracked code runs with no current observer. Effects created inside it still track their own
// reads, since each effect makes itself the observer while it runs.
void begin_untracked() noexcept {
    g_hidden_observers.push_back(g_observer);
    g_observer = nullptr;
}

void end_untracked() noexcept {
    g_observer = g_hidden_observers.back();
    g_hidden_observers.pop_back();
}

void flush_effects() {
    if (g_batch_depth > 0) {
        return;
    }
    ++g_flush;
    for (int runs = 0; !g_pending.empty(); ++runs) {
        if (runs >= kMaxEffectRuns) {
            for (ReactiveNode* node : g_pending) {
                Runtime::dequeue(node);
            }
            g_pending.clear();
            throw UsageError("effects keep re-triggering each other; an effect probably writes state "
                             "it also reads");
        }
        ReactiveNode* const node = g_pending.front();
        g_pending.pop_front();
        Runtime::dequeue(node);
        // Run inside the transaction that scheduled it, so animatable properties it sets animate.
        const std::shared_ptr<const Animation> transaction = Runtime::take_transaction(node);
        if (transaction) {
            g_flush_animation = transaction;
        }
        const Animation* const previous = std::exchange(g_running_animation, transaction.get());
        try {
            node->update();
        } catch (...) {
            g_running_animation = previous;
            throw;
        }
        g_running_animation = previous;
    }
}

void begin_transaction(std::shared_ptr<const Animation> animation) {
    check_ui_thread();
    g_transaction = std::move(animation);
}

void end_transaction() noexcept {
    g_transaction = nullptr;
}

const Animation* current_animation() noexcept {
    return g_running_animation;
}

std::uint64_t current_flush() noexcept {
    return g_flush;
}

std::shared_ptr<const Animation> take_flush_animation() noexcept {
    return std::exchange(g_flush_animation, nullptr);
}

void set_ui_thread(std::optional<std::thread::id> thread) noexcept {
    g_ui_thread = thread;
}

} // namespace elg::detail
