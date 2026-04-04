// saveloadlib.cpp
#include "saveloadlib.h"

const char *warning_label = " - WARNING: no target nor getter func!";

char linebuf[SL_MAX_LINE];  // shared buffer for constructing lines to save

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

bool sl_parse_line_buffer(char* linebuf) {
  size_t L = strlen(linebuf);
  while (L && (linebuf[L-1] == '\n' || linebuf[L-1] == '\r')) linebuf[--L] = '\0';
  char* eq = strchr(linebuf, '=');
  if (!eq) return false;
  *eq = '\0';
  char* left = linebuf;
  char* value = eq + 1;

  const int MAX_SEGS = 32;
  static char* segs[MAX_SEGS];
  int seg_count = sl_tokenise_inplace(left, segs, MAX_SEGS);
  if (seg_count <= 0) return false;
  if (!SL_ROOT) return false;

  if (SL_ROOT->path_segment && strcmp(SL_ROOT->path_segment, segs[0]) == 0) {
    return SL_ROOT->load_line(segs + 1, seg_count - 1, value);
  } else {
    return SL_ROOT->load_line(segs, seg_count, value);
  }
}

// Arduino SD/LittleFS streaming loader
bool sl_load_from_file(const char* path) {
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
    sl_parse_line_buffer(linebuf);
    yield();
  }
  f.close();
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

bool sl_save_to_file(ISaveableSettingHost* root, const char* path) {
  if (!root) return false;
  if (!globalFileWriter.begin(path)) return false;
  char prefix[1] = {0};
  root->save_recursive(prefix, 0, sl_file_output_cb);
  globalFileWriter.end();
  return true;
}

void sl_setup_all(ISaveableSettingHost* root) {
  //Serial.printf("sl_setup_all() for root %p aka %s\n", root, root ? root->path_segment : "null"); Serial.flush();
  //if (!root) return;
  // Ensure hashes are computed so replace/find operations are fast during setup.
  sl_compute_hashes_recursive(root);

  // Use a simple stackless recursion; depth is expected to be small.
  root->setup_saveable_settings();
  for (uint8_t i = 0; i < root->child_count; ++i) {
    ISaveableSettingHost* child = root->children[i].host;
    if (child) sl_setup_all(child);
  }
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
static void sl_print_recursive(ISaveableSettingHost* host, const char* prefix, SL_PrintCallback cb, void* ctx, uint8_t depth, uint8_t max_depth) {
  if (!host || depth > max_depth) return;

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
    sl_print_recursive(child, childpref, cb, ctx, depth + 1, max_depth);
  }
}

// Public: print to Arduino Print (Serial)
void sl_print_tree_to_print(ISaveableSettingHost* root, Print& out, uint8_t max_depth) {
  if (!root) return;
  // callback that writes to Print
  auto write_cb = [](const char* line, void* ctx) {
    Print* p = reinterpret_cast<Print*>(ctx);
    p->println(line);
    p->flush();
  };
  sl_print_recursive(root, "", write_cb, &out, 0, max_depth);
}

// Public: print with user callback
void sl_print_tree_with_callback(ISaveableSettingHost* root, SL_PrintCallback cb, void* user_ctx, uint8_t max_depth) {
  if (!root || !cb) return;
  sl_print_recursive(root, "", cb, user_ctx, 0, max_depth);
}
