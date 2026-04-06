// sl_arena.h — bump-pointer arena allocator for SaveableSettingBase objects
//
// ---------------------------------------------------------------------------
// Problem
// ---------------------------------------------------------------------------
// On Teensy with EXTMEM, each individual `new T(...)` call walks the EXTMEM
// heap free-list over slow PSRAM — ~26 s at startup for ~850 settings.
//
// On RP2350/RP2040 and similar the default heap is fast enough that this is
// not an issue, but a single large allocation still reduces fragmentation.
//
// ---------------------------------------------------------------------------
// Solution: transparent operator new/delete override
// ---------------------------------------------------------------------------
// SaveableSettingBase (defined in saveloadlib.h, which includes this header)
// overrides operator new/delete.  When a global arena is registered with
// sl_set_setting_arena() ALL `new LSaveableSetting<>()`,
// `new SaveableSetting<>()`, etc. automatically use bump allocation from
// that arena — no changes needed to setup_saveable_settings() code.
//
// If no arena is registered the override falls back to ::operator new/delete
// so the library works out of the box on any platform.
//
// ---------------------------------------------------------------------------
// Platform-specific patterns
// ---------------------------------------------------------------------------
//
// ── RP2040/RP2350 / desktop (fast heap, no special memory) ──────────────────
//
//   // In your main .cpp/.ino, before sl_setup_all():
//   static SL_Arena<20000> sl_arena;    // storage inline, lives in BSS/SRAM
//   // sl_set_setting_arena(&sl_arena); // optional on fast platforms
//
// ── Teensy 4.x with EXTMEM (slow heap, ~26 s without arena) ─────────────────
//
//   The critical trick: keep the arena *metadata* (just buf+capacity+used,
//   ~12 bytes) in fast DTCM so each bump allocation only touches fast RAM
//   for the bookkeeping, while the actual object data lives in EXTMEM.
//
//   EXTMEM static char sl_pool[20000]; // buffer in EXTMEM (one slow malloc)
//   static SL_ArenaBase sl_arena(sl_pool, sizeof(sl_pool)); // metadata in DTCM
//   // Then before sl_setup_all():
//   sl_set_setting_arena(&sl_arena);
//
//   Result: 850 heap scans → 1 heap scan + 850 fast pointer increments.
//
// ── If DTCM is also scarce: use SL_Arena<N> in EXTMEM (still a big win) ────
//
//   EXTMEM static SL_Arena<20000> sl_arena; // everything in EXTMEM
//   sl_set_setting_arena(&sl_arena);
//
//   Even here: one slow heap scan instead of 850.  The bump-pointer bookkeeping
//   accesses sl_arena.used over slow PSRAM but that's only ~850 small reads/
//   writes versus 850 full free-list traversals.
//
// ---------------------------------------------------------------------------
// Sizing
// ---------------------------------------------------------------------------
//   sl_arena_size_for<T>(N)  — constexpr bytes for N objects of type T,
//   accounting for alignment padding.  Add together for a mixed arena:
//
//   EXTMEM static char sl_pool[
//       sl_arena_size_for<LSaveableSetting<int>>(200) +
//       sl_arena_size_for<LSaveableSetting<float>>(150) +
//       sl_arena_size_for<LSaveableSetting<bool>>(100)];
//
//   Print actual usage after setup to tune the size:
//   Serial.printf("Arena: %u / %u bytes\n",
//                 sl_arena.bytes_used(), sl_arena.capacity());
//
// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------
// * Objects are NOT destructed. For permanent settings this is intentional.
// * operator delete is a no-op for arena-owned pointers (checked by address
//   range); heap-allocated pointers fall through to ::operator delete.
// * The arena is NOT thread-safe. Call sl_set_setting_arena / setup_saveable_
//   settings only from setup(), not from ISRs or multiple threads.

#pragma once
#include <stddef.h>

// ---------------------------------------------------------------------------
// Compile-time size helper
// ---------------------------------------------------------------------------
template<typename T>
constexpr size_t sl_arena_size_for(size_t n) {
    constexpr size_t align  = alignof(T);
    constexpr size_t padded = (sizeof(T) + align - 1) & ~(align - 1);
    return padded * n;
}

// ---------------------------------------------------------------------------
// SL_ArenaBase — type-erased bump allocator over an external byte buffer
// ---------------------------------------------------------------------------
// Construct with a pointer to any buffer (DTCM, EXTMEM, BSS …) and its size.
// The struct itself is only ~12 bytes; keep it in fast RAM on Teensy.
struct SL_ArenaBase {
    char*  buf;
    size_t capacity;
    size_t used = 0;

    // Construct over an externally-owned buffer (e.g. EXTMEM char pool[]).
    SL_ArenaBase(char* b, size_t cap) : buf(b), capacity(cap) {}

    // Low-level aligned allocation. Returns nullptr if out of space.
    void* allocate(size_t sz, size_t align_req = 4) {
        size_t aligned = (used + align_req - 1u) & ~(align_req - 1u);
        if (aligned + sz > capacity) return nullptr;
        void* p = buf + aligned;
        used = aligned + sz;
        return p;
    }

    // Returns true if p was allocated from this arena (checked by address range).
    bool owns(const void* p) const {
        return p >= (const void*)buf && p < (const void*)(buf + used);
    }

    // Explicit make<T>() — still usable if you prefer not to rely on the
    // global operator new override.
    template<typename T, typename... Args>
    T* make(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        if (!mem) {
            if (Serial) Serial.printf(
                "SL_Arena: out of space allocating %u bytes (%u/%u used)\n",
                (unsigned)sizeof(T), (unsigned)used, (unsigned)capacity);
            return nullptr;
        }
        return ::new (mem) T(static_cast<Args&&>(args)...);
    }

    // Discard all allocations (objects are NOT destructed).
    void reset() { used = 0; }

    size_t bytes_used() const { return used; }
    size_t bytes_free() const { return capacity > used ? capacity - used : 0; }
};

// ---------------------------------------------------------------------------
// SL_Arena<N> — owns its backing storage inline
// ---------------------------------------------------------------------------
// For platforms where inline storage is fine (RP2350, desktop, etc.).
// On Teensy, declaring this EXTMEM puts the struct AND the storage in PSRAM.
// Prefer the separate buf+SL_ArenaBase pattern when DTCM metadata is desired.
template<size_t N>
struct SL_Arena : SL_ArenaBase {
    char _storage[N];
    SL_Arena() : SL_ArenaBase(_storage, N) {}
    static constexpr size_t CAPACITY = N;
};

// ---------------------------------------------------------------------------
// Global arena registration — used by SaveableSettingBase::operator new/delete
// ---------------------------------------------------------------------------
// Defined in saveloadlib.cpp.
extern SL_ArenaBase* sl_setting_arena;

// Register the arena to use for all SaveableSettingBase allocations.
// Call this BEFORE sl_setup_all() / sl_register_and_setup_root().
// Passing nullptr disables the override and falls back to ::operator new.
inline void sl_set_setting_arena(SL_ArenaBase* arena) {
    sl_setting_arena = arena;
}
