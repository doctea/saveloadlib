
// CURRENTLY UNUSED — may be useful for future refactorings, but not worth the maintenance cost right now.

// sl_func.h — inline SBO callable wrapper; zero heap allocations
//
// SL_Func<Signature, BufSize = 16> stores any callable (lambda, functor,
// function pointer) entirely inside a fixed-size inline buffer.  No heap
// allocation ever occurs — in contrast to vl::Func which calls `new` for
// both the Invoker object and the Ptr<> refcount every time a lambda is used.
//
// When LSaveableSetting objects are placed in the arena (via the
// SaveableSettingBase::operator new override), their SL_Func<> members live
// there too. The net result: zero external allocations per setting.
//
// ---------------------------------------------------------------------------
// BufSize selection
// ---------------------------------------------------------------------------
// The buffer must be >= sizeof(your lambda's capture group), rounded up to
// alignment.  On 32-bit ARM (Teensy 4.x):
//   - Lambda capturing `this`              = 4 bytes  → fits in BufSize 16
//   - Lambda capturing `this` + 1 ptr      = 8 bytes  → fits in BufSize 16
//   - Lambda capturing `this` + 3 ptrs     = 16 bytes → fits in BufSize 16
//   - Lambda capturing `this` + 4 ptrs     = 20 bytes → needs BufSize 24
//
// If a callable is too large, a static_assert fires at the offending
// call site — increase the BufSize template parameter:
//   LSaveableSetting<int, 24>  (third template arg)
//
// ---------------------------------------------------------------------------
// Backward compatibility
// ---------------------------------------------------------------------------
// LSaveableSetting's constructor previously accepted vl::Func<> values.
// sizeof(vl::Func<R(A)>) == sizeof(Ptr<Invoker>) ≈ 12 bytes on 32-bit ARM,
// so those still fit in BufSize 16 and are transparently stored inline.
// Calling an SL_Func that wraps a vl::Func just dispatches through the
// already-heap-allocated invoker — no new allocation at call time.
//
// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------
// * Copy/move copy the callable bytes; destroy calls the destructor.
// * Not thread-safe. Only call during setup(), not from ISRs.
// * Plain function pointers (R(*)(Args...)) are stored as-is in the buffer.

#pragma once
#include <stddef.h>
// placement new  — most Arduino/arm-none-eabi toolchains provide it without a
// header, but include <new> defensively if available.
#if defined(__has_include)
  #if __has_include(<new>)
    #include <new>
  #endif
#endif

// ---------------------------------------------------------------------------
// Minimal type trait — avoids pulling in <type_traits>
// ---------------------------------------------------------------------------
template<typename T> struct sl_remove_ref         { typedef T Type; };
template<typename T> struct sl_remove_ref<T&>     { typedef T Type; };
template<typename T> struct sl_remove_ref<T&&>    { typedef T Type; };

// ---------------------------------------------------------------------------
// SL_Func<Sig, BufSize>
// ---------------------------------------------------------------------------
template<typename Sig, size_t BufSize = 128>
class SL_Func;  // primary template — only the specialisation below is used

template<typename R, typename... Args, size_t BufSize>
class SL_Func<R(Args...), BufSize> {
    // Three type-erased function pointers to vtable-level operations.
    using invoke_fn_t  = R   (*)(const char* buf, Args...);
    using destroy_fn_t = void (*)(char* buf);
    using copy_fn_t    = void (*)(char* dst, const char* src);

    alignas(double) char  buf_[BufSize];  // inline storage for the callable
    invoke_fn_t           invoke_  = nullptr;
    destroy_fn_t          destroy_ = nullptr;
    copy_fn_t             copy_    = nullptr;

    // --- per-type erased helpers, instantiated once per callable type C ---
    template<typename C>
    static R    do_invoke (const char* buf, Args... args) {
        return (*reinterpret_cast<const C*>(buf))(args...);
    }
    template<typename C>
    static void do_destroy(char* buf) {
        reinterpret_cast<C*>(buf)->~C();
    }
    template<typename C>
    static void do_copy(char* dst, const char* src) {
        ::new (static_cast<void*>(dst)) C(*reinterpret_cast<const C*>(src));
    }

    void clear() {
        if (invoke_) {
            destroy_(buf_);
            invoke_  = nullptr;
            destroy_ = nullptr;
            copy_    = nullptr;
        }
    }
    void copy_from(const SL_Func& o) {
        invoke_  = o.invoke_;
        destroy_ = o.destroy_;
        copy_    = o.copy_;
        if (invoke_) copy_(buf_, o.buf_);
    }

public:
    // Default: empty (operator bool returns false)
    SL_Func() { }

    // Construct from any callable except another SL_Func (those are handled
    // by the explicit copy/move constructors below, which take precedence).
    template<typename C>
    SL_Func(C&& callable) {
        using Bare = typename sl_remove_ref<C>::Type;
        static_assert(sizeof(Bare) <= BufSize,
            "SL_Func: callable captures too much state for the inline buffer — "
            "increase the BufSize template parameter (e.g. LSaveableSetting<int, 24>)");
        ::new (static_cast<void*>(buf_)) Bare(static_cast<C&&>(callable));
        invoke_  = &do_invoke <Bare>;
        destroy_ = &do_destroy<Bare>;
        copy_    = &do_copy   <Bare>;
    }

    // Copy / move
    SL_Func(const SL_Func& o)  { copy_from(o); }
    SL_Func(SL_Func&& o)       { copy_from(o); o.clear(); }

    SL_Func& operator=(const SL_Func& o) {
        if (this != &o) { clear(); copy_from(o); }
        return *this;
    }
    SL_Func& operator=(SL_Func&& o) {
        if (this != &o) { clear(); copy_from(o); o.clear(); }
        return *this;
    }
    // Assign directly from any callable
    template<typename C>
    SL_Func& operator=(C&& callable) {
        clear();
        *this = SL_Func<R(Args...), BufSize>(static_cast<C&&>(callable));
        return *this;
    }

    ~SL_Func() { clear(); }

    R operator()(Args... args) const { return invoke_(buf_, args...); }

    explicit operator bool() const { return invoke_ != nullptr; }
};
