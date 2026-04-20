#pragma once

#include <functional>

namespace vl {

template <typename Signature>
class Func;

template <typename R, typename... Args>
class Func<R(Args...)> {
public:
    Func() = default;

    template <typename F>
    Func(F&& f) : fn_(std::forward<F>(f)) {}

    R operator()(Args... args) const {
        return fn_(args...);
    }

    explicit operator bool() const {
        return static_cast<bool>(fn_);
    }

private:
    std::function<R(Args...)> fn_;
};

} // namespace vl
