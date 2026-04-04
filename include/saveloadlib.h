// saveloadlib.h
// this version is based on code from copilot suggestions.
#pragma once
#include <Arduino.h>

extern const char *warning_label;
extern char linebuf[128];

// Platform file systems
#if defined(ENABLE_SD)
  #include <SD.h>
#endif
#if defined(ENABLE_LITTLEFS)
  #include <LittleFS.h>
#endif

#include <cstdint>
#include <cstddef>
#include <cstring>

// Tune these for your device
#ifndef SL_MAX_CHILDREN
#define SL_MAX_CHILDREN 12
#endif
#ifndef SL_MAX_SETTINGS
#define SL_MAX_SETTINGS 24
#endif
#ifndef SL_MAX_LINE
#define SL_MAX_LINE 512
#endif


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

struct SaveableSettingBase {
  const char* label;
  const char* category_name;
  virtual const char* get_line() = 0; // returns "key=value" or "value"
  virtual bool parse_key_value(const char* key, const char* value) = 0;
  virtual ~SaveableSettingBase() {}
};

struct ISaveableSettingHost {
  const char* path_segment;
  uint16_t seg_hash;

  struct ChildEntry { ISaveableSettingHost* host; const char* seg; uint16_t hash; };
  struct SettingEntry  { SaveableSettingBase* setting; const char* key; uint16_t hash; };

  ChildEntry children[SL_MAX_CHILDREN];
  uint8_t child_count = 0;

  SettingEntry settings[SL_MAX_SETTINGS];
  uint8_t setting_count = 0;

  virtual void set_path_segment(const char* seg) { path_segment = seg; seg_hash = sl_fnv1a_16(seg); }

  void register_child(ISaveableSettingHost* child) {
    if (child_count < SL_MAX_CHILDREN) {
      children[child_count].host = child;
      children[child_count].seg = child->path_segment;
      children[child_count].hash = sl_fnv1a_16(child->path_segment);
      child_count++;
    }
  }
  // returns true on success (added or replaced), false on overflow or invalid input
  bool register_setting(SaveableSettingBase* setting, bool allow_replace = false) {
    if (!setting || !setting->label) return false;

    if (allow_replace) {
      if (replace_setting_by_label(setting->label, setting)) return true;
    }

    if (setting_count < SL_MAX_SETTINGS) {
      settings[setting_count].setting = setting;
      settings[setting_count].key = setting->label;
      settings[setting_count].hash = sl_fnv1a_16(setting->label);
      ++setting_count;
      return true;
    }

    // no space
    return false;
  }

  // in ISaveableSettingHost
  virtual void setup_saveable_settings() {
    // default: do nothing
  }

  // helper find
  int find_setting_index(const char* label) {
    if (!label) return -1;
    sl_trim_inplace((char*)label);

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
    char out[SL_MAX_LINE];
    for (uint8_t i = 0; i < setting_count; ++i) {
      const char* kv = settings[i].setting->get_line();
      const char* val = strchr(kv, '=') ? strchr(kv, '=') + 1 : kv;
      if (prefix_len == 0) {
        snprintf(out, sizeof(out), "%s=%s", settings[i].key, val);
      } else {
        snprintf(out, sizeof(out), "%s~%s=%s", prefix, settings[i].key, val);
      }
      output_cb(out);
    }
    for (uint8_t c = 0; c < child_count; ++c) {
      char newpref[SL_MAX_LINE];
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

// Root pointer
static ISaveableSettingHost* SL_ROOT = nullptr;
static inline void sl_register_root(ISaveableSettingHost* r) { SL_ROOT = r; }

// Parser helpers
int sl_tokenise_inplace(char* left, char* segs[], int max_segs);
bool sl_parse_line_buffer(char* linebuf);

// File IO helpers declared below in implementation
bool sl_load_from_file(const char* path);
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