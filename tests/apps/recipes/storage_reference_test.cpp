/**
 * \file            storage_reference_test.cpp
 * \brief           External consumer proves distinct persistence contracts
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "database_reference.h"
#include "nexus/components/flash_storage.h"
#include "nexus/components/persistent_record.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>
namespace {
/** \brief External physical-media model only; no database algorithm is copied.
 */
class Medium {
  public:
    std::vector<uint8_t> bytes = std::vector<uint8_t>(24576U, 0xffU);
    nx_flash_erase_span_t span{0U, 4096U, 6U};
    nx_flash_geometry_t geometry{0U, 24576U, 4U, &span, 1U, 0U};
    unsigned snapshot_programs{0U};
    bool fail_program{false};
    static nx_time_us_t now(void*) {
        return nx_time_now_us();
    }
    static const nx_flash_geometry_t* describe(const void* context) {
        return &static_cast<const Medium*>(context)->geometry;
    }
    static nx_result_t read(const void* context, uint32_t offset, void* output,
                            size_t count) {
        const auto& self = *static_cast<const Medium*>(context);
        std::memcpy(output, self.bytes.data() + offset, count);
        return NX_SUCCESS;
    }
    static nx_result_t program(void* context, uint32_t offset,
                               const void* input, size_t count, nx_time_us_t) {
        auto& self = *static_cast<Medium*>(context);
        if (offset >= 16384U) {
            ++self.snapshot_programs;
        }
        if (self.fail_program) {
            return NX_ERROR_IO;
        }
        const auto* value = static_cast<const uint8_t*>(input);
        for (size_t index = 0U; index < count; ++index) {
            if ((self.bytes[offset + index] & value[index]) != value[index]) {
                return NX_ERROR_IO;
            }
            self.bytes[offset + index] &= value[index];
        }
        return NX_SUCCESS;
    }
    static nx_result_t erase(void* context, uint32_t offset, size_t count,
                             nx_time_us_t) {
        auto& self = *static_cast<Medium*>(context);
        std::fill_n(self.bytes.begin() + offset, count, 0xffU);
        return NX_SUCCESS;
    }
    static nx_result_t recover(void*) {
        return NX_SUCCESS;
    }
    const nx_flash_ops_t ops{describe, read, program, erase, recover};
    nx_flash_port_t port{&ops, this};
};
class StorageRecipe : public testing::Test {
  protected:
    static constexpr nx_time_us_t deadline = NX_DEADLINE_NEVER - 1U;
    Medium media;
    nx_fal_t kv_flash{};
    nx_fal_t ts_flash{};
    nx_flashdb_t kv{};
    nx_flashdb_t ts{};
    std::vector<max_align_t> kv_workspace;
    std::vector<max_align_t> ts_workspace;
    nx_flash_storage_adapter_t snapshot_adapter{};
    nx_storage_port_t snapshot_port{};
    nx_storage_t snapshot{};
    nx_settings_t settings{};
    std::array<uint8_t, 64> storage_workspace{};
    std::array<uint8_t, 512> settings_image{};
    std::array<uint8_t, 128> record{};
    example_storage_plan_t plan{&kv,           &ts,           &settings,
                                record.data(), record.size(), 7U};
    example_storage_report_t report{};
    void SetUp() override {
        const nx_clock_t clock{Medium::now, &media};
        ASSERT_EQ(nx_fal_bind(&kv_flash, {&media.port, 0U, 8192U, true}, clock),
                  NX_SUCCESS);
        ASSERT_EQ(
            nx_fal_bind(&ts_flash, {&media.port, 8192U, 8192U, true}, clock),
            NX_SUCCESS);
        const size_t units =
            (nx_flashdb_workspace_bytes() + sizeof(max_align_t) - 1U) /
            sizeof(max_align_t);
        kv_workspace.resize(units);
        ts_workspace.resize(units);
        ASSERT_EQ(nx_flashdb_init(&kv, &kv_flash, NX_FLASHDB_KV,
                                  kv_workspace.data(),
                                  units * sizeof(max_align_t), 0U),
                  NX_SUCCESS);
        ASSERT_EQ(nx_flashdb_init(&ts, &ts_flash, NX_FLASHDB_TS,
                                  ts_workspace.data(),
                                  units * sizeof(max_align_t), record.size()),
                  NX_SUCCESS);
        ASSERT_EQ(nx_flash_storage_bind(&snapshot_adapter,
                                        {&media.port, 16384U, 8192U, true},
                                        deadline, &snapshot_port),
                  NX_SUCCESS);
        ASSERT_EQ(nx_storage_open(&snapshot, &snapshot_port, 0U, 4096U,
                                  storage_workspace.data(),
                                  storage_workspace.size()),
                  NX_STORAGE_OK);
        ASSERT_EQ(nx_settings_open(&settings, &snapshot, 7U,
                                   settings_image.data(), settings_image.size(),
                                   4U),
                  NX_STORAGE_OK);
    }
    void TearDown() override {
        EXPECT_EQ(nx_flashdb_close(&kv), NX_SUCCESS);
        EXPECT_EQ(nx_flashdb_close(&ts), NX_SUCCESS);
        nx_settings_close(&settings);
    }
};
TEST_F(StorageRecipe, UncreatedDatabasesRequireExplicitPermission) {
    EXPECT_EQ(example_storage_publish(&plan, false, false, 20U, 100, 1U,
                                      deadline, &report),
              NX_ERROR_EMPTY);
    EXPECT_FALSE(report.kv_opened);
    EXPECT_FALSE(report.staged);
    EXPECT_TRUE(std::all_of(media.bytes.begin(), media.bytes.end(),
                            [](uint8_t byte) { return byte == 0xffU; }));
}
TEST_F(StorageRecipe, ImmediateDatabasesDoNotImplicitlyCommitSettings) {
    ASSERT_EQ(example_storage_publish(&plan, true, false, 20U, 100, 1U,
                                      deadline, &report),
              NX_SUCCESS);
    EXPECT_TRUE(report.kv_saved);
    EXPECT_TRUE(report.record_appended);
    EXPECT_TRUE(settings.dirty);
    EXPECT_FALSE(report.snapshot_committed);
    EXPECT_EQ(media.snapshot_programs, 0U);
    nx_persistent_record_view_t view{};
    ASSERT_EQ(nx_persistent_record_decode(7U, record.data(), 36U, &view),
              NX_SUCCESS);
    EXPECT_EQ(view.identifier, 1U);
    ASSERT_EQ(nx_flashdb_close(&kv), NX_SUCCESS);
    ASSERT_EQ(nx_flashdb_close(&ts), NX_SUCCESS);
    ASSERT_EQ(example_storage_publish(&plan, false, true, 30U, 101, 2U,
                                      deadline, &report),
              NX_SUCCESS);
    EXPECT_TRUE(report.snapshot_committed);
    EXPECT_FALSE(settings.dirty);
    EXPECT_GT(media.snapshot_programs, 0U);
}
TEST_F(StorageRecipe, FailedMutationStopsWithoutFormattingOrAutomaticReplay) {
    ASSERT_EQ(example_storage_publish(&plan, true, false, 20U, 100, 1U,
                                      deadline, &report),
              NX_SUCCESS);
    const auto before = media.bytes;
    media.fail_program = true;
    EXPECT_EQ(example_storage_publish(&plan, false, false, 30U, 101, 2U,
                                      deadline, &report),
              NX_ERROR_IO);
    EXPECT_FALSE(report.kv_saved);
    EXPECT_FALSE(report.record_appended);
    EXPECT_FALSE(kv.opened);
    EXPECT_EQ(media.bytes, before);
    EXPECT_TRUE(kv_flash.faulted);
    EXPECT_EQ(nx_fal_recover(&kv_flash), NX_SUCCESS);
}
} /* namespace */
