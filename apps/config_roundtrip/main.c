/** Finite, nonencrypted RAM configuration and snapshot roundtrip. */
#include "nexus_config.h"
#include "config/config.h"
#include "config/config_ram_backend.h"
#include "runtime/nx_runtime.h"
#include "nexus_board.h"
#include "osal/osal.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#if defined(NX_CONFIG_PLATFORM_NATIVE)
#include "heap_bounds.h"
#else
#include "newlib_sbrk.h"
#endif


/* Management context only, one owner. These bounded example buffers are not
 * a Flash backend or a durable snapshot. Binary v2 retains namespace IDs and
 * needs the same namespace map during import; it is not a portable backup. */
/* The public size query reserves worst-case JSON escapes, not just the
 * compact ASCII output length; retain a fixed budget large enough for it. */
static char json[2048];
static uint8_t binary[2048];
static config_ns_handle_t motor;
static config_ns_handle_t network;
static const uint8_t calibration[] = {0x00, 0x17, 0x80, 0xff};
/* Debugger-visible results on MCU. -1 is not run, 0 passed, 1 failed. A
 * FreeRTOS task may finish while the scheduler continues owning the firmware. */
volatile int config_example_result = -1;
volatile unsigned int config_example_checks;
const char* volatile config_example_failure;

static bool output(const char* format, ...) {
#if defined(NX_CONFIG_PLATFORM_NATIVE)
    va_list args;
    va_start(args, format);
    int written = vprintf(format, args);
    va_end(args);
    return written >= 0;
#else
    /* No newlib _write retarget or UART console is implied. MCU verification
     * uses the result/check/failure symbols above; no text is transmitted. */
    (void)format;
    return true;
#endif
}

static bool expect(config_status_t actual, config_status_t expected,
                   const char* operation) {
    if (actual == expected) {
        ++config_example_checks;
        return true;
    }
    config_example_failure = operation;
#if defined(NX_CONFIG_PLATFORM_NATIVE)
    (void)fprintf(stderr, "%s: expected %s, got %s\n", operation,
                  config_error_to_str(expected), config_error_to_str(actual));
#endif
    return false;
}

static bool verify(bool condition, const char* detail) {
    if (condition) {
        ++config_example_checks;
        return true;
    }
    config_example_failure = detail;
#if defined(NX_CONFIG_PLATFORM_NATIVE)
    (void)fprintf(stderr, "Verification failed: %s\n", detail);
#endif
    return false;
}

#define EXPECT(call, status)                                                    \
    do {                                                                       \
        if (!expect((call), (status), #call)) return false;                     \
    } while (0)
#define CHECK(call) EXPECT(call, CONFIG_OK)
#define VERIFY(condition, detail)                                              \
    do {                                                                       \
        if (!verify((condition), (detail))) return false;                       \
    } while (0)

static bool typed_readback(void) {
    int32_t timeout = 0;
    uint32_t retry = 0;
    int64_t counter = 0;
    float threshold = 0;
    bool enabled = false;
    char name[32] = {0};
    uint8_t blob[sizeof(calibration)] = {0};
    size_t size = 0;
    CHECK(config_get_i32("app.timeout", &timeout, 0));
    CHECK(config_get_u32("app.retry", &retry, 0));
    CHECK(config_get_i64("sensor.counter", &counter, 0));
    CHECK(config_get_float("sensor.threshold", &threshold, 0));
    CHECK(config_get_bool("feature.enabled", &enabled, false));
    CHECK(config_get_str("device.name", name, sizeof(name)));
    CHECK(config_get_blob("sensor.calibration", blob, sizeof(blob), &size));
    VERIFY(timeout == 5000 && retry == 3 && counter == INT64_C(4294967301) &&
               threshold == 25.5f && enabled &&
               strcmp(name, "Nexus-Demo") == 0 && size == sizeof(calibration) &&
               memcmp(blob, calibration, size) == 0,
           "all seven typed values survive readback");
    return true;
}

static bool basic_and_default_json(void) {
    CHECK(config_set_i32("app.timeout", 5000));
    CHECK(config_set_u32("app.retry", 3));
    CHECK(config_set_i64("sensor.counter", INT64_C(4294967301)));
    CHECK(config_set_float("sensor.threshold", 25.5f));
    CHECK(config_set_bool("feature.enabled", true));
    CHECK(config_set_str("device.name", "Nexus-Demo"));
    CHECK(config_set_blob("sensor.calibration", calibration,
                          sizeof(calibration)));
    VERIFY(typed_readback(), "initial typed readback");

    int32_t fallback = 0;
    CHECK(config_get_i32("missing", &fallback, 42));
    VERIFY(fallback == 42, "missing numeric key uses caller default");
    EXPECT(config_get_i32("device.name", &fallback, 0),
           CONFIG_ERROR_TYPE_MISMATCH);
    EXPECT(config_set_i32(NULL, 1), CONFIG_ERROR_INVALID_PARAM);
    char missing[16] = {0};
    EXPECT(config_get_str("missing", missing, sizeof(missing)),
           CONFIG_ERROR_NOT_FOUND);

    size_t required = 0;
    size_t actual = 0;
    CHECK(config_get_export_size(CONFIG_FORMAT_JSON, CONFIG_EXPORT_FLAG_NONE,
                                 &required));
    VERIFY(output("Default JSON required capacity: %zu; fixed budget: %zu bytes\n",
                  required, sizeof(json)), "write JSON capacity information");
    VERIFY(required <= sizeof(json), "default JSON fits example budget");
    char tiny[2] = {0};
    EXPECT(config_export(CONFIG_FORMAT_JSON, CONFIG_EXPORT_FLAG_NONE, tiny,
                         sizeof(tiny), &actual), CONFIG_ERROR_BUFFER_TOO_SMALL);
    VERIFY(actual == required, "small export reports required capacity");
    CHECK(config_export(CONFIG_FORMAT_JSON, CONFIG_EXPORT_FLAG_NONE, json,
                        sizeof(json), &actual));
    VERIFY(actual < sizeof(json) && json[actual] == '\0',
           "JSON output is bounded and terminated");
    VERIFY(output("Default namespace JSON (%zu bytes): %s\n", actual, json),
           "write JSON to configured stdout");
    CHECK(config_set_i32("app.timeout", 1));
    CHECK(config_import(CONFIG_FORMAT_JSON, CONFIG_IMPORT_FLAG_CLEAR, json,
                        actual));
    VERIFY(typed_readback(), "default JSON roundtrip");
    return true;
}

static bool namespace_readback(void) {
    int32_t motor_limit = 0;
    int32_t network_limit = 0;
    char motor_mode[16] = {0};
    char network_mode[16] = {0};
    bool exists = true;
    CHECK(config_ns_get_i32(motor, "limit", &motor_limit, 0));
    CHECK(config_ns_get_i32(network, "limit", &network_limit, 0));
    CHECK(config_ns_get_str(motor, "mode", motor_mode, sizeof(motor_mode)));
    CHECK(config_ns_get_str(network, "mode", network_mode, sizeof(network_mode)));
    CHECK(config_exists("limit", &exists));
    VERIFY(motor_limit == 1000 && network_limit == 2000 && !exists &&
               strcmp(motor_mode, "control") == 0 &&
               strcmp(network_mode, "uplink") == 0,
           "identical namespace keys remain isolated from each other/default");
    return true;
}

static bool namespaced_roundtrips(void) {
    CHECK(config_open_namespace("motor", &motor));
    CHECK(config_open_namespace("network", &network));
    CHECK(config_ns_set_i32(motor, "limit", 1000));
    CHECK(config_ns_set_str(motor, "mode", "control"));
    CHECK(config_ns_set_i32(network, "limit", 2000));
    CHECK(config_ns_set_str(network, "mode", "uplink"));
    VERIFY(namespace_readback(), "initial namespace isolation");

    size_t required = 0;
    size_t actual = 0;
    /* Global JSON deliberately cannot flatten nondefault namespaces. */
    EXPECT(config_get_export_size(CONFIG_FORMAT_JSON, CONFIG_EXPORT_FLAG_NONE,
                                  &required), CONFIG_ERROR_UNSUPPORTED);
    EXPECT(config_export(CONFIG_FORMAT_JSON, CONFIG_EXPORT_FLAG_NONE, json,
                         sizeof(json), &actual), CONFIG_ERROR_UNSUPPORTED);
    CHECK(config_export_namespace("motor", CONFIG_FORMAT_JSON,
                                  CONFIG_EXPORT_FLAG_NONE, json, sizeof(json),
                                  &actual));
    VERIFY(actual < sizeof(json) && json[actual] == '\0',
           "namespace JSON is bounded and terminated");
    VERIFY(output("Motor namespace JSON: %s\n", json), "write namespace JSON");
    CHECK(config_ns_set_i32(motor, "limit", 1));
    CHECK(config_import_namespace("motor", CONFIG_FORMAT_JSON,
                                  CONFIG_IMPORT_FLAG_CLEAR, json, actual));
    VERIFY(namespace_readback(), "namespace JSON restores motor only");

    CHECK(config_get_export_size(CONFIG_FORMAT_BINARY, CONFIG_EXPORT_FLAG_NONE,
                                 &required));
    VERIFY(required <= sizeof(binary), "binary snapshot fits example budget");
    CHECK(config_export(CONFIG_FORMAT_BINARY, CONFIG_EXPORT_FLAG_NONE, binary,
                        sizeof(binary), &actual));
    VERIFY(actual == required && actual > 16 && binary[4] == 2,
           "exported binary uses the documented version 2 envelope");
    /* A truncated CLEAR import must reject the entire input and retain RAM. */
    EXPECT(config_import(CONFIG_FORMAT_BINARY, CONFIG_IMPORT_FLAG_CLEAR,
                         binary, actual - 1), CONFIG_ERROR_INVALID_FORMAT);
    VERIFY(typed_readback() && namespace_readback(),
           "failed import preserves all live values");
    CHECK(config_set_i32("app.timeout", 1));
    CHECK(config_ns_set_i32(motor, "limit", 1));
    CHECK(config_ns_set_i32(network, "limit", 1));
    CHECK(config_import(CONFIG_FORMAT_BINARY, CONFIG_IMPORT_FLAG_CLEAR, binary,
                        actual));
    VERIFY(typed_readback() && namespace_readback(),
           "binary v2 restores default and both existing namespaces");
    size_t count = 0;
    CHECK(config_get_count(&count));
    VERIFY(count == 11, "roundtrip restores all eleven entries");
    /* Explicit RAM backend exercises the same neutral snapshot contract.
     * Its stored snapshot is still volatile, not durable across reset. */
    CHECK(config_commit());
    CHECK(config_set_i32("app.timeout", 1));
    CHECK(config_ns_set_i32(motor, "limit", 1));
    CHECK(config_ns_set_i32(network, "limit", 1));
    /* Snapshot load replaces the namespace map. Close every borrowed namespace
     * handle before replacement; reopen by name after the new map is installed. */
    CHECK(config_close_namespace(motor));
    motor = NULL;
    CHECK(config_close_namespace(network));
    network = NULL;
    CHECK(config_load());
    CHECK(config_open_namespace("motor", &motor));
    CHECK(config_open_namespace("network", &network));
    VERIFY(typed_readback() && namespace_readback(),
           "RAM backend snapshot reload restores every namespace");
    CHECK(config_get_count(&count));
    VERIFY(count == 11, "RAM snapshot reload retains all eleven entries");
    return output("Verified %zu RAM entries; binary v2 roundtrip (%zu bytes).\n"
                  "RAM snapshot reload verified; reset loses all values.\n",
                  count, actual);
}

static int run_configuration_example(void) {
#if defined(NX_CONFIG_PLATFORM_NATIVE)
    if (!verify(example_heap_bounds_self_test() == 0,
                "bounded heap growth/shrink/OOM/overflow/canary self-test")) {
#else
    if (!verify(example_newlib_heap_bind(), "bind single-owner newlib heap")) {
#endif
        config_example_result = 1;
        return 1;
    }
    bool initialized = expect(config_init(NULL), CONFIG_OK, "config_init");
    bool passed = initialized &&
                  expect(config_set_backend(config_backend_ram_get()), CONFIG_OK,
                         "bind explicit RAM backend") &&
                  output("[%s] Volatile RAM configuration example\n", NX_BOARD_NAME) &&
                  basic_and_default_json() && namespaced_roundtrips();
    if (motor && !expect(config_close_namespace(motor), CONFIG_OK,
                         "close motor namespace")) passed = false;
    if (network && !expect(config_close_namespace(network), CONFIG_OK,
                           "close network namespace")) passed = false;
    if (initialized && !expect(config_deinit(), CONFIG_OK, "config_deinit"))
        passed = false;

    if (passed && !output("Configuration example completed.\n")) passed = false;
#if defined(NX_CONFIG_PLATFORM_NATIVE)
    if (fflush(stdout) != 0 || ferror(stdout)) passed = false;
#endif
    config_example_result = passed ? 0 : 1;
    return passed ? 0 : 1;
}

#if defined(NX_CONFIG_OSAL_FREERTOS)
static void configuration_task(void* unused) {
    (void)unused;
    (void)run_configuration_example();
    /* The example is finite, but the MCU scheduler owns the firmware lifetime.
     * Global MCU shutdown/reset belongs to the application; do not report a successful
     * deinit while the scheduler and this task's static TCB remain owned. */
}
#endif

int main(void) {
    if (nx_runtime_bootstrap(NULL) != NX_OK) {
        config_example_result = 1;
        config_example_failure = "runtime bootstrap";
        return 1;
    }
#if defined(NX_CONFIG_OSAL_FREERTOS)
    /* Kernel-backed configuration locking runs in a scheduled task. Bootstrap
     * only creates the task and immediately starts the scheduler. */
    osal_task_handle_t task = NULL;
    const osal_task_config_t config = {
        .name = "Configuration", .func = configuration_task,
        .priority = OSAL_TASK_PRIORITY_NORMAL, .stack_size = 2048U
    };
    if (osal_task_create(&config, &task) != OSAL_OK) {
        config_example_result = 1;
        config_example_failure = "create configuration task";
        (void)nx_runtime_shutdown(NULL);
        return 1;
    }
    osal_start();
    config_example_result = 1;
    config_example_failure = "scheduler returned";
    return 1; /* A stopped or failed scheduler is not a successful run. */
#else
    int result = run_configuration_example();
#if defined(NX_CONFIG_PLATFORM_NATIVE)
    if (nx_runtime_shutdown(NULL) != NX_OK) {
        result = config_example_result = 1;
        config_example_failure = "runtime shutdown";
    }
#else
    /* Baremetal has no running kernel, but chip-wide deinit is deliberately
     * unsupported. The board/clock remains owned until a controlled reset. */
#endif
    return result;
#endif
}
