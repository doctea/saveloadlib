// saveloadlib.h
// this version is based on code from copilot suggestions.
#pragma once
#include <Arduino.h>

// Native test stubs may not provide a full Print type definition.
// A forward declaration is enough for the Print& API declarations below.
class Print;

// Platform file systems
#if defined(ENABLE_SD)
  #include <SD.h>
#endif
#if defined(ENABLE_LITTLEFS)
  #include <LittleFS.h>
#endif

#include "LinkedList.h"

// #include <cstdint>
// #include <cstddef>
// #include <cstring>

// Tune these for your device
#ifndef SL_MAX_CHILDREN
#define SL_MAX_CHILDREN 24
#endif
#ifndef SL_MAX_SETTINGS
#define SL_MAX_SETTINGS 24
#endif
#ifndef SL_MAX_LINE
#define SL_MAX_LINE 512
#endif

// ---------------------------------------------------------------------------
// Library and file-format version identifiers.
// SL_LIB_VERSION must be kept in sync with the "version" field in library.json.
// Bump SL_FILE_FORMAT_VERSION (integer) when the on-disk format changes in a
// backwards-incompatible way; reset it to 1 on major lib version bumps.
// Both values are written as comment lines at the top of every saved file and
// are safely ignored during loading.
// ---------------------------------------------------------------------------
#define SL_LIB_VERSION            "0.0.9"
#define SL_FILE_FORMAT_VERSION    1
#define SL_FILE_FORMAT_VERSION_STR "1"

// ---------------------------------------------------------------------------
// Save scope masks — each setting slot carries a bitmask of the scopes it
// belongs to.  Save and load operations accept a scope mask and only visit
// slots whose mask intersects the requested mask.
//
// Canonical load order: SL_SCOPE_SYSTEM → SL_SCOPE_PROJECT → SL_SCOPE_SCENE
// Later loads override earlier ones for settings that appear in multiple scopes.
// ---------------------------------------------------------------------------
using sl_scope_t = uint8_t;
static constexpr sl_scope_t SL_SCOPE_SYSTEM  = 0x01;  // bit 0 — device-wide settings
static constexpr sl_scope_t SL_SCOPE_PROJECT = 0x02;  // bit 1 — project/song settings
static constexpr sl_scope_t SL_SCOPE_SCENE = 0x04;  // bit 2 — per-scene settings
static constexpr sl_scope_t SL_SCOPE_ROUTING = 0x08;  // bit 3 — MIDI routing / connection matrix
static constexpr sl_scope_t SL_SCOPE_SNAPSHOT = 0x10;  // bit 4 — for more 'ephemeral' or 'performance' settings that we want to be able to save/load but don't necessarily want to be part of the regular scene/project settings; not included in SL_SCOPE_ALL since it's a bit more optional and use-case-specific than the others, and we might want to exclude it from bulk save/load operations by default
// bits 4..7 reserved for future levels
static constexpr sl_scope_t SL_SCOPE_ALL     = 0xFF;  // default: slot belongs to every scope

// ---------------------------------------------------------------------------
// Scope debug helpers — sl_scope_to_string() and sl_scope_from_string()
// ---------------------------------------------------------------------------

// Entry mapping a single scope bit to its canonical name.
struct sl_scope_entry {
    sl_scope_t  mask;
    const char* name;
};

// Single source of truth for scope bit↔name mapping.
// Add new SL_SCOPE_* bits here; both helper functions pick them up automatically.
static const sl_scope_entry sl_scope_entries[] = {
    { SL_SCOPE_SYSTEM,  "SL_SCOPE_SYSTEM"  },
    { SL_SCOPE_PROJECT, "SL_SCOPE_PROJECT" },
    { SL_SCOPE_SCENE,   "SL_SCOPE_SCENE"   },
    { SL_SCOPE_ROUTING, "SL_SCOPE_ROUTING" },
};
static constexpr size_t SL_SCOPE_ENTRY_COUNT = sizeof(sl_scope_entries) / sizeof(sl_scope_entries[0]);

// Returns a human-readable string like "SL_SCOPE_PROJECT|SL_SCOPE_SCENE".
// SL_SCOPE_ALL (0xFF) is returned as the literal "SL_SCOPE_ALL".
// An unrecognised bitmask falls back to a hex representation, e.g. "0x10".
static inline const char* sl_scope_to_string(sl_scope_t mask) {
    if (mask == SL_SCOPE_ALL) return "SL_SCOPE_ALL";
    static char result[128];
    result[0] = '\0';
    for (size_t i = 0; i < SL_SCOPE_ENTRY_COUNT; ++i) {
        if (mask & sl_scope_entries[i].mask) {
            if (result[0] != '\0') strcat(result, "|");
            strcat(result, sl_scope_entries[i].name);
        }
    }
    if (result[0] == '\0')
        snprintf(result, sizeof(result), "0x%02X", (unsigned)mask);
    return result;
}

// Parses a string like "SL_SCOPE_PROJECT|SL_SCOPE_SCENE" and returns the
// equivalent bitmask.  Tokens are separated by '|'; surrounding whitespace is
// stripped.  "SL_SCOPE_ALL" is handled as a special case.  Unknown tokens are
// silently skipped (result is 0 for a completely unrecognised string).
static inline sl_scope_t sl_scope_from_string(const char* str) {
    if (!str) return 0;
    if (strcmp(str, "SL_SCOPE_ALL") == 0) return SL_SCOPE_ALL;
    sl_scope_t result = 0;
    char buf[128];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* token = strtok(buf, "|");
    while (token) {
        // trim leading whitespace
        while (*token == ' ' || *token == '\t') ++token;
        // trim trailing whitespace
        char* end = token + strlen(token);
        while (end > token && (*(end-1) == ' ' || *(end-1) == '\t')) *(--end) = '\0';
        for (size_t i = 0; i < SL_SCOPE_ENTRY_COUNT; ++i) {
            if (strcmp(token, sl_scope_entries[i].name) == 0) {
                result |= sl_scope_entries[i].mask;
                break;
            }
        }
        token = strtok(nullptr, "|");
    }
    return result;
}

extern const char *warning_label;
extern char linebuf[SL_MAX_LINE];

// trim in-place (removes leading/trailing whitespace and CR/LF)
static inline void sl_trim_inplace(char* s) {
  if (!s) return;
  // trim leading
  char* p = s;
  while (*p && (*p == ' ' || *p == '\t')) ++p;
  if (p != s) memmove(s, p, strlen(p) + 1);
  // trim trailing
  size_t L = strlen(s);
  while (L && (s[L-1] == ' ' || s[L-1] == '\t' || s[L-1] == '\r' || s[L-1] == '\n')) s[--L] = '\0';
}

static inline uint16_t sl_fnv1a_16(const char* s) {
  if (!s) {
    return 0;
  }
  uint32_t h = 2166136261u;
  while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
  return (uint16_t)((h >> 16) ^ (h & 0xFFFF));
}

#ifndef SL_MAX_LABEL
#define SL_MAX_LABEL 48   // max bytes (incl. NUL) copied by set_label(); also a safety cap
#endif

#include "sl_arena.h"  // provides SL_ArenaBase, sl_setting_arena, sl_set_setting_arena

struct SaveableSettingBase {
  // 4-byte pointer instead of a 48-byte inline buffer.
  // Points to arena memory, heap memory, or a string literal — see set_label() / set_label_static().
  const char* label = "";
  const char* category_name = "";  // always a string literal pointer

  // Copy lbl into the arena (if active) or the heap.
  // Safe for temporaries, stack buffers, and dynamically-built strings.
  // Uses at most SL_MAX_LABEL-1 chars + NUL.
  void set_label(const char* lbl) {
    if (!lbl || !*lbl) { label = ""; return; }
    size_t n = strlen(lbl);
    if (n >= SL_MAX_LABEL) n = SL_MAX_LABEL - 1;
    // Allocate n+1 bytes: from arena if available (fast, no heap scan), else heap.
    // Settings are permanent so the allocation is intentionally never freed.
    char* buf = sl_setting_arena
              ? (char*)sl_setting_arena->allocate(n + 1, 1u)
              : (char*)::operator new(n + 1);
    if (buf) { memcpy(buf, lbl, n); buf[n] = '\0'; label = buf; }
    else       label = lbl;  // fallback: borrow pointer (unsafe, but better than crashing)
  }

  // Store pointer directly — NO copy, zero allocation.
  // ONLY safe when lbl is a string literal or otherwise permanently allocated
  // (e.g. a member array, a global, or a pointer whose lifetime exceeds the setting).
  void set_label_static(const char* lbl) {
    label = lbl ? lbl : "";
  }

  void set_category(const char* cat) {
    category_name = cat ? cat : "";
  }

  virtual const char* get_line() = 0; // returns "key=value" or "value"
  virtual bool parse_key_value(const char* key, const char* value) = 0;
  virtual size_t heap_size() const { return sizeof(SaveableSettingBase); }
  virtual ~SaveableSettingBase() {}

  // When a global arena is registered via sl_set_setting_arena(), all
  // `new LSaveableSetting<>()` / `new SaveableSetting<>()` etc. calls
  // automatically use bump allocation from that arena — no source changes
  // needed in setup_saveable_settings().  Falls back to ::operator new if
  // no arena is set (so the library works as-is on any platform).
  static void* operator new(size_t sz) {
    if (sl_setting_arena) {
      void* p = sl_setting_arena->allocate(sz, 8u); // 8-byte align: safe for any member type
      if (p) return p;
      if (Serial) Serial.printf("SL_Arena: full, falling back to heap for %u bytes\n", (unsigned)sz);
    }
    return ::operator new(sz);
  }
  // Arena-owned pointers are freed at arena reset(); heap allocations go to ::delete.
  static void operator delete(void* p) noexcept {
    if (sl_setting_arena && sl_setting_arena->owns(p)) return;
    ::operator delete(p);
  }
};

// Print callback signature for each setting line (declared here so ISaveableSettingHost can use it)
using SL_PrintCallback = void(*)(const char* line, void* user_ctx);

struct ISaveableSettingHost {
  const char* path_segment = "";
  static constexpr size_t PATH_SEG_MAX = 32;
  char path_segment_buf[PATH_SEG_MAX] = {};
  uint16_t seg_hash = 0;

  struct ChildEntry   { ISaveableSettingHost* host; const char* seg; uint16_t hash; };
  // mask: bitmask of SL_SCOPE_* values — which save/load scopes this slot participates in.
  struct SettingEntry { SaveableSettingBase* setting; const char* key; uint16_t hash; sl_scope_t mask; };

  // Arrays injected by SHStorage<NCH,NSET> — NOT owned by this base; never nullptr after construction
  ChildEntry*   children    = nullptr;
  SettingEntry* settings    = nullptr;
  uint8_t max_children      = 0;
  uint8_t max_settings      = 0;
  uint8_t child_count       = 0;
  uint8_t setting_count     = 0;

  virtual void set_path_segment(const char* seg) {
    if (!seg) { 
      path_segment_buf[0] = '\0'; 
      this->path_segment = path_segment_buf; 
      return; 
    }
    strncpy(path_segment_buf, seg, PATH_SEG_MAX - 1);
    path_segment_buf[PATH_SEG_MAX - 1] = '\0';
    this->path_segment = path_segment_buf;
    // Warn if an empty segment is set, which would cause all children to be at the same path and thus collide.
    if (this->path_segment[0] == '\0') {
      if (Serial) Serial.println(F("SL_WARNING: setting empty path segment for a saveable settings host may cause collisions between its children"));
    }
    this->seg_hash = sl_fnv1a_16(this->path_segment);
  }

  // Override to return a dynamically-computed segment (e.g. a virtual label).
  // sl_setup_all() calls this via virtual dispatch after construction, so it is safe
  // to return get_label() or any other virtual value here.
  // Default: return the value set by set_path_segment().
  virtual const char* get_path_segment() const { return path_segment; }

  virtual void set_path_segment_fmt(const char *fmt, ... ) {
    char buf[PATH_SEG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, PATH_SEG_MAX, fmt, args);
    va_end(args);
    set_path_segment(buf);
  }

  virtual void register_child(ISaveableSettingHost* child) {
    if (child_count < max_children && child && child->path_segment) {
      children[child_count].host = child;
      children[child_count].seg = child->path_segment;
      children[child_count].hash = sl_fnv1a_16(child->path_segment);
      child_count++;
    } else if (child_count >= max_children) {
      if (Serial) Serial.printf("SL_WARNING: failed to register_child with path segment '%s' in host '%s' - max children of %i reached\n", child ? child->path_segment : "null", this->path_segment, max_children);
    }
  }
  // Register a saveable setting slot.
  // mask:         scope bitmask (SL_SCOPE_* constants).  Defaults to SL_SCOPE_ALL so
  //               existing registrations continue to participate in every scope.
  // allow_replace: if true, attempt to replace an existing slot with the same label first
  //               (the slot's existing mask is preserved when replacing).
  // returns true on success
  virtual bool register_setting(SaveableSettingBase* setting, sl_scope_t mask = SL_SCOPE_ALL, bool allow_replace = false) {
    if (!setting || !setting->label || !setting->label[0]) return false;

    if (allow_replace) {
      // replace_setting_by_label keeps the slot's existing mask unchanged
      if (replace_setting_by_label(setting->label, setting)) return true;
    }

    if (setting_count < max_settings) {
      settings[setting_count].setting = setting;
      settings[setting_count].key     = setting->label;
      settings[setting_count].hash    = sl_fnv1a_16(setting->label);
      settings[setting_count].mask    = mask;
      ++setting_count;
      return true;
    }

    // no space
    Serial.printf("SL_WARNING: failed to register setting '%s' in host '%s' - max settings of %i reached\n", setting->label, this->path_segment, max_settings);
    return false;
  }

  // in ISaveableSettingHost
  bool saveable_settings_setup = false;
  virtual void setup_saveable_settings() {
    // default: do nothing
  }

  // helper find
  int find_setting_index(const char* label) {
    if (!label) return -1;
    uint16_t h = sl_fnv1a_16(label);
    for (int i = 0; i < (int)setting_count; ++i) {
      if (settings[i].hash == h && strcmp(settings[i].key, label) == 0) return i;
    }
    return -1;
  }

  // Replace the setting object held in an existing slot, or add a new slot.
  // When replacing: the slot's mask is intentionally preserved — it belongs to the
  //   slot, not the setting object, so changing the object doesn't change scope membership.
  // When adding (allow_add=true): the new slot receives add_mask.
  bool replace_setting_by_label(const char* label, SaveableSettingBase* newSetting, bool allow_add = false, sl_scope_t add_mask = SL_SCOPE_ALL) {
    if (!label || !newSetting) return false;
    int idx = find_setting_index(label);
    if (idx >= 0) {
      settings[idx].setting = newSetting;
      settings[idx].key     = newSetting->label;
      settings[idx].hash    = sl_fnv1a_16(newSetting->label);
      // mask not changed: belongs to the slot, not the object
      return true;
    }
    if (allow_add) {
      return register_setting(newSetting, add_mask, false);
    }
    return false;
  }

  // Hook for nodes that have dynamic (runtime-generated) entries not stored in settings[].
  // Called by sl_print_recursive after iterating registered settings, so they appear in showtree.
  // prefix already includes this node's own path_segment (i.e. it is the full parent path of any
  // dynamic key, matching what save_recursive passes to _emit_routing_lines etc.).
  // Default: no-op.
  virtual void print_dynamic_entries(const char* /*prefix*/, SL_PrintCallback /*cb*/, void* /*ctx*/, sl_scope_t /*scope*/) {}

  bool remove_setting_by_label(const char* label) {
    int idx = find_setting_index(label);
    if (idx < 0) return false;
    // shift left to compact array
    for (int i = idx; i + 1 < (int)setting_count; ++i) settings[i] = settings[i + 1];
    --setting_count;
    return true;
  }


  // Save recursion.
  // scope defaults to SL_SCOPE_ALL so the full tree is written when no scope is specified.
  virtual void save_recursive(char* prefix, size_t prefix_len, void (*output_cb)(const char*), sl_scope_t scope = SL_SCOPE_ALL) {
    // Serial.printf("save_recursive in host '%s' with scope mask 0x%02X (setting count %i, host count %i)\n", this->path_segment, scope, setting_count, child_count);
    static char out[SL_MAX_LINE];  // safe static: written then immediately consumed before recursion
    for (uint8_t i = 0; i < setting_count; ++i) {
      // Serial.printf("Working with setting '%s' of host '%s' with scope mask 0x%02X (save scope 0x%02X)\n", settings[i].key, this->path_segment, settings[i].mask, scope);
      if (!(settings[i].mask & scope)) {
        // Serial.printf("Skipping setting '%s' for host '%s' due to scope mismatch (slot mask 0x%02X, save scope 0x%02X)\n", settings[i].key, this->path_segment, settings[i].mask, scope);
        continue;  // not in the requested scope
      }
      const char* kv = settings[i].setting->get_line();
      const char* val = strchr(kv, '=') ? strchr(kv, '=') + 1 : kv;
      if (prefix_len == 0) {
        snprintf(out, SL_MAX_LINE, "%s=%s", settings[i].key, val);
      } else {
        snprintf(out, SL_MAX_LINE, "%s~%s=%s", prefix, settings[i].key, val);
      }
      output_cb(out);
    }
    for (uint8_t c = 0; c < child_count; ++c) {
      // Serial.printf("Child [%i/%i] of host '%s': segment '%s' (hash 0x%04X)\n", c + 1, child_count, this->path_segment, children[c].seg, children[c].hash);
      if (!children[c].host || !children[c].seg) {
        // Serial.printf("\tSkipping child [%i/%i] of host '%s' due to missing host or segment\n", c + 1, child_count, this->path_segment);
        continue;
      }
      char newpref[SL_MAX_LABEL * 4];  // fits max nesting depth with short segment names
      if (prefix_len == 0) snprintf(newpref, sizeof(newpref), "%s", children[c].seg);
      else snprintf(newpref, sizeof(newpref), "%s~%s", prefix, children[c].seg);
      children[c].host->save_recursive(newpref, strlen(newpref), output_cb, scope);
    }
  }

  // Context-based variant: allows capturing state in plain C callbacks (e.g. to push
  // lines into a LinkedList).  Mirrors the logic of the callback-only overload exactly.
  virtual void save_recursive(char* prefix, size_t prefix_len, void (*output_cb)(const char*, void*), void* ctx, sl_scope_t scope = SL_SCOPE_ALL) {
    static char out[SL_MAX_LINE];
    for (uint8_t i = 0; i < setting_count; ++i) {
      if (!(settings[i].mask & scope)) continue;
      const char* kv = settings[i].setting->get_line();
      const char* val = strchr(kv, '=') ? strchr(kv, '=') + 1 : kv;
      if (prefix_len == 0) {
        snprintf(out, SL_MAX_LINE, "%s=%s", settings[i].key, val);
      } else {
        snprintf(out, SL_MAX_LINE, "%s~%s=%s", prefix, settings[i].key, val);
      }
      output_cb(out, ctx);
    }
    for (uint8_t c = 0; c < child_count; ++c) {
      if (!children[c].host || !children[c].seg) continue;
      char newpref[SL_MAX_LABEL * 4];
      if (prefix_len == 0) snprintf(newpref, sizeof(newpref), "%s", children[c].seg);
      else snprintf(newpref, sizeof(newpref), "%s~%s", prefix, children[c].seg);
      children[c].host->save_recursive(newpref, strlen(newpref), output_cb, ctx, scope);
    }
  }

  // Load line: segments are in-place tokenised pointers.
  // A slot is only applied when (slot.mask & scope) != 0, preventing a scope-specific
  // file from overwriting settings that don't belong to that scope.
  // scope defaults to SL_SCOPE_ALL so all slots are matched when no scope is specified.
  virtual bool load_line(char** segments, int seg_count, const char* value, sl_scope_t scope = SL_SCOPE_ALL) {
    if (seg_count == 1) {
      const char* key = segments[0];
      uint16_t h = sl_fnv1a_16(key);
      for (uint8_t i = 0; i < setting_count; ++i) {
        if (settings[i].hash == h && strcmp(settings[i].key, key) == 0) {
          if (!(settings[i].mask & scope)) return false;  // scope mismatch: do not apply
          return settings[i].setting->parse_key_value(key, value);
        }
      }
      return false;
    }
    const char* seg0 = segments[0];
    uint16_t h0 = sl_fnv1a_16(seg0);
    for (uint8_t c = 0; c < child_count; ++c) {
      if (children[c].hash == h0 && strcmp(children[c].seg, seg0) == 0) {
        return children[c].host->load_line(segments + 1, seg_count - 1, value, scope);
      }
    }
    return false;
  }

  virtual ~ISaveableSettingHost() {}
};

// ---------------------------------------------------------------------------
// SHStorage<NCH, NSET> — inherit from this instead of ISaveableSettingHost.
// Provides inline storage for NCH children and NSET settings; injects pointers
// into ISaveableSettingHost so all virtual methods work without change.
// NCH=0 is legal (leaf nodes with no children).
//
// Uses VIRTUAL inheritance from ISaveableSettingHost so that a subclass can
// override the sizing by ALSO inheriting SHStorage<larger_NCH, larger_NSET>:
//
//   class Base    : public SHStorage<0, 6>  { ... };   // 6 settings when used directly
//   class Derived : public Base,
//                   public SHStorage<0, 14> { ... };   // 14 settings - overrides Base's 6
//
// The most-derived SHStorage constructor runs last and wins. The parent's inline
// arrays still exist in memory (unavoidable), so reserve this pattern for cases
// where the base is sometimes used directly with small sizing AND a subclass
// occasionally needs significantly more. For simple cases, just size the base
// class generously to cover all subclasses.
// ---------------------------------------------------------------------------
template<uint8_t NCH, uint8_t NSET>
struct SHStorage : virtual public ISaveableSettingHost {
  // GCC zero-size arrays (arm-none-eabi extension) are used when NCH=0;
  // children pointer is set to nullptr in that case.
  ChildEntry   _ch[NCH];   // zero-size when NCH=0; GCC extension, fine on RP2040
  SettingEntry _st[NSET];
  SHStorage() {
    children     = NCH > 0 ? _ch : nullptr;
    max_children = NCH;
    settings     = _st;
    max_settings = NSET;
  }
};

// Convenience aliases for common patterns
using SHLeaf    = SHStorage<0,  SL_MAX_SETTINGS>;  // no children, default settings
using SHNode    = SHStorage<SL_MAX_CHILDREN, SL_MAX_SETTINGS>; // full size (backwards compat)

// ---------------------------------------------------------------------------
// SHDynamic — heap-backed alternative to SHStorage; never silently drops
// registrations.  Arrays start small and double on each overflow.
//
// The tree is expected to be static after sl_setup_all() (true for all
// Arduino sketches), so there is no fragmentation concern: all allocations
// happen once during setup() and are never freed.
//
// Usage: inherit instead of SHStorage<NCH,NSET>:
//   class MyThing : public SHDynamic<4, 16> { ... };
//
// SHDynamic supports the same virtual-inheritance size-override pattern as
// SHStorage.  The most-derived SHDynamic<NCH,NSET> constructor runs last and
// its NCH/NSET values win (used as first-allocation sizes):
//   class Base    : virtual public SHDynamic<2, 8>  { ... };
//   class Derived : public Base, virtual public SHDynamic<8, 16> { ... };
//
// Implementation note: the grow logic and the virtual overrides live in a
// single non-templated SHDynamicBase.  SHDynamic<NCH,NSET> is a thin wrapper
// that only sets the initial-allocation sizes via its constructor.  This
// ensures exactly one override of register_child/register_setting exists in
// the vtable regardless of how many SHDynamic<X,Y> instantiations appear in
// the inheritance chain.
// ---------------------------------------------------------------------------

// Non-templated base: holds all the logic.  Do not inherit this directly;
// use SHDynamic<NCH,NSET> below.
struct SHDynamicBase : virtual public ISaveableSettingHost {
  // Initial allocation sizes, written by each SHDynamic<NCH,NSET> constructor.
  // The most-derived one runs last and wins.
  uint8_t _init_children = 4;
  uint8_t _init_settings = 8;

  ~SHDynamicBase() override {
    delete[] children;
    delete[] settings;
    children = nullptr;
    settings = nullptr;
  }

  void register_child(ISaveableSettingHost* child) override {
    if (!child || !child->path_segment) return;
    if (child_count >= max_children) _grow_children();
    children[child_count].host = child;
    children[child_count].seg  = child->path_segment;
    children[child_count].hash = sl_fnv1a_16(child->path_segment);
    child_count++;
  }

  bool register_setting(SaveableSettingBase* setting, sl_scope_t mask = SL_SCOPE_ALL, bool allow_replace = false) override {
    if (!setting || !setting->label || !setting->label[0]) return false;
    if (allow_replace) {
      if (replace_setting_by_label(setting->label, setting)) return true;
    }
    if (setting_count >= max_settings) _grow_settings();
    settings[setting_count].setting = setting;
    settings[setting_count].key     = setting->label;
    settings[setting_count].hash    = sl_fnv1a_16(setting->label);
    settings[setting_count].mask    = mask;
    ++setting_count;
    return true;
  }

private:
  void _grow_children() {
    uint8_t new_max = (max_children == 0) ? (_init_children > 0 ? _init_children : 4)
                    : (max_children <= 127 ? max_children * 2 : 255);
    ChildEntry* arr = new ChildEntry[new_max];
    if (children && child_count) memcpy(arr, children, child_count * sizeof(ChildEntry));
    delete[] children;
    children     = arr;
    max_children = new_max;
  }

  void _grow_settings() {
    uint8_t new_max = (max_settings == 0) ? (_init_settings > 0 ? _init_settings : 8)
                    : (max_settings <= 127 ? max_settings * 2 : 255);
    SettingEntry* arr = new SettingEntry[new_max];
    if (settings && setting_count) memcpy(arr, settings, setting_count * sizeof(SettingEntry));
    delete[] settings;
    settings     = arr;
    max_settings = new_max;
  }
};

// Templated wrapper: sets initial allocation sizes, then defers everything
// else to SHDynamicBase.  Inherit this in your classes.
template<uint8_t NCH, uint8_t NSET>
struct SHDynamic : virtual public SHDynamicBase {
  SHDynamic() {
    _init_children = NCH;
    _init_settings = NSET;
  }
};


extern ISaveableSettingHost* SL_ROOT;
static inline void sl_register_root(ISaveableSettingHost* r) { SL_ROOT = r; }

// Save the tree into a LinkedList<String>.  Each entry is one "path~key=value" line.
// Requires LinkedList.h to be already included (it is, via the saveloadlib.h header).
static inline void sl_save_to_linkedlist(ISaveableSettingHost* root, LinkedList<String>& out, sl_scope_t scope = SL_SCOPE_ALL) {
  if (!root) return;
  // Write version + scope header lines so the origin of the file is identifiable.
  // #saveloadlib_scope format: 0xNN:SYMBOLIC_NAMES  — hex mask before ':' for machine parsing,
  // symbolic names after ':' for human readability.
  out.add(String(F("#saveloadlib_version=" SL_LIB_VERSION)));
  out.add(String(F("#saveloadlib_format=" SL_FILE_FORMAT_VERSION_STR)));
  {
    char _scopehdr[80];
    snprintf(_scopehdr, sizeof(_scopehdr), "#saveloadlib_scope=0x%02X:%s", (unsigned)scope, sl_scope_to_string(scope));
    out.add(String(_scopehdr));
  }
  char prefix[2] = {0};
  root->save_recursive(prefix, 0,
    [](const char* line, void* ctx) {
      reinterpret_cast<LinkedList<String>*>(ctx)->add(String(line));
    }, &out, scope);
}

// Parser helpers
int sl_tokenise_inplace(char* left, char* segs[], int max_segs);
// Parse one key=value line and route it into the tree.
// scope: only slots whose mask intersects scope are applied; defaults to SL_SCOPE_ALL.
bool sl_parse_line_buffer(char* linebuf, sl_scope_t scope = SL_SCOPE_ALL);

// File IO helpers.
// scope: limits which settings are visited; defaults to SL_SCOPE_ALL (all settings).
bool sl_load_from_file(const char* path, sl_scope_t scope = SL_SCOPE_ALL);
bool sl_load_from_linkedlist(const char* path, const LinkedList<String>& lines, sl_scope_t scope = SL_SCOPE_ALL);
bool sl_save_to_file(ISaveableSettingHost* root, const char* path, sl_scope_t scope = SL_SCOPE_ALL);

// ---------------------------------------------------------------------------
// Optional bulk file-read buffer
// ---------------------------------------------------------------------------
// Register a pre-allocated buffer before calling sl_load_from_file().
// When set, the entire file is read in one shot and parsed in RAM —
// typically 3-5× faster than line-by-line SD streaming.
//
// The buffer can be anywhere (DTCM, EXTMEM, BSS).
// Size it to comfortably exceed your largest save file.
// Falls back to line-by-line streaming if the file doesn't fit.
//
// Example (Teensy EXTMEM — one large allocation):
//   EXTMEM static char sl_file_buf[65536];
//   sl_set_file_read_buffer(sl_file_buf, sizeof(sl_file_buf));
//
// Example (RP2350 — ordinary BSS):
//   static char sl_file_buf[32768];
//   sl_set_file_read_buffer(sl_file_buf, sizeof(sl_file_buf));
void sl_set_file_read_buffer(char* buf, size_t size);

// ---------------------------------------------------------------------------
// Multi-scope save/load helpers
// ---------------------------------------------------------------------------
// Maps one scope bit to a file path.  Build a small array and pass it to
// sl_save_all_scopes / sl_load_all_scopes.
struct SL_ScopeTarget {
  sl_scope_t  scope;   // e.g. SL_SCOPE_SYSTEM
  const char* path;    // e.g. "/save/system.txt"
};

// Save each scope to its configured file.
bool sl_save_all_scopes(ISaveableSettingHost* root, const SL_ScopeTarget* targets, uint8_t count);

// Load each scope from its configured file in the order provided.
// For predictable overrides, pass targets in canonical order: system, project, pattern.
bool sl_load_all_scopes(const SL_ScopeTarget* targets, uint8_t count);

// Recursively ensure seg_hash is set and call setup_saveable_settings on each node.
// This function does not modify child arrays beyond calling the virtual setup method.
static void sl_compute_hashes_recursive(ISaveableSettingHost* host) {
  if (!host) return;
  if (!host->path_segment) return;
  // If seg_hash is zero, compute it now. Allow user to precompute if desired.
  if (host->seg_hash == 0) host->seg_hash = sl_fnv1a_16(host->path_segment);
  for (uint8_t i = 0; i < host->child_count; ++i) {
    sl_compute_hashes_recursive(host->children[i].host);
  }
}

// Walk the tree and call setup_saveable_settings on each host.
// The order is parent first, then children, matching the inheritance pattern.
void sl_setup_all(ISaveableSettingHost* root);

static inline void sl_register_and_setup_root(ISaveableSettingHost* r) {
  sl_register_root(r);
  sl_setup_all(r);
}


// ---------------------------------------------------------------------------
// Tree statistics
// ---------------------------------------------------------------------------
struct SL_TreeCounts {
  uint16_t nodes;     // total ISaveableSettingHost objects in tree
  uint16_t settings;  // total registered settings across all nodes
  uint32_t bytes;     // estimated memory: node structs + inline arrays + heap-allocated settings
};

extern SL_TreeCounts sl_cached_tree_counts;  // last computed result (SL_SCOPE_ALL only)
extern bool          sl_tree_counts_valid;   // false after any sl_setup_all call

// Walk the tree and count nodes + settings.
// scope: when SL_SCOPE_ALL the result is cached (pass force=true to re-walk).
//        when a specific scope mask is given the walk is always fresh (not cached),
//        and only settings whose mask intersects scope are counted in .settings and
//        their heap bytes in .bytes; .nodes counts every traversed node regardless.
SL_TreeCounts sl_count_tree(ISaveableSettingHost* root, bool force = false, sl_scope_t scope = SL_SCOPE_ALL);

// ---------------------------------------------------------------------------
// Tree validation
// ---------------------------------------------------------------------------
// Walk every node and warn about any SHStorage node that is at capacity
// (setting_count == max_settings or child_count == max_children).
// A node at capacity after sl_setup_all() silently dropped any further
// register_setting()/register_child() calls — this function makes that visible.
// SHDynamic nodes are never at capacity, so they will never trigger a warning.
//
// Prints one warning line per saturated node; prints an "OK" line if clean.
// Returns true if no issues found, false if any warnings were emitted.
//
// Recommended usage at the end of setup():
//   sl_validate_tree(SL_ROOT, Serial);
bool sl_validate_tree(ISaveableSettingHost* root, Print& out);

// Print the whole tree to an Arduino Print object (Serial, etc.)
void sl_print_tree_to_print(ISaveableSettingHost* root, Print& out, uint8_t max_depth = 8, sl_scope_t scope = SL_SCOPE_ALL);

// Walk the tree and call a callback for each printed line
void sl_print_tree_with_callback(ISaveableSettingHost* root, SL_PrintCallback cb, void* user_ctx = nullptr, uint8_t max_depth = 8, sl_scope_t scope = SL_SCOPE_ALL);

void debug_print_file(const char *filename);


#include "functional-vlpp.h"
using SL_PrintLambda = vl::Func<void(const char*)>;

void sl_print_tree_with_lambda(ISaveableSettingHost* root, SL_PrintLambda lambda, uint8_t max_depth = 8, sl_scope_t scope = SL_SCOPE_ALL);

// #include "functional-vlpp.h"
// using SL_PrintLambda = vl::Func<void(const char* line, void* user_ctx)>;
// void sl_print_tree_with_lambda(ISaveableSettingHost* root, SL_PrintLambda lambda, void* user_ctx = nullptr, uint8_t max_depth = 8);