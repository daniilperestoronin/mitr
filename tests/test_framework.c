/**
 * test_framework.c - Shared test harness state
 */

#include "test_framework.h"

TestStats g_test_stats = {0, 0, 0, nullptr};
bool g_test_current_failed = false;
