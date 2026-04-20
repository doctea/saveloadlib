#include <vector>
#include <chrono>
#include <cstdio>

#include "saveloadlib.h"
#include "saveload_settings.h"

// Reuse test host classes from test_saveload.cpp
class SingleIntHost : public SHStorage<0, 1> {
public:
    int value = 3;
    SingleIntHost() { set_path_segment("single"); }
    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("value", "SingleIntHost", &value), SL_SCOPE_ALL);
    }
};

class MultiSettingHost : public SHStorage<0, 5> {
public:
    int tempo = 120;
    float bpm = 120.5f;
    int8_t midi_note = 60;
    bool enabled = true;
    uint8_t velocity = 100;
    MultiSettingHost() { set_path_segment("multi"); }
    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("tempo", "MultiSettingHost", &tempo), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<float>("bpm", "MultiSettingHost", &bpm), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<int8_t>("midi_note", "MultiSettingHost", &midi_note), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<bool>("enabled", "MultiSettingHost", &enabled), SL_SCOPE_ALL);
        register_setting(new LSaveableSetting<uint8_t>("velocity", "MultiSettingHost", &velocity), SL_SCOPE_ALL);
    }
};

class GridHost : public SHStorage<0, 3> {
public:
    uint8_t degree = 0;
    uint8_t chord_type = 0;
    int8_t offset = -2;
    GridHost() { set_path_segment("grid"); }
    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<uint8_t>("degree", "GridHost", &degree), SL_SCOPE_PROJECT);
        register_setting(new LSaveableSetting<uint8_t>("type", "GridHost", &chord_type), SL_SCOPE_PROJECT);
        register_setting(new LSaveableSetting<int8_t>("offset", "GridHost", &offset), SL_SCOPE_PROJECT);
    }
};

class DeepLeafHost : public SHStorage<0, 2> {
public:
    int value_scene = 10;
    int value_project = 20;
    DeepLeafHost() { set_path_segment("deepleaf"); }
    void setup_saveable_settings() override {
        register_setting(new LSaveableSetting<int>("value_scene", "DeepLeafHost", &value_scene), SL_SCOPE_SCENE);
        register_setting(new LSaveableSetting<int>("value_project", "DeepLeafHost", &value_project), SL_SCOPE_PROJECT);
    }
};

class DeepMidHost : public SHStorage<1, 2> {
public:
    DeepLeafHost* leaf = nullptr;
    int mid_value = 50;
    DeepMidHost(DeepLeafHost* l) : leaf(l) { set_path_segment("deepmid"); }
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
    DeepRootHost(DeepMidHost* m) : mid(m) { set_path_segment("deeproot"); }
    void setup_saveable_settings() override {
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

// ============================================================================
// TIER 1: LIGHTWEIGHT - Instance Sizes (quick baseline)
// ============================================================================

void benchmark_instance_sizes() {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║ TIER 1: MEMORY - Instance Sizes                            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    printf("BENCH: SingleIntHost            = %3zu bytes\n", sizeof(SingleIntHost));
    printf("BENCH: MultiSettingHost (5 settings) = %3zu bytes\n", sizeof(MultiSettingHost));
    printf("BENCH: GridHost (3 settings)    = %3zu bytes\n", sizeof(GridHost));
    printf("BENCH: DeepLeafHost (2 settings) = %3zu bytes\n", sizeof(DeepLeafHost));
    printf("BENCH: DeepMidHost (with leaf)  = %3zu bytes\n", sizeof(DeepMidHost));
    printf("BENCH: DeepRootHost (with mid)  = %3zu bytes\n", sizeof(DeepRootHost));
    printf("\n");
}

// ============================================================================
// TIER 2: STANDARD - Performance Benchmarks
// ============================================================================

void benchmark_save_performance_simple() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║ TIER 2: PERFORMANCE - Save Operations                      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // Benchmark: Single setting save
    {
        SingleIntHost host;
        host.setup_saveable_settings();
        std::vector<std::string> lines;
        char prefix[1] = {0};

        auto start = std::chrono::high_resolution_clock::now();
        const int ITERATIONS = 10000;
        for (int i = 0; i < ITERATIONS; i++) {
            lines.clear();
            host.save_recursive(prefix, 0, collect_line_cb, &lines, SL_SCOPE_ALL);
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        printf("BENCH: Save 1 setting x%d:    %.3f ms (%.2f us/op, %.1f ops/ms)\n",
               ITERATIONS, duration_us / 1000.0, (double)duration_us / ITERATIONS, ITERATIONS * 1000.0 / duration_us);
    }

    // Benchmark: Multiple settings save
    {
        MultiSettingHost host;
        host.setup_saveable_settings();
        std::vector<std::string> lines;
        char prefix[1] = {0};

        auto start = std::chrono::high_resolution_clock::now();
        const int ITERATIONS = 5000;
        for (int i = 0; i < ITERATIONS; i++) {
            lines.clear();
            host.save_recursive(prefix, 0, collect_line_cb, &lines, SL_SCOPE_ALL);
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        printf("BENCH: Save 5 settings x%d:   %.3f ms (%.2f us/op, %.1f ops/ms)\n",
               ITERATIONS, duration_us / 1000.0, (double)duration_us / ITERATIONS, ITERATIONS * 1000.0 / duration_us);
    }

    printf("\n");
}

void benchmark_load_performance_simple() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║ TIER 2: PERFORMANCE - Load Operations                      ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // Benchmark: Single setting load
    {
        SingleIntHost host;
        host.setup_saveable_settings();

        auto start = std::chrono::high_resolution_clock::now();
        const int ITERATIONS = 10000;
        for (int i = 0; i < ITERATIONS; i++) {
            char buf[256] = "value=42";
            char* eq = strchr(buf, '=');
            if (eq) {
                *eq = '\0';
                char* value = eq + 1;
                char* segments[] = {buf};
                host.load_line(segments, 1, value, SL_SCOPE_ALL);
            }
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        printf("BENCH: Load 1 setting x%d:   %.3f ms (%.2f us/op, %.1f ops/ms)\n",
               ITERATIONS, duration_us / 1000.0, (double)duration_us / ITERATIONS, ITERATIONS * 1000.0 / duration_us);
    }

    printf("\n");
}

void benchmark_hierarchy_performance() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║ TIER 2: PERFORMANCE - Hierarchy Operations                 ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // Benchmark: 2-level hierarchy (mid + leaf)
    {
        DeepLeafHost leaf;
        DeepMidHost mid(&leaf);
        mid.setup_saveable_settings();
        std::vector<std::string> lines;
        char prefix[1] = {0};

        auto start = std::chrono::high_resolution_clock::now();
        const int ITERATIONS = 5000;
        for (int i = 0; i < ITERATIONS; i++) {
            lines.clear();
            mid.save_recursive(prefix, 0, collect_line_cb, &lines, SL_SCOPE_ALL);
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        printf("BENCH: Save 2-level hierarchy (3 settings) x%d:  %.3f ms (%.2f us/op)\n",
               ITERATIONS, duration_us / 1000.0, (double)duration_us / ITERATIONS);
        printf("       Lines generated per op: %zu\n", lines.size());
    }

    // Benchmark: 3-level hierarchy (root + mid + leaf)
    {
        DeepLeafHost leaf;
        DeepMidHost mid(&leaf);
        DeepRootHost root(&mid);
        root.setup_saveable_settings();
        std::vector<std::string> lines;
        char prefix[1] = {0};

        auto start = std::chrono::high_resolution_clock::now();
        const int ITERATIONS = 5000;
        for (int i = 0; i < ITERATIONS; i++) {
            lines.clear();
            root.save_recursive(prefix, 0, collect_line_cb, &lines, SL_SCOPE_ALL);
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        printf("BENCH: Save 3-level hierarchy (4 settings) x%d:  %.3f ms (%.2f us/op)\n",
               ITERATIONS, duration_us / 1000.0, (double)duration_us / ITERATIONS);
        printf("       Lines generated per op: %zu\n", lines.size());
    }

    printf("\n");
}

// ============================================================================
// TIER 3: STRESS TEST - High load scenarios
// ============================================================================

void benchmark_stress_test() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║ TIER 3: STRESS - High Iteration Load                       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // Stress: Many rapid saves
    {
        MultiSettingHost host;
        host.setup_saveable_settings();
        std::vector<std::string> lines;
        char prefix[1] = {0};

        auto start = std::chrono::high_resolution_clock::now();
        const int ITERATIONS = 50000;
        for (int i = 0; i < ITERATIONS; i++) {
            lines.clear();
            host.save_recursive(prefix, 0, collect_line_cb, &lines, SL_SCOPE_ALL);
            // Simulate value changes
            host.tempo = (host.tempo + 1) % 200;
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        printf("BENCH: Save + modify x%d:    %ld ms (%.2f us/op, %.0f ops/s)\n",
               ITERATIONS, duration_ms, (double)(duration_ms * 1000) / ITERATIONS, 
               ITERATIONS * 1000.0 / duration_ms);
    }

    // Stress: Scope filtering at scale
    {
        MultiSettingHost host;
        host.setup_saveable_settings();
        std::vector<std::string> lines;
        char prefix[1] = {0};

        auto start = std::chrono::high_resolution_clock::now();
        const int ITERATIONS = 50000;
        for (int i = 0; i < ITERATIONS; i++) {
            lines.clear();
            // Alternate between scopes
            sl_scope_t scope = (i % 3 == 0) ? SL_SCOPE_ALL : (i % 2 == 0 ? SL_SCOPE_PROJECT : SL_SCOPE_SCENE);
            host.save_recursive(prefix, 0, collect_line_cb, &lines, scope);
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        printf("BENCH: Save with scope filter x%d: %ld ms (%.2f us/op)\n",
               ITERATIONS, duration_ms, (double)(duration_ms * 1000) / ITERATIONS);
    }

    printf("\n");
}

// ============================================================================
// SUMMARY & RECOMMENDATIONS
// ============================================================================

void print_benchmark_summary() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║ BENCHMARK SUMMARY & USAGE RECOMMENDATIONS                  ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\nRun benchmarks regularly to:\n");
    printf("  1. Establish baseline performance (current commit)\n");
    printf("  2. Compare before/after optimization changes\n");
    printf("  3. Detect performance regressions\n");
    printf("  4. Track memory growth as features are added\n");
    printf("\nHow to compare runs:\n");
    printf("  - Save output to file: ./scripts/run_bench.sh > bench_run_1.txt\n");
    printf("  - Make changes to saveloadlib\n");
    printf("  - Run again:              ./scripts/run_bench.sh > bench_run_2.txt\n");
    printf("  - Diff:                   diff bench_run_1.txt bench_run_2.txt\n");
    printf("\nTier usage:\n");
    printf("  - TIER 1 (Memory):     Run every commit - catches size regressions\n");
    printf("  - TIER 2 (Perf):       Run before/after optimizations\n");
    printf("  - TIER 3 (Stress):     Run when investigating edge cases\n");
    printf("\n");
}

// ============================================================================
// MAIN - Run all benchmarks
// ============================================================================

#ifdef SL_BENCH_MAIN
int main(int argc, char** argv) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║ saveloadlib BENCHMARKS                                     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");

    // Tier 1: Memory
    benchmark_instance_sizes();

    // Tier 2: Performance
    benchmark_save_performance_simple();
    benchmark_load_performance_simple();
    benchmark_hierarchy_performance();

    // Tier 3: Stress (optional - comment out if slow)
    benchmark_stress_test();

    // Summary
    print_benchmark_summary();

    return 0;
}
#endif
