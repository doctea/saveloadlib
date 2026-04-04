// example_usage.cpp
#include "saveloadlib.h"

// Simple integer Setting
struct IntSetting : SaveableSettingBase {
  int* target;
  IntSetting(const char* lbl, int* t) { label = lbl; target = t; }
  const char* get_line() override {
    static char buf[64];
    snprintf(buf, sizeof(buf), "%s=%d", label, *target);
    return buf;
  }
  bool parse_key_value(const char* key, const char* value) override {
    *target = atoi(value);
    return true;
  }
};

// Define hosts
ISaveableSettingHost ProjectRoot = { "project", 0 };
ISaveableSettingHost BehaviourHost = { "behaviour", 0 };

// Example setting
int tempo = 120;
IntSetting tempoSetting("tempo", &tempo);

void setup_save_example() {
  // compute hashes
  ProjectRoot.seg_hash = sl_fnv1a_16(ProjectRoot.path_segment);
  BehaviourHost.seg_hash = sl_fnv1a_16(BehaviourHost.path_segment);

  // register tree
  ProjectRoot.register_child(&BehaviourHost);
  BehaviourHost.register_setting(&tempoSetting);

  // register root
  sl_register_root(&ProjectRoot);
}

// Save and load usage
void save_example() { sl_save_to_file(&ProjectRoot, "/project.txt"); }
void load_example() { sl_load_from_file("/project.txt"); }



////////////////////////////
//Example using vl::Func<> from functional‑vlpp.h

// example_vlpp.cpp (illustrative)
#include "saveload_settings_callable_nostd.h"
#include "functional-vlpp.h" // your header providing vl::Func

// hosts
ISaveableSettingHost ProjectRoot = { "project", 0 };
ISaveableSettingHost BehaviourHost = { "behaviour", 0 };

// variable
int density0 = 7;

// create vl::Func callables (syntax depends on your vlpp usage; adapt if needed)
vl::Func<void(int)> vl_set = [&](int v){ density0 = v; };
vl::Func<int()> vl_get = [&]()->int { return density0; };

// instantiate template using vl::Func types
using MyDensitySetting = LSaveableSettingCallableNoStd<int, decltype(vl_set), decltype(vl_get)>;

MyDensitySetting densitySetting("global_density_0", "Euclidian", &density0, vl_set, vl_get);

void setup_saveables() {
  ProjectRoot.seg_hash = sl_fnv1a_16(ProjectRoot.path_segment);
  BehaviourHost.seg_hash = sl_fnv1a_16(BehaviourHost.path_segment);

  ProjectRoot.register_child(&BehaviourHost);
  BehaviourHost.register_setting(&densitySetting);
  sl_register_root(&ProjectRoot);
}
