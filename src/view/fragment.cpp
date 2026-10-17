// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// If and ForEach: dynamic children (design.md §6, structural reactivity).

#include "view/view_node.h"

#include "core/log.h"

#include <unordered_map>
#include <unordered_set>

namespace elg::detail {

namespace {

bool same_value(const std::shared_ptr<const void>& old_value, const ForEachItem& item) {
    if (old_value == item.value) {
        return true; // same object, or both empty (If branches)
    }
    return old_value && item.value && item.equals && item.equals(old_value.get(), item.value.get());
}

} // namespace

// --- Lowering ---

std::unique_ptr<Element> IfNode::lower(const LowerContext& context) const {
    auto fragment = std::make_unique<FragmentElement>(context);
    FragmentElement* const target = fragment.get();
    // Each branch has a fixed key, so re-evaluating to the same branch keeps its views.
    detail::bind(condition_, context, [target, then_view = then_, else_view = else_](bool condition) {
        const View& branch = condition ? then_view : else_view;
        // An empty branch (View()) mounts nothing, so stacks don't space around it.
        if (!ViewAccess::node(branch)) {
            target->set_items({});
            return;
        }
        target->set_items({ForEachItem{static_cast<std::int64_t>(condition ? 1 : 0), nullptr, nullptr,
                                       [branch] { return branch; }}});
    });
    return fragment;
}

std::unique_ptr<Element> ForEachNode::lower(const LowerContext& context) const {
    auto fragment = std::make_unique<FragmentElement>(context);
    FragmentElement* const target = fragment.get();
    // Reading the items subscribes this effect; mounting the items doesn't (set_items untracks).
    effect([target, items = items_] { target->set_items(items()); });
    return fragment;
}

// --- FragmentElement ---

FragmentElement::Child FragmentElement::mount(const ForEachItem& item, const Animation* animation) {
    Child child{item.key, item.value, nullptr, nullptr, std::make_unique<Scope>()};
    Scope::Use use(*child.scope);
    // Building views may read state directly (e.g. in a component body); that must not subscribe
    // the If/ForEach effect running this.
    begin_untracked();
    try {
        child.element = lower(item.view(), context_);
    } catch (...) {
        end_untracked();
        throw;
    }
    end_untracked();

    // Every child gets a transition: its own `.transition(...)`, else a fade.
    child.transition = child.element->as_transition();
    if (!child.transition) {
        auto wrapper = std::make_unique<TransitionElement>(Transition::opacity(), std::move(child.element));
        child.transition = wrapper.get();
        child.element = std::move(wrapper);
    }
    adopt(*child.element);
    child.transition->insert(animation);
    return child;
}

void FragmentElement::set_items(const std::vector<ForEachItem>& items) {
    // Changes animate during animated updates; the first set (the initial mount) never does.
    const Animation* const animation = initialized_ ? change_animation() : nullptr;
    initialized_ = true;

    std::unordered_map<ItemKey, std::size_t> existing;
    for (std::size_t i = 0; i < children_.size(); ++i) {
        existing.emplace(children_[i].key, i);
    }

    std::vector<Child> next;
    next.reserve(items.size());
    std::vector<bool> kept(children_.size(), false);
    std::unordered_set<ItemKey> seen;
    bool reported_duplicate = false;
    bool changed = items.size() != children_.size();

    for (const ForEachItem& item : items) {
        if (!seen.insert(item.key).second) {
            if (!reported_duplicate) {
                log_warning("ForEach has duplicate keys; items with a repeated key are rebuilt on every change");
                reported_duplicate = true;
            }
        } else if (auto it = existing.find(item.key); it != existing.end() && !kept[it->second] &&
                                                    same_value(children_[it->second].value, item)) {
            kept[it->second] = true;
            changed |= it->second != next.size(); // moved
            next.push_back(std::move(children_[it->second]));
            continue;
        }
        changed = true;
        next.push_back(mount(item, animation));
    }

    // Children not kept either animate out (kept in exiting_ until done) or are destroyed here:
    // their scopes first (ending their effects), then their elements.
    for (std::size_t i = 0; i < children_.size(); ++i) {
        if (!kept[i] && children_[i].transition->remove(animation)) {
            exiting_.push_back(std::move(children_[i]));
        }
    }
    children_ = std::move(next);
    if (!changed) {
        return;
    }

    rebuild_children();
    invalidate_layout();
    notify_structure_changed();
    if (context_.needs_frame) {
        *context_.needs_frame = true;
    }
    if (context_.tree_changed) {
        *context_.tree_changed = true;
    }
}

void FragmentElement::rebuild_children() {
    laid_out_.clear();
    for (const Child& child : children_) {
        child.element->collect_layout_children(laid_out_);
    }
    handles_.clear();
    handles_.reserve(laid_out_.size());
    for (Element* element : laid_out_) {
        handles_.emplace_back(*element);
    }
}

void FragmentElement::on_structure_changed() {
    // A nested If/ForEach changed; this fragment's flattened children changed with it.
    rebuild_children();
    invalidate_layout();
    notify_structure_changed();
}

void FragmentElement::collect_layout_children(std::vector<Element*>& out) {
    out.insert(out.end(), laid_out_.begin(), laid_out_.end());
}

void FragmentElement::collect_exiting(std::vector<Element*>& out) {
    for (const Child& child : exiting_) {
        out.push_back(child.element.get());
    }
}

void FragmentElement::prune_exiting() {
    std::erase_if(exiting_, [](const Child& child) { return child.transition->exit_finished(); });
}

void FragmentElement::for_each_child(const std::function<void(Element&)>& visit) {
    for (const Child& child : children_) {
        visit(*child.element);
    }
}

// On its own (not directly inside a layout), a fragment stacks its children vertically.

Size FragmentElement::measure(ProposedSize proposal) {
    return standalone_layout_.size_that_fits(proposal, handles_);
}

void FragmentElement::arrange(Rect bounds, ProposedSize proposal) {
    prune_exiting();
    standalone_layout_.place_children(bounds, proposal, handles_);
}

void FragmentElement::paint_impl(DisplayList& list) const {
    for (const Element* element : laid_out_) {
        element->paint(list);
    }
    for (const Child& child : exiting_) {
        child.element->paint(list);
    }
}

bool FragmentElement::hit_impl(Point point, std::vector<Element*>& path) {
    for (auto it = laid_out_.rbegin(); it != laid_out_.rend(); ++it) {
        if ((*it)->hit_test(point, path)) {
            return true;
        }
    }
    return false;
}

} // namespace elg::detail
