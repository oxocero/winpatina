/**
 * @file test_harness.h
 * @brief Minimal test harness for WinPatina
 *
 * No external dependencies. Each test file is a standalone executable
 * with its own main(). Include this header and use the macros below.
 *
 * Usage:
 *
 *   #include "test_harness.h"
 *
 *   TEST(my_test_name) {
 *       ASSERT_EQ(1 + 1, 2);
 *       ASSERT_TRUE(some_condition);
 *   }
 *
 *   TEST_MAIN()
 */

#ifndef WINPATINA_TEST_HARNESS_H
#define WINPATINA_TEST_HARNESS_H

#include <stdio.h>
#include <string.h>

/*============================================================================
 * Test Registration
 *============================================================================*/

#define WP_MAX_TESTS 256

typedef void (*wp_test_fn)(int* pass, int* fail);

static struct {
    const char* name;
    wp_test_fn fn;
} wp_tests[WP_MAX_TESTS];

static int wp_test_count = 0;

static void wp_register_test(const char* name, wp_test_fn fn)
{
    if (wp_test_count < WP_MAX_TESTS) {
        wp_tests[wp_test_count].name = name;
        wp_tests[wp_test_count].fn = fn;
        wp_test_count++;
    }
}

/*============================================================================
 * Test Definition Macro
 *============================================================================*/

/**
 * Define a test case. The body follows in braces.
 *
 * Each test receives pointers to pass/fail counters that the assertion
 * macros update. This avoids setjmp/longjmp complexity.
 */
#define TEST(name)                                                          \
    static void wp_test_##name(int* wp_pass_count_, int* wp_fail_count_);  \
    static struct wp_test_reg_##name {                                      \
        wp_test_reg_##name() {                                              \
            wp_register_test(#name, wp_test_##name);                        \
        }                                                                   \
    } wp_test_reg_instance_##name;                                          \
    static void wp_test_##name(int* wp_pass_count_, int* wp_fail_count_)

/*============================================================================
 * Assertion Macros
 *============================================================================*/

#define ASSERT_TRUE(expr)                                                   \
    do {                                                                    \
        if (expr) {                                                         \
            (*wp_pass_count_)++;                                            \
        } else {                                                            \
            (*wp_fail_count_)++;                                            \
            printf("  FAIL: %s:%d: ASSERT_TRUE(%s)\n",                     \
                   __FILE__, __LINE__, #expr);                              \
        }                                                                   \
    } while (0)

#define ASSERT_FALSE(expr)                                                  \
    do {                                                                    \
        if (!(expr)) {                                                      \
            (*wp_pass_count_)++;                                            \
        } else {                                                            \
            (*wp_fail_count_)++;                                            \
            printf("  FAIL: %s:%d: ASSERT_FALSE(%s)\n",                    \
                   __FILE__, __LINE__, #expr);                              \
        }                                                                   \
    } while (0)

#define ASSERT_EQ(a, b)                                                     \
    do {                                                                    \
        auto wp_a_ = (a);                                                   \
        auto wp_b_ = (b);                                                   \
        if (wp_a_ == wp_b_) {                                               \
            (*wp_pass_count_)++;                                            \
        } else {                                                            \
            (*wp_fail_count_)++;                                            \
            printf("  FAIL: %s:%d: ASSERT_EQ(%s, %s)\n",                   \
                   __FILE__, __LINE__, #a, #b);                             \
        }                                                                   \
    } while (0)

#define ASSERT_NE(a, b)                                                     \
    do {                                                                    \
        auto wp_a_ = (a);                                                   \
        auto wp_b_ = (b);                                                   \
        if (wp_a_ != wp_b_) {                                               \
            (*wp_pass_count_)++;                                            \
        } else {                                                            \
            (*wp_fail_count_)++;                                            \
            printf("  FAIL: %s:%d: ASSERT_NE(%s, %s)\n",                   \
                   __FILE__, __LINE__, #a, #b);                             \
        }                                                                   \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                 \
    do {                                                                    \
        const char* wp_a_ = (a);                                            \
        const char* wp_b_ = (b);                                            \
        if (wp_a_ != NULL && wp_b_ != NULL && strcmp(wp_a_, wp_b_) == 0) {  \
            (*wp_pass_count_)++;                                            \
        } else {                                                            \
            (*wp_fail_count_)++;                                            \
            printf("  FAIL: %s:%d: ASSERT_STR_EQ(%s, %s)\n"                \
                   "    got:      \"%s\"\n"                                 \
                   "    expected: \"%s\"\n",                                 \
                   __FILE__, __LINE__, #a, #b,                              \
                   wp_a_ ? wp_a_ : "(null)",                                \
                   wp_b_ ? wp_b_ : "(null)");                               \
        }                                                                   \
    } while (0)

/*============================================================================
 * Test Runner
 *============================================================================*/

/**
 * Generates a main() that runs all registered tests.
 * Returns 0 if all pass, 1 if any fail.
 */
#define TEST_MAIN()                                                         \
    int main(void)                                                          \
    {                                                                       \
        int total_pass = 0, total_fail = 0;                                 \
        int tests_passed = 0, tests_failed = 0;                             \
                                                                            \
        for (int i = 0; i < wp_test_count; i++) {                           \
            int pass = 0, fail = 0;                                         \
            printf("[TEST] %s\n", wp_tests[i].name);                        \
            wp_tests[i].fn(&pass, &fail);                                   \
                                                                            \
            total_pass += pass;                                             \
            total_fail += fail;                                             \
                                                                            \
            if (fail == 0) {                                                \
                printf("  OK (%d assertions)\n", pass);                     \
                tests_passed++;                                             \
            } else {                                                        \
                printf("  FAILED (%d passed, %d failed)\n", pass, fail);    \
                tests_failed++;                                             \
            }                                                               \
        }                                                                   \
                                                                            \
        printf("\n========================================\n");              \
        printf("Tests:      %d passed, %d failed, %d total\n",             \
               tests_passed, tests_failed, wp_test_count);                  \
        printf("Assertions: %d passed, %d failed, %d total\n",             \
               total_pass, total_fail, total_pass + total_fail);            \
        printf("========================================\n");               \
                                                                            \
        return (total_fail > 0) ? 1 : 0;                                   \
    }

#endif /* WINPATINA_TEST_HARNESS_H */
