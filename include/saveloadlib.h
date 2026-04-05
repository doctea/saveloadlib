// saveloadlib.h
// this version is based on code from copilot suggestions.
#pragma once
#include <Arduino.h>

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
#define SL_MAX_LINE 256
#endif

// ---------------------------------------------------------------------------
// Save scope masks — each setting slot carries a bitmask of the scopes it
// belongs to.  Save and load operations accept a scope mask and only visit
// slots whose mask intersects the requested mask.
//
// Canonical load order: SL_SCOPE_SYSTEM → SL_SCOPE_PROJECT → SL_SCOPE_PATTERN
// Later loads override earlier ones for settings that appear in multiple scopes.
// ---------------------------------------------------------------------------
using sl_scope_t = uint8_t;
static constexpr sl_scope_t SL_SCOPE_SYSTEM  = 0x01;  // bit 0 — device-wide settings
static constexpr sl_scope_t SL_SCOPE_PROJECT = 0x02;  // bit 1 — project/song settings
static constexpr sl_scope_t SL_SCOPE_PATTERN = 0x04;  // bit 2 — per-pattern settings
// bits 3..7 reserved for future levels
static constexpr sl_scope_t SL_SCOPE_ALL     = 0xFF;  // default: slot belongs to every scope

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
#define SL_MAX_LABEL 48
#endif

struct SaveableSettingBase {
  char label[SL_MAX_LABEL] = {};       // owned copy - safe against temporaries at construction
  const char* category_name = "";     // always a string literal, pointer is fine

  void set_label(const char* lbl) {
    if (lbl) strncpy(label, lbl, SL_MAX_LABEL - 1);
    label[SL_MAX_LABEL - 1] = '\0';
  }
  void set_category(const char* cat) {
    category_name = cat ? cat : "";
  }

  virtual const char* get_line() = 0; // returns "key=value" or "value"
  virtual bool parse_key_value(const char* key, const char* value) = 0;
  virtual size_t heap_size() const { return sizeof(SaveableSettingBase); }
  virtual ~SaveableSettingBase() {}
};

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
    this->seg_hash = sl_fnv1a_16(this->path_segment);
  }

  virtual void set_path_segment_fmt(const char *fmt, ... ) {
    char buf[PATH_SEG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, PATH_SEG_MAX, fmt, args);
    va_end(args);
    set_path_segment(buf);
  }

  void register_child(ISaveableSettingHost* child) {
    if (child_count < max_children && child && child->path_segment) {
      children[child_count].host = child;
      children[child_count].seg = child->path_segment;
      children[child_count].hash = sl_fnv1a_16(child->path_segment);
      child_count++;
    }
  }
  // Register a saveable setting slot.
  // mask:         scope bitmask (SL_SCOPE_* constants).  Defaults to SL_SCOPE_ALL so
  //               existing registrations continue to participate in every scope.
  // allow_replace: if true, attempt to replace an existing slot with the same label first
  //               (the slot's existing mask is preserved when replacing).
  // returns true on 
  bool register_setting(SaveableSettingBase* setting, bool allow_replace = false, sl_scope_t mask = SL_SCOPE_ALL) {
    if (!setting || setting->label[0] == '\0') return false;

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
    Serial.printf("Warning: failed to register setting '%s' for host '%s' - max settings reached\n", setting->label, this->path_segment);
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
      return register_setting(newSetting, false, add_mask);
    }
    return false;
  }

  bool remove_setting_by_label(const char* label) {
    int idx = find_setting_index(label);
    if (idx < 0) return false;
    // shift left to compact array
    for (int i = idx; i + 1 < (int)setting_count; ++i) settings[i] = settings[i + 1];
    --setting_count;
    return true;
  }


  // Save recursion.
  // Only slots where (slot.mask & scope) != 0 are written; all others are silently skipped.
  // scope defaults to SL_SCOPE_ALL so the full tree is written when no scope is specified.
  virtual void save_recursive(char* prefix, size_t prefix_len, void (*output_cb)(const char*), sl_scope_t scope = SL_SCOPE_ALL) {
    static char out[SL_MAX_LINE];  // safe static: written then immediately consumed before recursion
    for (uint8_t i = 0; i < setting_count; ++i) {
      if (!(settings[i].mask & scope)) continue;  // not in the requested scope
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
      if (!children[c].host || !children[c].seg) continue;
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


extern ISaveableSettingHost* SL_ROOT;
static inline void sl_register_root(ISaveableSettingHost* r) { SL_ROOT = r; }

// Save the tree into a LinkedList<String>.  Each entry is one "path~key=value" line.
// Requires LinkedList.h to be already included (it is, via the saveloadlib.h header).
static inline void sl_save_to_linkedlist(ISaveableSettingHost* root, LinkedList<String>& out, sl_scope_t scope = SL_SCOPE_ALL) {
  if (!root) return;
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

extern SL_TreeCounts sl_cached_tree_counts;  // last computed result
extern bool          sl_tree_counts_valid;   // false after any sl_setup_all call

// Walk the tree and count nodes + settings.
// Result is cached; pass force=true to force a re-walk.
SL_TreeCounts sl_count_tree(ISaveableSettingHost* root, bool force = false);

// Print callback signature for each setting line
using SL_PrintCallback = void(*)(const char* line, void* user_ctx);

// Print the whole tree to an Arduino Print object (Serial, etc.)
void sl_print_tree_to_print(ISaveableSettingHost* root, Print& out, uint8_t max_depth = 8);

// Walk the tree and call a callback for each printed line
void sl_print_tree_with_callback(ISaveableSettingHost* root, SL_PrintCallback cb, void* user_ctx = nullptr, uint8_t max_depth = 8);

void debug_print_file(const char *filename);


#include "functional-vlpp.h"
using SL_PrintLambda = vl::Func<void(const char*)>;
void sl_print_tree_with_lambda(ISaveableSettingHost* root, SL_PrintLambda lambda, uint8_t max_depth = 8);

// #include "functional-vlpp.h"
// using SL_PrintLambda = vl::Func<void(const char* line, void* user_ctx)>;
// void sl_print_tree_with_lambda(ISaveableSettingHost* root, SL_PrintLambda lambda, void* user_ctx = nullptr, uint8_t max_depth = 8);