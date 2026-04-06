// saveloadlib.cpp
#include "saveloadlib.h"

const char *warning_label = " - WARNING: no target nor getter func!";

ISaveableSettingHost* SL_ROOT = nullptr;  // single definition; extern-declared in saveloadlib.h

char linebuf[SL_MAX_LINE];  // shared buffer for constructing lines to save

SL_TreeCounts sl_cached_tree_counts = {0, 0, 0};
bool          sl_tree_counts_valid  = false;

static SL_TreeCounts sl_count_tree_recursive(ISaveableSettingHost* root) {
  SL_TreeCounts c = {0, 0, 0};
  if (!root) return c;
  c.nodes    = 1;
  c.settings = root->setting_count;
  // node struct itself + inline child/setting arrays inside SHStorage
  c.bytes    = (uint32_t)sizeof(ISaveableSettingHost)
             + (uint32_t)root->max_children * sizeof(ISaveableSettingHost::ChildEntry)
             + (uint32_t)root->max_settings  * sizeof(ISaveableSettingHost::SettingEntry);
  // heap-allocated setting objects
  for (uint8_t i = 0; i < root->setting_count; ++i)
    c.bytes += (uint32_t)root->settings[i].setting->heap_size();
  for (uint8_t i = 0; i < root->child_count; ++i) {
    SL_TreeCounts child = sl_count_tree_recursive(root->children[i].host);
    c.nodes    += child.nodes;
    c.settings += child.settings;
    c.bytes    += child.bytes;
  }
  return c;
}

SL_TreeCounts sl_count_tree(ISaveableSettingHost* root, bool force) {
  if (!force && sl_tree_counts_valid)
    return sl_cached_tree_counts;
  sl_cached_tree_counts = sl_count_tree_recursive(root);
  sl_tree_counts_valid  = true;
  return sl_cached_tree_counts;
}

int sl_tokenise_inplace(char* left, char* segs[], int max_segs) {
  int count = 0;
  char* p = left;
  if (*p == '\0') return 0;
  segs[count++] = p;
  while (*p && count < max_segs) {
    if (*p == '~') { *p = '\0'; if (*(p+1) != '\0') segs[count++] = p+1; }
    ++p;
  }
  return count;
}

bool sl_parse_line_buffer(char* linebuf, sl_scope_t scope) {
  if (!SL_ROOT) 
    return false;

  size_t L = strlen(linebuf);

  // strip trailing newlines just in case
  while (L && (linebuf[L-1] == '\n' || linebuf[L-1] == '\r')) 
    linebuf[--L] = '\0';
 
  // find '=' to split at; return if not found
  char* eq = strchr(linebuf, '=');
  if (!eq) 
    return false;

  // split in place and set pointers
  *eq = '\0';
  char* left = linebuf;
  char* value = eq + 1;

  const int MAX_SEGS = 32;
  static char* segs[MAX_SEGS];
  int seg_count = sl_tokenise_inplace(left, segs, MAX_SEGS);
  if (seg_count <= 0) return false;
  
  if (SL_ROOT->path_segment && strcmp(SL_ROOT->path_segment, segs[0]) == 0) {
    return SL_ROOT->load_line(segs + 1, seg_count - 1, value, scope);
  } else {
    return SL_ROOT->load_line(segs, seg_count, value, scope);
  }
}

// Arduino SD/LittleFS streaming loader.
// scope: only settings whose mask intersects scope are applied.
bool sl_load_from_file(const char* path, sl_scope_t scope) {
  char linebuf[SL_MAX_LINE];
#if defined(ENABLE_SD)
  File f = SD.open(path, FILE_READ);
#elif defined(ENABLE_LITTLEFS)
  File f = LittleFS.open(path, "r");
#else
  return false;
#endif
  if (!f) return false;

  while (f.available()) {
    size_t n = f.readBytesUntil('\n', linebuf, sizeof(linebuf) - 1);
    linebuf[n] = '\0';
    sl_parse_line_buffer(linebuf, scope);
  }
  f.close();
  return true;
}

// LinkedList<String> loader (for testing with in-memory data, or from other sources like Serial)
bool sl_load_from_linkedlist(const char* path, const LinkedList<String>& lines, sl_scope_t scope) {
  char linebuf[SL_MAX_LINE];
  for (int i = 0; i < lines.size(); i++) {
    String line = lines.get(i);
    line.toCharArray(linebuf, sizeof(linebuf));
    sl_parse_line_buffer(linebuf, scope);
  }
  return true;
}

// File writer helper
struct FileWriter {
  File f;
  bool begin(const char* path) {
#if defined(ENABLE_SD)
    f = SD.open(path, FILE_WRITE);
#elif defined(ENABLE_LITTLEFS)
    f = LittleFS.open(path, "w");
#else
    return false;
#endif
    return (bool)f;
  }
  void writeLine(const char* line) { if (f) f.println(line); }
  void end() { if (f) f.close(); }
} globalFileWriter;

static void sl_file_output_cb(const char* line) { globalFileWriter.writeLine(line); }

// Save the tree to a file.
// scope: only settings whose mask intersects scope are written.
bool sl_save_to_file(ISaveableSettingHost* root, const char* path, sl_scope_t scope) {
  if (!root) return false;
  if (!globalFileWriter.begin(path)) return false;
  char prefix[1] = {0};
  //Serial.printf("Saving to '%s' with scope mask 0x%02X... starting at %s\n", path, scope, root->path_segment);
  root->save_recursive(prefix, 0, sl_file_output_cb, scope);
  globalFileWriter.end();
  return true;
}

// Save each scope to its configured file.
bool sl_save_all_scopes(ISaveableSettingHost* root, const SL_ScopeTarget* targets, uint8_t count) {
  if (!root || !targets) return false;
  bool ok = true;
  for (uint8_t i = 0; i < count; ++i)
    ok &= sl_save_to_file(root, targets[i].path, targets[i].scope);
  return ok;
}

// Load each scope from its configured file in the order provided.
// Canonical order: system → project → pattern (later loads override earlier ones).
bool sl_load_all_scopes(const SL_ScopeTarget* targets, uint8_t count) {
  if (!targets) return false;
  bool ok = true;
  for (uint8_t i = 0; i < count; ++i)
    ok &= sl_load_from_file(targets[i].path, targets[i].scope);
  return ok;
}

void sl_setup_all(ISaveableSettingHost* root) {
  if (!root) return;
  sl_tree_counts_valid = false;  // tree is changing; invalidate cache
  if (root->saveable_settings_setup) return;   // guard: don't call twice
  
  root->setup_saveable_settings();
  
  // Ensure hashes are computed so replace/find operations are fast during setup.
  sl_compute_hashes_recursive(root);
  
  // Use a simple stackless recursion; depth is expected to be small.
  for (uint8_t i = 0; i < root->child_count; ++i) {
    ISaveableSettingHost* child = root->children[i].host;
    if (child) {
      // Refresh seg/hash via virtual dispatch before recursing.
      // get_path_segment() may return a virtual value (e.g. get_label()) that wasn't
      // available at register_child() time; objects are fully constructed here so
      // virtual dispatch is safe.
      const char* seg = child->get_path_segment();
      root->children[i].seg  = seg;
      root->children[i].hash = sl_fnv1a_16(seg);
      sl_setup_all(child);
    }
  }

  root->saveable_settings_setup = true;
}


/* helpers to output tree */

// Internal helper: append segment to prefix into dest buffer
static void sl_build_prefix(char* dest, size_t dest_size, const char* prefix, const char* seg) {
  if (!seg || seg[0] == '\0') {
    if (prefix && prefix[0]) strncpy(dest, prefix, dest_size);
    else dest[0] = '\0';
    return;
  }
  if (!prefix || prefix[0] == '\0') {
    strncpy(dest, seg, dest_size);
    dest[dest_size - 1] = '\0';
  } else {
    // prefix~seg
    snprintf(dest, dest_size, "%s~%s", prefix, seg);
  }
}

// Internal recursive walker that emits lines via callback
static void sl_print_recursive(ISaveableSettingHost* host, const char* prefix, SL_PrintCallback cb, void* ctx, uint8_t depth, uint8_t max_depth, sl_scope_t scope = SL_SCOPE_ALL) {
  if (!host || depth > max_depth) {
    if (depth > max_depth && cb) {
      char linebuf[SL_MAX_LINE];
      snprintf(linebuf, sizeof(linebuf), "%s... (max depth %i reached in %s)", prefix, max_depth, host->path_segment ? host->path_segment : "unknown");
      cb(linebuf, ctx);
    }
    return;
  }

  // Print host header line
  char linebuf[SL_MAX_LINE];
  if (host->path_segment && host->path_segment[0]) {
    if (prefix && prefix[0]) snprintf(linebuf, sizeof(linebuf), "%s~%s:", prefix, host->path_segment);
    else snprintf(linebuf, sizeof(linebuf), "%s:", host->path_segment);
  } else {
    if (prefix && prefix[0]) snprintf(linebuf, sizeof(linebuf), "%s:", prefix);
    else snprintf(linebuf, sizeof(linebuf), "root:");
  }
  if (cb) cb(linebuf, ctx);

  // Emit settings for this host
  for (uint8_t i = 0; i < host->setting_count; ++i) {
    if (!(host->settings[i].mask & scope)) {
      Serial.printf("Skipping setting '%s' for host '%s' due to scope mismatch (slot mask 0x%02X, print scope 0x%02X)\n", host->settings[i].setting->label, host->path_segment, host->settings[i].mask, scope);
      continue;  // not in the requested scope
    }
    SaveableSettingBase* s = host->settings[i].setting;
    if (!s) continue;
    // build full path: prefix~hostseg~key  or hostseg~key if no prefix
    char fullpref[SL_MAX_LINE];
    sl_build_prefix(fullpref, sizeof(fullpref), prefix, host->path_segment);
    // get value string from setting
    const char* kv = s->get_line(); // returns "key=value" or "label=value"
    // ensure we print as fullpath~key=value
    const char* eq = strchr(kv, '=');
    const char* val = eq ? eq + 1 : "";
    if (fullpref[0] == '\0') {
      snprintf(linebuf, sizeof(linebuf), "%s=%s", s->label, val);
    } else {
      snprintf(linebuf, sizeof(linebuf), "%s~%s=%s", fullpref, s->label, val);
    }
    if (cb) cb(linebuf, ctx);
  }

  // Recurse into children
  for (uint8_t c = 0; c < host->child_count; ++c) {
    ISaveableSettingHost* child = host->children[c].host;
    if (!child) continue;
    // build new prefix for child: prefix~hostseg
    char childpref[SL_MAX_LINE];
    sl_build_prefix(childpref, sizeof(childpref), prefix, host->path_segment);
    sl_print_recursive(child, childpref, cb, ctx, depth + 1, max_depth, scope);
  }
}

// Public: print to Arduino Print (Serial)
void sl_print_tree_to_print(ISaveableSettingHost* root, Print& out, uint8_t max_depth, sl_scope_t scope) {
  if (!root) return;
  // callback that writes to Print
  auto write_cb = [](const char* line, void* ctx) {
    Print* p = reinterpret_cast<Print*>(ctx);
    p->println(line);
    p->flush();
  };
  sl_print_recursive(root, "", write_cb, &out, 0, max_depth, scope);
}

// Public: print with user callback
void sl_print_tree_with_callback(ISaveableSettingHost* root, SL_PrintCallback cb, void* user_ctx, uint8_t max_depth, sl_scope_t scope) {
  if (!root || !cb) return;
  sl_print_recursive(root, "", cb, user_ctx, 0, max_depth, scope);
}

// Public: print with vl::Func lambda (no user_ctx needed - lambda captures its own state)
void sl_print_tree_with_lambda(ISaveableSettingHost* root, SL_PrintLambda lambda, uint8_t max_depth, sl_scope_t scope) {
  if (!root) return;
  // bridge: store lambda pointer in ctx, invoke via plain callback
  auto bridge_cb = [](const char* line, void* ctx) {
    (*reinterpret_cast<SL_PrintLambda*>(ctx))(line);
  };
  sl_print_recursive(root, "", bridge_cb, &lambda, 0, max_depth, scope);
}

void debug_print_file(const char *filename) {
  // for some reason we still get that fucking problem where the system freezes when we try to 
  // output lots of serial :/ especially when the app is running, rather than during setup()..
  // tried atomic, yielding, waiting for avail, delays, flushing... nothing seems to fix it
  // dont know what else to try!
  // ATOMIC() {
    Serial.printf("debug_print_file: Attempting to open file '%s' for reading...\n", filename);
    // //Serial.flush();
    #ifdef ENABLE_SD
      File f = SD.open(filename, FILE_READ);
    #elif defined(ENABLE_LITTLEFS)
      File f = LittleFS.open(filename, "r");
    #else
      Serial.println("No filesystem enabled for debug_print_file");
      return;
    #endif

    if (f) {
      Serial.print("###\ndebug_print_file showing contents of ");
      Serial.println(filename);
      while (f.available()) {
          Serial.write(f.read());
          //delay(1);
          //yield();
          //Serial.flush();
      }
      f.close();
      Serial.printf("debug_print_file: Finished showing contents of %s\n", filename);
      //Serial.flush();
    } else {
      Serial.printf("debug_print_file: File '%s' does not exist\n", filename);
      //Serial.flush();
    }
  // }
}