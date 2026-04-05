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
  virtual ~SaveableSettingBase() {}
};

struct ISaveableSettingHost {
  const char* path_segment = "";
  static constexpr size_t PATH_SEG_MAX = 32;
  char path_segment_buf[PATH_SEG_MAX] = {};
  uint16_t seg_hash = 0;

  struct ChildEntry   { ISaveableSettingHost* host; const char* seg; uint16_t hash; };
  struct SettingEntry { SaveableSettingBase* setting; const char* key; uint16_t hash; };

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
  // returns true on success (added or replaced), false on overflow or invalid input
  bool register_setting(SaveableSettingBase* setting, bool allow_replace = false) {
    if (!setting || setting->label[0] == '\0') return false;

    if (allow_replace) {
      if (replace_setting_by_label(setting->label, setting)) return true;
    }

    if (setting_count < max_settings) {
      settings[setting_count].setting = setting;
      settings[setting_count].key = setting->label;
      settings[setting_count].hash = sl_fnv1a_16(setting->label);
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

  bool replace_setting_by_label(const char* label, SaveableSettingBase* newSetting, bool allow_add = false) {
    if (!label || !newSetting) return false;
    int idx = find_setting_index(label);
    if (idx >= 0) {
      settings[idx].setting = newSetting;
      settings[idx].key = newSetting->label;
      settings[idx].hash = sl_fnv1a_16(newSetting->label);
      return true;
    }
    if (allow_add) {
      return register_setting(newSetting, false);
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


  // Save recursion
  virtual void save_recursive(char* prefix, size_t prefix_len, void (*output_cb)(const char*)) {
    static char out[SL_MAX_LINE];  // safe static: written then immediately consumed before recursion
    for (uint8_t i = 0; i < setting_count; ++i) {
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
      children[c].host->save_recursive(newpref, strlen(newpref), output_cb);
    }
  }

  // Load line: segments are in-place tokenised pointers
  virtual bool load_line(char** segments, int seg_count, const char* value) {
    if (seg_count == 1) {
      const char* key = segments[0];
      uint16_t h = sl_fnv1a_16(key);
      for (uint8_t i = 0; i < setting_count; ++i) {
        if (settings[i].hash == h && strcmp(settings[i].key, key) == 0) {
          return settings[i].setting->parse_key_value(key, value);
        }
      }
      return false;
    }
    const char* seg0 = segments[0];
    uint16_t h0 = sl_fnv1a_16(seg0);
    for (uint8_t c = 0; c < child_count; ++c) {
      if (children[c].hash == h0 && strcmp(children[c].seg, seg0) == 0) {
        return children[c].host->load_line(segments + 1, seg_count - 1, value);
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
// ---------------------------------------------------------------------------
template<uint8_t NCH, uint8_t NSET>
struct SHStorage : public ISaveableSettingHost {
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

// Parser helpers
int sl_tokenise_inplace(char* left, char* segs[], int max_segs);
bool sl_parse_line_buffer(char* linebuf);

// File IO helpers declared below in implementation
bool sl_load_from_file(const char* path);
bool sl_load_from_linkedlist(const char* path, const LinkedList<String>& lines);
bool sl_save_to_file(ISaveableSettingHost* root, const char* path);

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