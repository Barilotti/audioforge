#pragma once

#include <memory>
#include <utility>

namespace af::model
{

/// Model nodes are immutable and shared (architecture v0.2, section 1.4). An edit copies only
/// the changed node and its ancestors; untouched subtrees are shared between document versions.
template <typename T>
using NodePtr = std::shared_ptr<const T>;

/// Creates a new immutable node.
template <typename T>
[[nodiscard]] NodePtr<T> makeNode(T value)
{
    return std::make_shared<const T>(std::move(value));
}

/// Returns a new node equal to `node` with `change` applied to a copy; `node` stays unchanged.
///     auto louder = modified(clip, [](Clip& c) { c.gainDb += 3.0; });
template <typename T, typename Change>
[[nodiscard]] NodePtr<T> modified(const NodePtr<T>& node, Change&& change)
{
    T copy = *node;
    std::forward<Change>(change)(copy);
    return makeNode(std::move(copy));
}

} // namespace af::model
