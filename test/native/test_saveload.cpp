#include <vector>
#include <string>

#include <unity.h>

#include "saveloadlib.h"
#include "saveload_settings.h"

class SingleIntHost : public SHStorage<0, 1> {
public:
    int value = 3;

    SingleIntHost() {
        set_path_segment("single");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("value", "SingleIntHost", &value), SL_SCOPE_ALL);
    }
};

class ScopeHost : public SHStorage<0, 1> {
public:
    int value = 10;

    ScopeHost() {
        set_path_segment("scope");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("project_only", "ScopeHost", &value), SL_SCOPE_PROJECT);
    }
};

class LeafHost : public SHStorage<0, 1> {
public:
    int note = 7;

    LeafHost() {
        set_path_segment("leaf");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("note", "LeafHost", &note), SL_SCOPE_ALL);
    }
};

class MidHost : public SHStorage<1, 1> {
public:
    LeafHost* leaf = nullptr;
    bool enabled = true;

    MidHost(LeafHost* l) : leaf(l) {
        set_path_segment("mid");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<bool>("enabled", "MidHost", &enabled), SL_SCOPE_ALL);
        if (leaf != nullptr) {
            leaf->setup_saveable_settings();
            register_child(leaf);
        }
    }
};

class RootHost : public SHStorage<1, 1> {
public:
    MidHost* mid = nullptr;
    int tempo = 120;

    RootHost(MidHost* m) : mid(m) {
        set_path_segment("root");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("tempo", "RootHost", &tempo), SL_SCOPE_ALL);
        if (mid != nullptr) {
            mid->setup_saveable_settings();
            register_child(mid);
        }
    }
};

static void collect_line_cb(const char* line, void* ctx) {
    auto* out = static_cast<std::vector<std::string>*>(ctx);
    out->emplace_back(line ? line : "");
}

static bool load_path_line(ISaveableSettingHost* root, const char* line, sl_scope_t scope = SL_SCOPE_ALL) {
    if (root == nullptr || line == nullptr) return false;

    char buf[SL_MAX_LINE];
    std::strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* eq = std::strchr(buf, '=');
    if (!eq) return false;
    *eq = '\0';

    char* value = eq + 1;
    char* segs[16] = {0};
    int seg_count = 0;

    char* token = std::strtok(buf, "~");
    while (token != nullptr && seg_count < 16) {
        segs[seg_count++] = token;
        token = std::strtok(nullptr, "~");
    }

    if (seg_count <= 0) return false;

    if (root->path_segment && std::strcmp(root->path_segment, segs[0]) == 0) {
        return root->load_line(segs + 1, seg_count - 1, value, scope);
    }
    return root->load_line(segs, seg_count, value, scope);
}

void test_lsaveable_round_trip() {
    SingleIntHost host;
    host.setup_saveable_settings();

    TEST_ASSERT_EQUAL_INT(1, host.setting_count);
    TEST_ASSERT_EQUAL_STRING("value=3", host.settings[0].setting->get_line());

    bool ok = load_path_line(&host, "value=42");
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(42, host.value);
}

void test_scope_filtering_on_load() {
    ScopeHost host;
    host.setup_saveable_settings();

    bool scene_ok = load_path_line(&host, "project_only=99", SL_SCOPE_SCENE);
    TEST_ASSERT_FALSE(scene_ok);
    TEST_ASSERT_EQUAL_INT(10, host.value);

    bool project_ok = load_path_line(&host, "project_only=99", SL_SCOPE_PROJECT);
    TEST_ASSERT_TRUE(project_ok);
    TEST_ASSERT_EQUAL_INT(99, host.value);
}

void test_three_level_nested_route_and_round_trip() {
    LeafHost leaf;
    MidHost mid(&leaf);
    RootHost root(&mid);
    root.setup_saveable_settings();

    std::vector<std::string> lines;
    char prefix[1] = {0};
    root.save_recursive(prefix, 0, collect_line_cb, &lines, SL_SCOPE_ALL);

    bool saw_nested_line = false;
    for (const auto& line : lines) {
        if (line == "mid~leaf~note=7") {
            saw_nested_line = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(saw_nested_line);

    bool ok = load_path_line(&root, "mid~leaf~note=31", SL_SCOPE_ALL);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(31, leaf.note);
}

// Test pattern: Multiple settings per host (like Parameters, Conductor, etc.)
class MultiSettingHost : public SHStorage<0, 5> {
public:
    int tempo = 120;
    float bpm = 120.5f;
    int8_t midi_note = 60;
    bool enabled = true;
    uint8_t velocity = 100;

    MultiSettingHost() {
        set_path_segment("multi");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("tempo", "MultiSettingHost", &tempo), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<float>("bpm", "MultiSettingHost", &bpm), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<int8_t>("midi_note", "MultiSettingHost", &midi_note), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<bool>("enabled", "MultiSettingHost", &enabled), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<uint8_t>("velocity", "MultiSettingHost", &velocity), SL_SCOPE_ALL);
    }
};

void test_multiple_settings_per_host() {
    MultiSettingHost host;
    host.setup_saveable_settings();

    TEST_ASSERT_EQUAL_INT(5, host.setting_count);

    // Test each setting round-trips correctly
    load_path_line(&host, "tempo=180");
    TEST_ASSERT_EQUAL_INT(180, host.tempo);

    load_path_line(&host, "bpm=125.5");
    TEST_ASSERT_EQUAL_FLOAT(125.5f, host.bpm);

    load_path_line(&host, "midi_note=72");
    TEST_ASSERT_EQUAL_INT(72, host.midi_note);

    load_path_line(&host, "enabled=0");
    TEST_ASSERT_FALSE(host.enabled);

    load_path_line(&host, "velocity=64");
    TEST_ASSERT_EQUAL_INT(64, host.velocity);
}

// Test pattern: Negative value handling (seen with MIDI note arrays, offsets)
class NegativeValueHost : public SHStorage<0, 3> {
public:
    int8_t offset = -5;
    int negative_int = -12345;
    float negative_float = -3.14f;

    NegativeValueHost() {
        set_path_segment("negval");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int8_t>("offset", "NegativeValueHost", &offset), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<int>("neg_int", "NegativeValueHost", &negative_int), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<float>("neg_float", "NegativeValueHost", &negative_float), SL_SCOPE_ALL);
    }
};

void test_negative_value_round_trip() {
    NegativeValueHost host;
    host.setup_saveable_settings();

    // Test negative int8_t
    load_path_line(&host, "offset=-127");
    TEST_ASSERT_EQUAL_INT(-127, host.offset);

    // Test negative int32_t
    load_path_line(&host, "neg_int=-999999");
    TEST_ASSERT_EQUAL_INT(-999999, host.negative_int);

    // Test negative float
    load_path_line(&host, "neg_float=-99.99");
    TEST_ASSERT_EQUAL_FLOAT(-99.99f, host.negative_float);
}

// Test pattern: Multiple scope levels (SYSTEM calibration, PROJECT settings, SCENE performance)
class MultiScopeHost : public SHStorage<0, 3> {
public:
    int calibration = 100;   // SYSTEM scope
    int project_tempo = 120; // PROJECT scope
    int scene_volume = 64;   // SCENE scope

    MultiScopeHost() {
        set_path_segment("multiscope");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("calibration", "MultiScopeHost", &calibration), SL_SCOPE_SYSTEM);
        register_setting(new LSaveableSetting<int>("tempo", "MultiScopeHost", &project_tempo), SL_SCOPE_PROJECT);
        register_setting(new LSaveableSetting<int>("volume", "MultiScopeHost", &scene_volume), SL_SCOPE_SCENE);
    }
};

void test_multiple_scope_filtering() {
    MultiScopeHost host;
    host.setup_saveable_settings();

    // SYSTEM scope should only accept SYSTEM settings
    bool system_ok = load_path_line(&host, "calibration=200", SL_SCOPE_SYSTEM);
    TEST_ASSERT_TRUE(system_ok);
    TEST_ASSERT_EQUAL_INT(200, host.calibration);

    bool project_from_system = load_path_line(&host, "tempo=140", SL_SCOPE_SYSTEM);
    TEST_ASSERT_FALSE(project_from_system);

    // PROJECT scope
    bool project_ok = load_path_line(&host, "tempo=140", SL_SCOPE_PROJECT);
    TEST_ASSERT_TRUE(project_ok);
    TEST_ASSERT_EQUAL_INT(140, host.project_tempo);

    // SCENE scope
    bool scene_ok = load_path_line(&host, "volume=80", SL_SCOPE_SCENE);
    TEST_ASSERT_TRUE(scene_ok);
    TEST_ASSERT_EQUAL_INT(80, host.scene_volume);
}

// Test pattern: Deep nesting with multiple scope levels (like SettingsRoot → Components → SubComponents)
class DeepLeafHost : public SHStorage<0, 2> {
public:
    int value_scene = 10;
    int value_project = 20;

    DeepLeafHost() {
        set_path_segment("deepleaf");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("value_scene", "DeepLeafHost", &value_scene), SL_SCOPE_SCENE);
        register_setting(new LSaveableSetting<int>("value_project", "DeepLeafHost", &value_project), SL_SCOPE_PROJECT);
    }
};

class DeepMidHost : public SHStorage<1, 2> {
public:
    DeepLeafHost* leaf = nullptr;
    int mid_value = 50;

    DeepMidHost(DeepLeafHost* l) : leaf(l) {
        set_path_segment("deepmid");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("mid_value", "DeepMidHost", &mid_value), SL_SCOPE_ALL);
        if (leaf != nullptr) {
            leaf->setup_saveable_settings();
            register_child(leaf);
        }
    }
};

class DeepRootHost : public SHStorage<1, 1> {
public:
    DeepMidHost* mid = nullptr;

    DeepRootHost(DeepMidHost* m) : mid(m) {
        set_path_segment("deeproot");
    }

    void setup_saveable_settings() override {
        if (mid != nullptr) {
            mid->setup_saveable_settings();
            register_child(mid);
        }
    }
};

void test_deep_nesting_with_scopes() {
    DeepLeafHost leaf;
    DeepMidHost mid(&leaf);
    DeepRootHost root(&mid);
    root.setup_saveable_settings();

    // Load project-scoped value
    bool project_ok = load_path_line(&root, "deepmid~deepleaf~value_project=99", SL_SCOPE_PROJECT);
    TEST_ASSERT_TRUE(project_ok);
    TEST_ASSERT_EQUAL_INT(99, leaf.value_project);

    // Try loading scene value with wrong scope (should fail)
    bool project_scene_fail = load_path_line(&root, "deepmid~deepleaf~value_scene=77", SL_SCOPE_PROJECT);
    TEST_ASSERT_FALSE(project_scene_fail);
    TEST_ASSERT_EQUAL_INT(10, leaf.value_scene); // unchanged

    // Load scene value with correct scope
    bool scene_ok = load_path_line(&root, "deepmid~deepleaf~value_scene=77", SL_SCOPE_SCENE);
    TEST_ASSERT_TRUE(scene_ok);
    TEST_ASSERT_EQUAL_INT(77, leaf.value_scene);
}

// Test pattern: Verify all settings are saved correctly in recursive save
void test_recursive_save_all_types() {
    MultiSettingHost multi;
    multi.setup_saveable_settings();

    std::vector<std::string> lines;
    char prefix[1] = {0};
    multi.save_recursive(prefix, 0, collect_line_cb, &lines, SL_SCOPE_ALL);

    TEST_ASSERT_EQUAL_INT(5, lines.size());

    // Verify we captured settings for all types
    bool has_tempo = false, has_bpm = false, has_note = false, has_enabled = false, has_velocity = false;
    for (const auto& line : lines) {
        if (line.find("tempo=") == 0) has_tempo = true;
        if (line.find("bpm=") == 0) has_bpm = true;
        if (line.find("midi_note=") == 0) has_note = true;
        if (line.find("enabled=") == 0) has_enabled = true;
        if (line.find("velocity=") == 0) has_velocity = true;
    }

    TEST_ASSERT_TRUE(has_tempo);
    TEST_ASSERT_TRUE(has_bpm);
    TEST_ASSERT_TRUE(has_note);
    TEST_ASSERT_TRUE(has_enabled);
    TEST_ASSERT_TRUE(has_velocity);
}

// Test pattern: Scope filtering on save
void test_scope_filtering_on_save() {
    MultiScopeHost host;
    host.setup_saveable_settings();

    // Save with SYSTEM scope - should only get SYSTEM settings
    std::vector<std::string> system_lines;
    char prefix[1] = {0};
    host.save_recursive(prefix, 0, collect_line_cb, &system_lines, SL_SCOPE_SYSTEM);
    TEST_ASSERT_EQUAL_INT(1, system_lines.size());
    TEST_ASSERT_EQUAL_STRING("calibration=100", system_lines[0].c_str());

    // Save with PROJECT scope
    std::vector<std::string> project_lines;
    host.save_recursive(prefix, 0, collect_line_cb, &project_lines, SL_SCOPE_PROJECT);
    TEST_ASSERT_EQUAL_INT(1, project_lines.size());
    TEST_ASSERT_EQUAL_STRING("tempo=120", project_lines[0].c_str());

    // Save with SCENE scope
    std::vector<std::string> scene_lines;
    host.save_recursive(prefix, 0, collect_line_cb, &scene_lines, SL_SCOPE_SCENE);
    TEST_ASSERT_EQUAL_INT(1, scene_lines.size());
    TEST_ASSERT_EQUAL_STRING("volume=64", scene_lines[0].c_str());

    // Save with ALL scopes
    std::vector<std::string> all_lines;
    host.save_recursive(prefix, 0, collect_line_cb, &all_lines, SL_SCOPE_ALL);
    TEST_ASSERT_EQUAL_INT(3, all_lines.size());
}

// Test pattern: Load all data types from formatted strings
void test_load_formatted_strings() {
    MultiSettingHost host;
    host.setup_saveable_settings();

    // Complex formatted string with special characters and values
    load_path_line(&host, "tempo=250");
    load_path_line(&host, "bpm=99.99");
    load_path_line(&host, "midi_note=1");
    load_path_line(&host, "velocity=127");

    TEST_ASSERT_EQUAL_INT(250, host.tempo);
    TEST_ASSERT_EQUAL_FLOAT(99.99f, host.bpm);
    TEST_ASSERT_EQUAL_INT(1, host.midi_note);
    TEST_ASSERT_EQUAL_INT(127, host.velocity);
}

// Custom Setting Type Test: MIDI Note Array (from seqlib)
// Simulates SaveableMIDINoteArraySetting behavior: stores MIDI notes as individual bytes
class MIDINoteArrayHost : public SHStorage<0, 4> {
public:
    uint8_t note1 = 60;   // C4
    uint8_t note2 = 64;   // E4
    uint8_t note3 = 67;   // G4
    uint8_t note_off = 0xff;  // NOTE_OFF value

    MIDINoteArrayHost() {
        set_path_segment("notearray");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<uint8_t>("note1", "MIDINoteArrayHost", &note1), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<uint8_t>("note2", "MIDINoteArrayHost", &note2), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<uint8_t>("note3", "MIDINoteArrayHost", &note3), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<uint8_t>("note_off", "MIDINoteArrayHost", &note_off), SL_SCOPE_ALL);
    }
};

void test_midi_note_array_encoding() {
    MIDINoteArrayHost host;
    host.setup_saveable_settings();

    // Test round-trip with MIDI notes
    load_path_line(&host, "note1=60");
    load_path_line(&host, "note2=64");
    load_path_line(&host, "note3=67");
    load_path_line(&host, "note_off=255");

    TEST_ASSERT_EQUAL_INT(60, host.note1);
    TEST_ASSERT_EQUAL_INT(64, host.note2);
    TEST_ASSERT_EQUAL_INT(67, host.note3);
    TEST_ASSERT_EQUAL_INT(255, host.note_off);
}

// Custom Setting Type Test: Grid with Offset Encoding (from midihelpers Arranger)
// Simulates SaveableSectionGridSetting: uses offset encoding for negative values
class GridHost : public SHStorage<0, 3> {
public:
    uint8_t degree = 0;      // 0-6 (scale degree)
    uint8_t chord_type = 0;  // 0-3 (maj, min, dim, aug)
    int8_t offset = -2;      // Negative offset for transposition

    GridHost() {
        set_path_segment("grid");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<uint8_t>("degree", "GridHost", &degree), SL_SCOPE_PROJECT);
        register_setting(new LSaveableSetting<uint8_t>("type", "GridHost", &chord_type), SL_SCOPE_PROJECT);
        register_setting(new LSaveableSetting<int8_t>("offset", "GridHost", &offset), SL_SCOPE_PROJECT);
    }
};

void test_grid_with_offset_encoding() {
    GridHost host;
    host.setup_saveable_settings();

    // Load grid chord data
    load_path_line(&host, "degree=2", SL_SCOPE_PROJECT);
    load_path_line(&host, "type=1", SL_SCOPE_PROJECT);
    TEST_ASSERT_EQUAL_INT(2, host.degree);
    TEST_ASSERT_EQUAL_INT(1, host.chord_type);

    // Load negative offset
    load_path_line(&host, "offset=-5", SL_SCOPE_PROJECT);
    TEST_ASSERT_EQUAL_INT(-5, host.offset);

    // Verify grid doesn't load at wrong scope
    bool scene_fail = load_path_line(&host, "degree=3", SL_SCOPE_SCENE);
    TEST_ASSERT_FALSE(scene_fail);
    TEST_ASSERT_EQUAL_INT(2, host.degree);
}

// Custom Setting Type Test: Output Index/Name (from seqlib)
// Simulates LOutputSaveableSetting: uses callback function to resolve names
class OutputCallbackHost : public SHStorage<0, 2> {
public:
    int output_index = 0;
    bool output_enabled = true;

    // Simulate a function callback that maps index to name
    static const char* get_output_name(int idx) {
        static const char* names[] = {"Main", "Alt", "Sub", "CV1", "CV2"};
        if (idx >= 0 && idx < 5) return names[idx];
        return "Unknown";
    }

    OutputCallbackHost() {
        set_path_segment("output");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("output_index", "OutputCallbackHost", &output_index), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<bool>("enabled", "OutputCallbackHost", &output_enabled), SL_SCOPE_ALL);
    }
};

void test_output_name_callback() {
    OutputCallbackHost host;
    host.setup_saveable_settings();

    // Test that output indices map to meaningful names
    TEST_ASSERT_EQUAL_STRING("Main", OutputCallbackHost::get_output_name(0));
    TEST_ASSERT_EQUAL_STRING("Alt", OutputCallbackHost::get_output_name(1));
    TEST_ASSERT_EQUAL_STRING("CV1", OutputCallbackHost::get_output_name(3));

    // Load different output indices
    load_path_line(&host, "output_index=2");
    TEST_ASSERT_EQUAL_INT(2, host.output_index);
    TEST_ASSERT_EQUAL_STRING("Sub", OutputCallbackHost::get_output_name(host.output_index));

    load_path_line(&host, "output_index=4");
    TEST_ASSERT_EQUAL_INT(4, host.output_index);
    TEST_ASSERT_EQUAL_STRING("CV2", OutputCallbackHost::get_output_name(host.output_index));

    load_path_line(&host, "enabled=0");
    TEST_ASSERT_FALSE(host.output_enabled);
}

// Integration Test: Realistic hierarchy from Microlidian architecture
// Simulates: SettingsRoot → Conductor → ChordPlayer (with grid) → various settings
class ConductorSimHost : public SHStorage<0, 4> {
public:
    int tempo = 120;
    int time_signature = 4;  // beats per measure
    int scale = 0;           // C major
    bool follow_clock = true;

    ConductorSimHost() {
        set_path_segment("conductor");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("tempo", "ConductorSimHost", &tempo), SL_SCOPE_PROJECT);
        register_setting(new LSaveableSetting<int>("time_sig", "ConductorSimHost", &time_signature), SL_SCOPE_PROJECT);
        register_setting(new LSaveableSetting<int>("scale", "ConductorSimHost", &scale), SL_SCOPE_PROJECT);
        register_setting(new LSaveableSetting<bool>("follow_clock", "ConductorSimHost", &follow_clock), SL_SCOPE_PROJECT);
    }
};

class ChordPlayerSimHost : public SHStorage<1, 2> {
public:
    GridHost* grid = nullptr;
    int inversion = 0;

    ChordPlayerSimHost(GridHost* g) : grid(g) {
        set_path_segment("chordplayer");
    }

    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("inversion", "ChordPlayerSimHost", &inversion), SL_SCOPE_SCENE);
        if (grid != nullptr) {
            grid->setup_saveable_settings();
            register_child(grid);
        }
    }
};

class RootSimHost : public SHStorage<2, 2> {
public:
    ConductorSimHost* conductor = nullptr;
    ChordPlayerSimHost* player = nullptr;

    RootSimHost(ConductorSimHost* c, ChordPlayerSimHost* p) : conductor(c), player(p) {
        set_path_segment("root");
    }

    void setup_saveable_settings() override {
        if (conductor != nullptr) {
            conductor->setup_saveable_settings();
            register_child(conductor);
        }
        if (player != nullptr) {
            player->setup_saveable_settings();
            register_child(player);
        }
    }
};

void test_realistic_multi_component_hierarchy() {
    GridHost grid;
    ConductorSimHost conductor;
    ChordPlayerSimHost player(&grid);
    RootSimHost root(&conductor, &player);
    root.setup_saveable_settings();

    // Load settings at different scopes
    bool project_tempo = load_path_line(&root, "conductor~tempo=140", SL_SCOPE_PROJECT);
    TEST_ASSERT_TRUE(project_tempo);
    TEST_ASSERT_EQUAL_INT(140, conductor.tempo);

    bool project_grid = load_path_line(&root, "chordplayer~grid~degree=3", SL_SCOPE_PROJECT);
    TEST_ASSERT_TRUE(project_grid);
    TEST_ASSERT_EQUAL_INT(3, grid.degree);

    bool scene_inversion = load_path_line(&root, "chordplayer~inversion=2", SL_SCOPE_SCENE);
    TEST_ASSERT_TRUE(scene_inversion);
    TEST_ASSERT_EQUAL_INT(2, player.inversion);

    // Verify scope isolation: scene inversion shouldn't load at project scope
    bool scene_inversion_fail = load_path_line(&root, "chordplayer~inversion=1", SL_SCOPE_PROJECT);
    TEST_ASSERT_FALSE(scene_inversion_fail);
    TEST_ASSERT_EQUAL_INT(2, player.inversion);
}

// Edge case: Very deep nesting (stress test)
class Level4Host : public SHStorage<0, 1> {
public:
    int value = 4;
    Level4Host() { set_path_segment("l4"); }
    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("val", "Level4Host", &value), SL_SCOPE_ALL);
    }
};

class Level3Host : public SHStorage<1, 1> {
public:
    Level4Host* l4 = nullptr;
    int value = 3;
    Level3Host(Level4Host* l) : l4(l) { set_path_segment("l3"); }
    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("val", "Level3Host", &value), SL_SCOPE_ALL);
        if (l4) { l4->setup_saveable_settings(); register_child(l4); }
    }
};

class Level2Host : public SHStorage<1, 1> {
public:
    Level3Host* l3 = nullptr;
    int value = 2;
    Level2Host(Level3Host* l) : l3(l) { set_path_segment("l2"); }
    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("val", "Level2Host", &value), SL_SCOPE_ALL);
        if (l3) { l3->setup_saveable_settings(); register_child(l3); }
    }
};

class Level1Host : public SHStorage<1, 1> {
public:
    Level2Host* l2 = nullptr;
    Level1Host(Level2Host* l) : l2(l) { set_path_segment("l1"); }
    void setup_saveable_settings() override {
        if (l2) { l2->setup_saveable_settings(); register_child(l2); }
    }
};

void test_deeply_nested_structure() {
    Level4Host l4;
    Level3Host l3(&l4);
    Level2Host l2(&l3);
    Level1Host l1(&l2);
    l1.setup_saveable_settings();

    // Load deeply nested value: l1~l2~l3~l4~val=99
    bool deep_load = load_path_line(&l1, "l1~l2~l3~l4~val=99", SL_SCOPE_ALL);
    TEST_ASSERT_TRUE(deep_load);
    TEST_ASSERT_EQUAL_INT(99, l4.value);

    // Save and verify structure
    std::vector<std::string> lines;
    char prefix[1] = {0};
    l1.save_recursive(prefix, 0, collect_line_cb, &lines, SL_SCOPE_ALL);
    TEST_ASSERT_TRUE(lines.size() >= 3);  // Should have at least l2, l3, l4 values
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_lsaveable_round_trip);
    RUN_TEST(test_scope_filtering_on_load);
    RUN_TEST(test_three_level_nested_route_and_round_trip);
    RUN_TEST(test_multiple_settings_per_host);
    RUN_TEST(test_negative_value_round_trip);
    RUN_TEST(test_multiple_scope_filtering);
    RUN_TEST(test_deep_nesting_with_scopes);
    RUN_TEST(test_recursive_save_all_types);
    RUN_TEST(test_scope_filtering_on_save);
    RUN_TEST(test_load_formatted_strings);
    RUN_TEST(test_midi_note_array_encoding);
    RUN_TEST(test_grid_with_offset_encoding);
    RUN_TEST(test_output_name_callback);
    RUN_TEST(test_realistic_multi_component_hierarchy);
    RUN_TEST(test_deeply_nested_structure);
    return UNITY_END();
}
