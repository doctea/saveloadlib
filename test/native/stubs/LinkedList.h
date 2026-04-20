#pragma once

#include <vector>
#include <cstddef>

template <typename T>
class LinkedList {
public:
    LinkedList() = default;

    int size() const { return static_cast<int>(items_.size()); }

    void add(const T& value) { items_.push_back(value); }

    T& get(int index) { return items_.at(static_cast<size_t>(index)); }
    const T& get(int index) const { return items_.at(static_cast<size_t>(index)); }

private:
    std::vector<T> items_;
};
