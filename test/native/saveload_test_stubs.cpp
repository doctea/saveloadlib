#include "Arduino.h"
#include "saveloadlib.h"

SerialStub Serial;

const char* warning_label = " - WARNING: no target nor getter func!";
char linebuf[SL_MAX_LINE] = {0};

ISaveableSettingHost* SL_ROOT = nullptr;
SL_ArenaBase* sl_setting_arena = nullptr;
SL_TreeCounts sl_cached_tree_counts = {0, 0, 0};
bool sl_tree_counts_valid = false;
