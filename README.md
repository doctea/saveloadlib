# saveloadlib

Classes and helpers to manage saving + recalling a hierarchy of settings as a flat key=value text file, using a `~`-delimited path to identify each setting in the tree.

For use in my Microlidian and Nexus6/usb_midi_clocker projects.

Largely vibe-coded using Claude, based on my experience with save/load code in previous projects and the needs of Microlidian. The main goal was to have a simple way to manage a large number of settings across multiple classes, without needing to write custom serialisation code for each one or manually call save/load on each object.

---

(Documentation below written by Claude; don't necessarily trust it without checking the code!)

## Overview

saveloadlib organises objects that have saveable state into a tree. Each node in the tree is an `ISaveableSettingHost`. Each node holds:

- An array of **children** (other `ISaveableSettingHost` objects — subtrees)
- An array of **settings** (individual `SaveableSettingBase` objects — leaf values)

The tree is serialised to a flat text file as `path=value` lines, where path segments are joined with `~`:

```
EuclidianSequencer~mutate_enabled=1
EuclidianSequencer~pattern_0~steps=16
EuclidianSequencer~pattern_0~pulses=8
EuclidianSequencer~pattern_1~steps=16
```

On load, each line is split on `~` and routed down the tree until the matching leaf setting is found and its value applied.

---

## Quickstart

### 1. Inherit from `SHStorage<NCH, NSET>`

Instead of inheriting `ISaveableSettingHost` directly, inherit `SHStorage<NCH, NSET>`. This provides inline (stack/object-embedded) storage for `NCH` children and `NSET` settings, avoiding heap allocation for the arrays.

```cpp
#include "saveloadlib.h"
#include "saveload_settings.h"

class MyThing : public SHStorage<0, 4> {   // 0 children, 4 settings
    float volume = 0.8f;
    bool  muted  = false;

public:
    MyThing() { set_path_segment("MyThing"); }

    virtual void setup_saveable_settings() override {
        ISaveableSettingHost::setup_saveable_settings();
        register_setting(new LSaveableSetting<float>("volume", "MyThing", &this->volume));
        register_setting(new LSaveableSetting<bool> ("muted",  "MyThing", &this->muted));
    }
};
```

### 2. Register the root and call setup

```cpp
MyThing thing;
sl_register_and_setup_root(&thing);   // registers root + calls setup_saveable_settings() recursively
```

Or separately:

```cpp
sl_register_root(&thing);
sl_setup_all(&thing);
```

### 3. Save and load

```cpp
// Save to LittleFS (needs ENABLE_LITTLEFS defined)
sl_save_to_file(&thing, "/save/slot0.txt");

// Load from LittleFS
sl_load_from_file("/save/slot0.txt");
```

Both functions use `SL_ROOT` by default for load routing. Save takes an explicit root pointer so you can save subtrees independently.

An optional `scope` argument controls which settings are visited (see [Save Scopes](#save-scopes) below). Omitting it defaults to `SL_SCOPE_ALL`, which visits every setting — preserving backward compatibility.

---

## Registering Settings

### `LSaveableSetting<T>` — lambda/callable-based (preferred)

```cpp
// Simplest — pointer to member variable
register_setting(new LSaveableSetting<float>("density", "MyClass", &this->density));

// With custom getter and setter lambdas
register_setting(new LSaveableSetting<int>(
    "channel", "MyClass", nullptr,
    [this](int v){ this->set_channel(v); },   // setter
    [this]()->int{ return this->get_channel(); } // getter
));
```

Constructor: `LSaveableSetting(label, category, DataType* var, setter={}, getter={})`

- **label** — unique name within this host; becomes the key in the file.
- **category** — informational string, not used in serialisation.
- **var** — pointer to the variable, or `nullptr` if using lambdas.
- **setter** / **getter** — optional `vl::Func` callables. If provided, they take priority over `var`.

> **Important:** the setter is the 4th argument and the getter is the 5th. Getting these swapped is a common mistake — the value will save but never load (or vice versa).

Supported types out of the box: `bool`, all integer types, `float`, `double`, any `enum`, `const char*`.

For unsupported types, either specialise `sl_parse_from_cstr<T>` or subclass `SaveableSettingBase` directly.

### `SaveableSetting<TargetClass, DataType>` — member-function-pointer-based

An older style that takes a `this` pointer and member function pointers for getter/setter. Prefer `LSaveableSetting` for new code.

### Custom setting types

Subclass `SaveableSettingBase` and implement:
- `const char* get_line()` — return `"label=value"` in `linebuf`
- `bool parse_key_value(const char* key, const char* value)` — apply value if key matches

This is used for cases like `ModulationSlotsSaveableSetting` that serialise multiple values in a single compact line.

---

## Registering Children

Call `register_child(ptr)` inside `setup_saveable_settings()`:

```cpp
virtual void setup_saveable_settings() override {
    ISaveableSettingHost::setup_saveable_settings();  // always call base first if you need the ancestor's items too
    for (int i = 0; i < pattern_count; i++) {
        register_child(patterns[i]);   // patterns[i] is an ISaveableSettingHost*
    }
}
```

Each child must have its `path_segment` set before being registered (call `set_path_segment("name")` or `set_path_segment_fmt("pattern_%i", i)` in the child's constructor).

---

## Save Scopes

Each registered setting slot carries a small bitmask (`sl_scope_t`) that identifies which save/load scopes it participates in. Save and load operations accept a scope mask and only visit slots whose mask intersects the requested mask. This allows different parts of the tree to be written to different files — for example a device-global system file, a per-project file, and a per-pattern file.

### Scope constants

| Constant | Bit | Intended use |
|---|---|---|
| `SL_SCOPE_SYSTEM`  | `0x01` | Device-wide settings (MIDI channel, clock source, …) |
| `SL_SCOPE_PROJECT` | `0x02` | Per-project / per-song settings |
| `SL_SCOPE_SCENE`   | `0x04` | Per-scene settings |
| `SL_SCOPE_ALL`     | `0xFF` | **Default** — slot participates in every scope |

Bits `0x08`–`0x80` are reserved for future levels.

### Tagging settings with a scope

Pass a scope mask as the optional third argument to `register_setting`. Omitting it defaults to `SL_SCOPE_ALL`, so existing code is unaffected.

```cpp
virtual void setup_saveable_settings() override {
    ISaveableSettingHost::setup_saveable_settings();

    // belongs to every scope (default)
    register_setting(new LSaveableSetting<bool>("enabled", "MyClass", &this->enabled));

    // only saved/loaded as part of the system file
    register_setting(new LSaveableSetting<uint8_t>("midi_channel", "MyClass", &this->midi_ch),
                     false, SL_SCOPE_SYSTEM);

    // saved in both project and scene files
    register_setting(new LSaveableSetting<float>("volume", "MyClass", &this->volume),
                     false, SL_SCOPE_PROJECT | SL_SCOPE_SCENE);
}
```

The mask belongs to the **slot**, not the setting object. If you replace a setting object later with `replace_setting_by_label`, the existing mask is preserved automatically.

### Saving and loading a single scope

Pass a scope to `sl_save_to_file` / `sl_load_from_file`:

```cpp
// Save only system settings
sl_save_to_file(SL_ROOT, "/save/system.txt", SL_SCOPE_SYSTEM);

// Load only project settings (won't touch slots tagged SYSTEM or PATTERN)
sl_load_from_file("/save/project.txt", SL_SCOPE_PROJECT);
```

### Multi-scope save/load with `SL_ScopeTarget`

Define a mapping array and use the helper functions to save or load all scopes in one call:

```cpp
static const SL_ScopeTarget scopes[] = {
    { SL_SCOPE_SYSTEM,  "/save/system.txt"  },
    { SL_SCOPE_PROJECT, "/save/project.txt" },
    { SL_SCOPE_SCENE,   "/save/scene.txt"   },
};
constexpr uint8_t NUM_SCOPES = 3;

// Save all three files
sl_save_all_scopes(SL_ROOT, scopes, NUM_SCOPES);

// Load all three files in canonical order (system → project → scene)
// Later loads override earlier ones for settings that appear in multiple scopes.
sl_load_all_scopes(scopes, NUM_SCOPES);
```

### Canonical load order

Load scopes in order from most-global to most-specific: **system → project → scene**. Because later loads overwrite earlier values for settings tagged with multiple scopes (e.g. `SL_SCOPE_PROJECT | SL_SCOPE_SCENE`), the most-specific file wins — which is usually the desired behaviour.

### Scope gotcha: silent non-apply on mismatch

When loading a file with a scope mask, any line whose key matches a slot whose mask does **not** intersect the requested scope is silently ignored. There is no error or warning. This is intentional — loading a pattern file should not accidentally overwrite system settings — but it also means a typo in a scope mask will cause a setting to not be restored without any obvious indication.

---

## Choosing NCH and NSET

`SHStorage<NCH, NSET>` allocates fixed arrays inside the object — nothing goes on the heap. Getting the counts wrong is a silent failure: `register_setting`/`register_child` will print a warning to `Serial` and return `false`, but the setting simply won't be saved or loaded.

**Count everything in the whole chain.** `setup_saveable_settings()` calls `ISaveableSettingHost::setup_saveable_settings()` which propagates up to the base, so settings from every class in the inheritance chain are registered into the same arrays.

Example — if `Base` registers 4 settings and `Derived` registers 3 more, `Derived` needs at least `NSET=7`.

The global fallback sizes are `SL_MAX_CHILDREN` and `SL_MAX_SETTINGS` (both 24 by default). The convenience aliases `SHLeaf` and `SHNode` use these maximums, which wastes RAM when used for many objects.

---

## Gotchas

### Silent overflow

If `setting_count >= max_settings` when `register_setting` is called, the setting is silently dropped (a `Serial.printf` warning is emitted). The build will succeed and the object will appear to work, but that setting will never be saved or loaded. Always check `Serial` output during first boot after adding new settings.

### Subclass needs more slots than its base

If you have a base class that is sometimes used directly (with small sizing) and a subclass that registers more settings, add a second `SHStorage` with larger counts to the subclass using `virtual` inheritance. The most-derived constructor runs last and overwrites the limits:

```cpp
class Base    : virtual public SHStorage<0, 6>  { ... };
class Derived : public Base, virtual public SHStorage<0, 12> { ... };
```

The extra `_st[6]` array from `Base::SHStorage` is still allocated in memory (unavoidable), but it goes unused. Only do this when the size difference is significant enough to justify the complexity.

### Setter and getter lambdas are swapped

`LSaveableSetting`'s 4th argument is the **setter** and 5th is the **getter**. Swapping them compiles cleanly (? does it? - Ed.) but the value will save as whatever the getter returns and apply to whatever the setter accepts — producing wrong values silently.

### `setup_saveable_settings()` called twice

`ISaveableSettingHost` has a `saveable_settings_setup` guard flag. `sl_setup_all` checks this flag and will not call `setup_saveable_settings()` a second time on the same object. If you call it manually, check or set the flag yourself.

### `arduino::String` is not supported

Use `const char*` instead of `String` for saveable settings. `String` heap-allocates internally and its lifetime across save/load boundaries is error-prone on microcontrollers. Store the value in a fixed `char[]` buffer and register it as `LSaveableSetting<const char*>`.

### LittleFS / SD safety on dual-core RP2040

Flash file system operations on the RP2040 pause flash execution on core 1 mid-transaction. Wrap both `sl_save_to_file` and `sl_load_from_file` calls in your platform's mutex:

```cpp
acquire_lock();
sl_save_to_file(root, path);
release_lock();
```

Failing to do this causes the other core to hang or corrupt state mid-write, which can appear as controls freezing after a save.

### Path segment must be set before `register_child`

`register_child` copies the `path_segment` pointer at the time of the call. Set the path segment in the child's constructor, not after the fact.

---

## Printing the Tree

```cpp
// To a Print object (Serial, etc.)
sl_print_tree_to_print(root, Serial);

// With a plain C callback
sl_print_tree_with_callback(root, [](const char* line, void*){ Serial.println(line); });

// With a vl::Func lambda
sl_print_tree_with_lambda(root, [](const char* line){ Serial.println(line); });
```

All three accept an optional `max_depth` argument (default 8).

---

## Build Flags

| Flag | Effect |
|---|---|
| `ENABLE_LITTLEFS` | Enable LittleFS file I/O |
| `ENABLE_SD` | Enable SD card file I/O |
| `SL_MAX_CHILDREN` | Override default children slots (24) for `SHNode`/`SHLeaf` aliases |
| `SL_MAX_SETTINGS` | Override default setting slots (24) |
| `SL_MAX_LABEL` | Override max label length in bytes (48) |
| `SL_MAX_LINE` | Override max serialised line length in bytes (256) |
| `ENABLE_TESTSAVELOAD` | Enable test code and objects for save/load (not included in build by default) |
