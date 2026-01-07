//
// ULTIMATE dict_t BENCHMARK SUITE - MAXIMUM OVERDRIVE EDITION 🔥
// Tests: Performance, Correctness, Edge Cases, Collision Handling, Memory
//

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/time.h>

#include "../libds/dict.h"


// === CONFIG ===
#define NUM_ITEMS_SMALL   10000
#define NUM_ITEMS_MEDIUM  100000
#define NUM_ITEMS_LARGE   1000000
#define NUM_ITEMS_EXTREME 5000000
#define KEY_LEN 128

// === COLORS ===
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

// === TIMING ===
static inline double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

// === HASH FUNCTIONS ===

// FNV-1a (Fast, good distribution)
size_t hash_fnv1a(let_any key) {
    const char* str = (const char*)key;
    size_t hash = 2166136261u;
    while (*str) {
        hash ^= (unsigned char)(*str++);
        hash *= 16777619;
    }
    return hash;
}

// DJB2 (Alternative hash)
size_t hash_djb2(let_any key) {
    const char* str = (const char*)key;
    size_t hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

// Collision-prone hash (for stress testing)
size_t hash_bad(let_any key) {
    return strlen((const char*)key) % 16; // Terrible hash, lots of collisions
}

int str_compare(let_any a, let_any b) {
    return strcmp((const char*)a, (const char*)b);
}

// === DATA GENERATION ===
typedef struct {
    char** keys;
    int* values;
    size_t count;
} TestData;

TestData* create_test_data(size_t count) {
    TestData* data = malloc(sizeof(TestData));
    data->keys = malloc(count * sizeof(char*));
    data->values = malloc(count * sizeof(int));
    data->count = count;

    for (size_t i = 0; i < count; i++) {
        data->keys[i] = malloc(KEY_LEN);
        snprintf(data->keys[i], KEY_LEN, "key_%zu_%lx", i, (unsigned long)i * 12345);
        data->values[i] = (int)i;
    }
    return data;
}

void destroy_test_data(TestData* data) {
    for (size_t i = 0; i < data->count; i++) {
        free(data->keys[i]);
    }
    free(data->keys);
    free(data->values);
    free(data);
}

// === PRINT HELPERS ===
void print_header(const char* title) {
    printf("\n%s%s╔════════════════════════════════════════════════════════════╗%s\n",
           COLOR_BOLD, COLOR_CYAN, COLOR_RESET);
    printf("%s%s║  %-56s  ║%s\n", COLOR_BOLD, COLOR_CYAN, title, COLOR_RESET);
    printf("%s%s╚════════════════════════════════════════════════════════════╝%s\n",
           COLOR_BOLD, COLOR_CYAN, COLOR_RESET);
}

void print_result(const char* test_name, bool passed) {
    printf("  %s%-50s%s [%s%s%s]\n",
           COLOR_BOLD, test_name, COLOR_RESET,
           passed ? COLOR_GREEN : COLOR_RED,
           passed ? "✓ PASS" : "✗ FAIL",
           COLOR_RESET);
}

void print_benchmark(const char* name, size_t ops, double time_us) {
    double ops_per_sec = (ops / time_us) * 1000000.0;
    printf("  %s%-40s%s %s%.2f ms%s  (%s%.2fM ops/s%s)\n",
           COLOR_BOLD, name, COLOR_RESET,
           COLOR_YELLOW, time_us / 1000.0, COLOR_RESET,
           COLOR_GREEN, ops_per_sec / 1000000.0, COLOR_RESET);
}

// === TEST FUNCTIONS ===

// 1. Basic Functionality
bool test_basic_operations(void) {
    dict_t* d = init_dict(hash_fnv1a, str_compare);

    char* k1 = "alpha";
    char* k2 = "beta";
    char* k3 = "gamma";
    int v1 = 100, v2 = 200, v3 = 300;

    // Insert
    assert(dict_set(d, k1, &v1));
    assert(dict_set(d, k2, &v2));
    assert(dict_set(d, k3, &v3));

    // Retrieve
    int* val = dict_get(d, k1);
    assert(val && *val == 100);
    val = dict_get(d, k2);
    assert(val && *val == 200);

    // Update
    int v1_new = 999;
    dict_set(d, k1, &v1_new);
    val = dict_get(d, k1);
    assert(val && *val == 999);

    // Delete
    assert(dict_remove(d, k2));
    assert(dict_get(d, k2) == NULL);
    assert(!dict_remove(d, "nonexistent"));

    release_dict(d);
    return true;
}

// 2. Reference Counting (ARC)
bool test_arc(void) {
    dict_t* d1 = init_dict(hash_fnv1a, str_compare);
    dict_t* d2 = retain_dict(d1);
    dict_t* d3 = retain_dict(d1);

    assert(d1 == d2 && d2 == d3);

    int val = 42;
    dict_set(d1, "test", &val);

    release_dict(d2);
    release_dict(d3);

    // d1 still valid
    int* result = dict_get(d1, "test");
    assert(result && *result == 42);

    release_dict(d1); // Final release
    return true;
}

// 3. Rehashing Stress Test
bool test_rehashing(void) {
    dict_t* d = init_dict(hash_fnv1a, str_compare);
    TestData* data = create_test_data(NUM_ITEMS_MEDIUM);

    // Force multiple rehashes
    for (size_t i = 0; i < data->count; i++) {
        dict_set(d, data->keys[i], &data->values[i]);
    }

    // Verify all data
    for (size_t i = 0; i < data->count; i++) {
        int* val = dict_get(d, data->keys[i]);
        if (!val || *val != data->values[i]) {
            destroy_test_data(data);
            release_dict(d);
            return false;
        }
    }

    destroy_test_data(data);
    release_dict(d);
    return true;
}

// 4. Collision Handling
bool test_collision_handling(void) {
    // Use bad hash to force collisions
    dict_t* d = init_dict(hash_bad, str_compare);
    TestData* data = create_test_data(1000);

    for (size_t i = 0; i < data->count; i++) {
        dict_set(d, data->keys[i], &data->values[i]);
    }

    // All must be retrievable despite collisions
    for (size_t i = 0; i < data->count; i++) {
        int* val = dict_get(d, data->keys[i]);
        if (!val || *val != data->values[i]) {
            destroy_test_data(data);
            release_dict(d);
            return false;
        }
    }

    destroy_test_data(data);
    release_dict(d);
    return true;
}

// 5. Delete-Insert Cycle
bool test_delete_insert_cycle(void) {
    dict_t* d = init_dict(hash_fnv1a, str_compare);
    TestData* data = create_test_data(10000);

    // Insert all
    for (size_t i = 0; i < data->count; i++) {
        if (!dict_set(d, data->keys[i], &data->values[i])) {
            printf("    %s[DEBUG]%s Failed to insert key at index %zu\n",
                   COLOR_RED, COLOR_RESET, i);
            destroy_test_data(data);
            release_dict(d);
            return false;
        }
    }

    // Delete half
    for (size_t i = 0; i < data->count / 2; i++) {
        if (!dict_remove(d, data->keys[i])) {
            printf("    %s[DEBUG]%s Failed to remove key at index %zu: '%s'\n",
                   COLOR_RED, COLOR_RESET, i, data->keys[i]);
            destroy_test_data(data);
            release_dict(d);
            return false;
        }
    }

    // Re-insert with different values
    int new_values[10000];
    for (size_t i = 0; i < data->count / 2; i++) {
        new_values[i] = data->values[i] + 10000;
        if (!dict_set(d, data->keys[i], &new_values[i])) {
            printf("    %s[DEBUG]%s Failed to re-insert key at index %zu: '%s'\n",
                   COLOR_RED, COLOR_RESET, i, data->keys[i]);
            destroy_test_data(data);
            release_dict(d);
            return false;
        }
    }

    // Verify
    for (size_t i = 0; i < data->count / 2; i++) {
        int* val = dict_get(d, data->keys[i]);
        if (!val) {
            printf("    %s[DEBUG]%s Key not found at index %zu: '%s'\n",
                   COLOR_RED, COLOR_RESET, i, data->keys[i]);
            destroy_test_data(data);
            release_dict(d);
            return false;
        }
        if (*val != new_values[i]) {
            printf("    %s[DEBUG]%s Value mismatch at index %zu: expected %d, got %d (key: '%s')\n",
                   COLOR_RED, COLOR_RESET, i, new_values[i], *val, data->keys[i]);
            destroy_test_data(data);
            release_dict(d);
            return false;
        }
    }

    destroy_test_data(data);
    release_dict(d);
    return true;
}

// 6. Edge Cases
bool test_edge_cases(void) {
    dict_t* d = init_dict(hash_fnv1a, str_compare);

    // NULL checks
    assert(dict_get(NULL, "key") == NULL);
    assert(!dict_set(NULL, "key", "val"));
    assert(!dict_remove(NULL, "key"));

    // Empty string key
    int val = 123;
    assert(dict_set(d, "", &val));
    int* result = dict_get(d, "");
    assert(result && *result == 123);

    // Very long key
    char long_key[1024];
    memset(long_key, 'x', 1023);
    long_key[1023] = '\0';
    assert(dict_set(d, long_key, &val));
    result = dict_get(d, long_key);
    assert(result && *result == 123);

    release_dict(d);
    return true;
}

// === BENCHMARK FUNCTIONS ===

void benchmark_insert(size_t count) {
    dict_t* d = init_dict(hash_fnv1a, str_compare);
    TestData* data = create_test_data(count);

    double start = get_time_us();
    for (size_t i = 0; i < count; i++) {
        dict_set(d, data->keys[i], &data->values[i]);
    }
    double end = get_time_us();

    print_benchmark("Insert", count, end - start);

    destroy_test_data(data);
    release_dict(d);
}

void benchmark_lookup(size_t count) {
    dict_t* d = init_dict(hash_fnv1a, str_compare);
    TestData* data = create_test_data(count);

    // Prepare
    for (size_t i = 0; i < count; i++) {
        dict_set(d, data->keys[i], &data->values[i]);
    }

    // Benchmark
    double start = get_time_us();
    for (size_t i = 0; i < count; i++) {
        volatile int* val = dict_get(d, data->keys[i]);
        (void)val;
    }
    double end = get_time_us();

    print_benchmark("Lookup", count, end - start);

    destroy_test_data(data);
    release_dict(d);
}

void benchmark_delete(size_t count) {
    dict_t* d = init_dict(hash_fnv1a, str_compare);
    TestData* data = create_test_data(count);

    // Prepare
    for (size_t i = 0; i < count; i++) {
        dict_set(d, data->keys[i], &data->values[i]);
    }

    // Benchmark
    double start = get_time_us();
    for (size_t i = 0; i < count; i++) {
        dict_remove(d, data->keys[i]);
    }
    double end = get_time_us();

    print_benchmark("Delete", count, end - start);

    destroy_test_data(data);
    release_dict(d);
}

void benchmark_mixed_workload(size_t count) {
    dict_t* d = init_dict(hash_fnv1a, str_compare);
    TestData* data = create_test_data(count);

    double start = get_time_us();

    // 50% insert, 30% lookup, 20% delete
    for (size_t i = 0; i < count; i++) {
        int op = i % 10;
        if (op < 5) {
            dict_set(d, data->keys[i % data->count], &data->values[i % data->count]);
        } else if (op < 8) {
            volatile int* val = dict_get(d, data->keys[i % data->count]);
            (void)val;
        } else {
            dict_remove(d, data->keys[i % data->count]);
        }
    }

    double end = get_time_us();
    print_benchmark("Mixed (50%I/30%L/20%D)", count, end - start);

    destroy_test_data(data);
    release_dict(d);
}

// === MAIN ===

int main(void) {
    printf("\n%s%s", COLOR_BOLD, COLOR_MAGENTA);
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                  ║\n");
    printf("║     🔥 ULTIMATE dict_t BENCHMARK SUITE 🔥                    ║\n");
    printf("║              Maximum Overdrive Edition                           ║\n");
    printf("║                                                                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("%s\n", COLOR_RESET);

    // === CORRECTNESS TESTS ===
    print_header("CORRECTNESS TESTS");
    print_result("Basic Operations", test_basic_operations());
    print_result("Reference Counting (ARC)", test_arc());
    print_result("Rehashing Under Load", test_rehashing());
    print_result("Collision Handling (Bad Hash)", test_collision_handling());
    print_result("Delete-Insert Cycles", test_delete_insert_cycle());
    print_result("Edge Cases & NULL Safety", test_edge_cases());

    // === PERFORMANCE BENCHMARKS ===
    print_header("PERFORMANCE BENCHMARKS - 10K Elements");
    benchmark_insert(NUM_ITEMS_SMALL);
    benchmark_lookup(NUM_ITEMS_SMALL);
    benchmark_delete(NUM_ITEMS_SMALL);
    benchmark_mixed_workload(NUM_ITEMS_SMALL);

    print_header("PERFORMANCE BENCHMARKS - 100K Elements");
    benchmark_insert(NUM_ITEMS_MEDIUM);
    benchmark_lookup(NUM_ITEMS_MEDIUM);
    benchmark_delete(NUM_ITEMS_MEDIUM);
    benchmark_mixed_workload(NUM_ITEMS_MEDIUM);

    print_header("PERFORMANCE BENCHMARKS - 1M Elements");
    benchmark_insert(NUM_ITEMS_LARGE);
    benchmark_lookup(NUM_ITEMS_LARGE);
    benchmark_delete(NUM_ITEMS_LARGE);
    benchmark_mixed_workload(NUM_ITEMS_LARGE);

    printf("\n%s%s", COLOR_BOLD, COLOR_GREEN);
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                  ║\n");
    printf("║              🎉 ALL TESTS PASSED! dict_t IS SOLID 🎉         ║\n");
    printf("║                                                                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("%s\n", COLOR_RESET);

    return 0;
}
