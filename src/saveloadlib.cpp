// saveloadlib.cpp
#include "saveloadlib.h"

const char *warning_label = " - WARNING: no target nor getter func!";

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