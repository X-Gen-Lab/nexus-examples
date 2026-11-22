/**
 * \file            network_delivery_test.cpp
 * \brief           Durable delivery and bounded remote command contracts
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "boot_audit_reference.h"
#include "delivery_reference.h"
#include "nexus/components/persistent_record.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

namespace {
const nx_flashdb_operation_t* operation = nullptr;

class Delivery : public testing::Test {
  protected:
    std::vector<uint8_t> medium = std::vector<uint8_t>(8192U, 0xffU);
    nx_flash_erase_span_t span{0U, 4096U, 2U};
    nx_flash_geometry_t geometry{0U, 8192U, 4U, &span, 1U, 0U};
    nx_flash_port_t port{};
    nx_fal_t fal{};
    nx_flashdb_t database{};
    std::vector<max_align_t> workspace;
    example_outbox_t outbox{};
    nx_flashdb_operation_t scope{&database, NX_DEADLINE_NEVER - 1U};
    bool fail_program{};
    unsigned programs{};
    static nx_time_us_t clock(void*) {
        return nx_time_now_us();
    }
    static const nx_flash_geometry_t* describe(const void* p) {
        return &static_cast<const Delivery*>(p)->geometry;
    }
    static nx_result_t read(const void* p, uint32_t offset, void* out,
                            size_t bytes) {
        const auto& self = *static_cast<const Delivery*>(p);
        std::memcpy(out, self.medium.data() + offset, bytes);
        return NX_SUCCESS;
    }
    static nx_result_t program(void* p, uint32_t offset, const void* in,
                               size_t bytes, nx_time_us_t) {
        auto& self = *static_cast<Delivery*>(p);
        ++self.programs;
        if (self.fail_program) {
            return NX_ERROR_IO;
        }
        const auto* source = static_cast<const uint8_t*>(in);
        for (size_t i = 0; i < bytes; ++i) {
            if ((self.medium[offset + i] & source[i]) != source[i]) {
                return NX_ERROR_IO;
            }
            self.medium[offset + i] &= source[i];
        }
        return NX_SUCCESS;
    }
    static nx_result_t erase(void* p, uint32_t offset, size_t bytes,
                             nx_time_us_t) {
        auto& self = *static_cast<Delivery*>(p);
        std::fill_n(self.medium.begin() + offset, bytes, 0xffU);
        return NX_SUCCESS;
    }
    static nx_result_t recover(void*) {
        return NX_SUCCESS;
    }
    static const nx_flash_ops_t flash_ops;
    void SetUp() override {
        port = {&flash_ops, this};
        ASSERT_EQ(nx_fal_bind(&fal, {&port, 0U, 8192U, true}, {clock, this}),
                  NX_SUCCESS);
        workspace.resize(
            (nx_flashdb_workspace_bytes() + sizeof(max_align_t) - 1U) /
            sizeof(max_align_t));
        ASSERT_EQ(nx_flashdb_init(&database, &fal, NX_FLASHDB_KV,
                                  workspace.data(),
                                  workspace.size() * sizeof(max_align_t), 0U),
                  NX_SUCCESS);
        operation = &scope;
        ASSERT_EQ(nx_flashdb_open(&scope, NX_FLASHDB_CREATE_IF_ERASED),
                  NX_SUCCESS);
        ASSERT_EQ(example_outbox_load(&outbox, &scope), NX_SUCCESS);
    }
    void TearDown() override {
        EXPECT_EQ(nx_flashdb_close(&database), NX_SUCCESS);
        operation = nullptr;
    }
    void reopen() {
        ASSERT_EQ(nx_flashdb_close(&database), NX_SUCCESS);
        ASSERT_EQ(nx_flashdb_open(&scope, NX_FLASHDB_EXISTING_ONLY),
                  NX_SUCCESS);
        outbox = {};
        ASSERT_EQ(example_outbox_load(&outbox, &scope), NX_SUCCESS);
    }
};
const nx_flash_ops_t Delivery::flash_ops{describe, read, program, erase,
                                         recover};

TEST_F(Delivery, PendingRecordSurvivesReopenAndRejectsOverwrite) {
    const uint8_t payload[]{10U, 20U, 30U};
    ASSERT_EQ(
        example_outbox_enqueue(&outbox, &scope, 41U, payload, sizeof(payload)),
        NX_SUCCESS);
    const unsigned before = programs;
    EXPECT_EQ(
        example_outbox_enqueue(&outbox, &scope, 42U, payload, sizeof(payload)),
        NX_ERROR_BUSY);
    EXPECT_EQ(programs, before);
    reopen();
    uint64_t identifier{};
    std::array<uint8_t, 96> output{};
    size_t bytes = output.size();
    ASSERT_EQ(example_outbox_read(&outbox, &identifier, output.data(), &bytes),
              NX_SUCCESS);
    EXPECT_EQ(identifier, 41U);
    EXPECT_EQ(bytes, sizeof(payload));
    EXPECT_EQ(std::memcmp(output.data(), payload, bytes), 0);
}

TEST_F(Delivery, AckIsDurableAndItsIdentifierCannotBeReusedAfterReboot) {
    const uint8_t payload[]{1U};
    ASSERT_EQ(example_outbox_enqueue(&outbox, &scope, 8U, payload, 1U),
              NX_SUCCESS);
    EXPECT_EQ(example_outbox_acknowledge(&outbox, &scope, 7U), NX_ERROR_STATE);
    ASSERT_EQ(example_outbox_acknowledge(&outbox, &scope, 8U), NX_SUCCESS);
    const unsigned before = programs;
    EXPECT_EQ(example_outbox_acknowledge(&outbox, &scope, 8U), NX_SUCCESS);
    EXPECT_EQ(programs, before);
    reopen();
    uint64_t identifier{};
    uint8_t output{};
    size_t bytes = 1U;
    EXPECT_EQ(example_outbox_read(&outbox, &identifier, &output, &bytes),
              NX_ERROR_EMPTY);
    EXPECT_EQ(example_outbox_enqueue(&outbox, &scope, 8U, payload, 1U),
              NX_ERROR_STATE);
    EXPECT_EQ(example_outbox_enqueue(&outbox, &scope, 9U, payload, 1U),
              NX_SUCCESS);
}

TEST_F(Delivery, UncertainAckClosesAdmissionUntilActualRecoveryAndReload) {
    const uint8_t payload[]{1U};
    ASSERT_EQ(example_outbox_enqueue(&outbox, &scope, 2U, payload, 1U),
              NX_SUCCESS);
    fail_program = true;
    EXPECT_EQ(example_outbox_acknowledge(&outbox, &scope, 2U), NX_ERROR_IO);
    EXPECT_TRUE(outbox.faulted);
    EXPECT_EQ(example_outbox_acknowledge(&outbox, &scope, 2U), NX_ERROR_STATE);
    fail_program = false;
    ASSERT_EQ(nx_fal_recover(&fal), NX_SUCCESS);
    reopen();
    uint64_t identifier{};
    uint8_t output{};
    size_t bytes = 1U;
    ASSERT_EQ(example_outbox_read(&outbox, &identifier, &output, &bytes),
              NX_SUCCESS);
    EXPECT_EQ(identifier, 2U);
}

TEST_F(Delivery, CorruptRecordIsNeverTreatedAsEmptyOrOverwritten) {
    const uint8_t corrupt[]{1U, 2U, 3U};
    ASSERT_EQ(nx_flashdb_kv_set(&scope, "telemetry", corrupt, sizeof(corrupt)),
              NX_SUCCESS);
    outbox = {};
    const unsigned before = programs;
    EXPECT_NE(example_outbox_load(&outbox, &scope), NX_SUCCESS);
    const uint8_t payload[]{1U};
    EXPECT_EQ(example_outbox_enqueue(&outbox, &scope, 1U, payload, 1U),
              NX_ERROR_STATE);
    EXPECT_EQ(programs, before);
}

TEST_F(Delivery, ShortOutputAndAliasingDoNotLosePendingRecord) {
    const uint8_t payload[]{1U, 2U};
    ASSERT_EQ(example_outbox_enqueue(&outbox, &scope, 1U, payload, 2U),
              NX_SUCCESS);
    uint64_t identifier{99U};
    uint8_t output{42U};
    size_t bytes = 1U;
    EXPECT_EQ(example_outbox_read(&outbox, &identifier, &output, &bytes),
              NX_ERROR_EXHAUSTED);
    EXPECT_EQ(bytes, 2U);
    EXPECT_EQ(identifier, 99U);
    EXPECT_EQ(output, 42U);
    bytes = 2U;
    EXPECT_EQ(example_outbox_read(&outbox, &identifier, &outbox, &bytes),
              NX_ERROR_INVALID);
}

struct BootImage {
    uint64_t identifier{9U};
    nx_result_t authentication{NX_SUCCESS};
    unsigned inspections{};
    nx_flash_erase_span_t span{0U, 64U, 1U};
    nx_flash_geometry_t geometry{0U, 64U, 4U, &span, 1U, 0U};
    static const nx_flash_geometry_t* describe(const void* context) {
        return &static_cast<const BootImage*>(context)->geometry;
    }
    static nx_result_t read(const void* context, uint32_t offset, void* output,
                            size_t bytes) {
        const auto& self = *static_cast<const BootImage*>(context);
        if (offset != 0U || bytes != sizeof(self.identifier)) {
            return NX_ERROR_INVALID;
        }
        std::memcpy(output, &self.identifier, bytes);
        return NX_SUCCESS;
    }
    static nx_result_t authenticate(void* context,
                                    const nx_flash_region_t* running,
                                    uint32_t bytes, nx_time_us_t deadline,
                                    uint64_t* identifier) {
        auto& self = *static_cast<BootImage*>(context);
        ++self.inspections;
        EXPECT_EQ(bytes, 64U);
        EXPECT_EQ(deadline, NX_DEADLINE_NEVER - 1U);
        if (self.authentication != NX_SUCCESS) {
            return self.authentication;
        }
        return nx_flash_region_read(running, 0U, identifier,
                                    sizeof(*identifier));
    }
    const nx_flash_ops_t ops{describe, read, nullptr, nullptr, nullptr};
    nx_flash_port_t port{&ops, this};
    example_boot_audit_ports_t ports{
        {&port, 0U, 64U, false}, 64U, authenticate, this};
};

TEST_F(Delivery, BootOutcomeRequiresActualImageInspectionAndSurvivesReopen) {
    BootImage image;
    example_boot_audit_report_t report{};
    EXPECT_EQ(example_update_audit_boot(&scope, &image.ports, &report),
              NX_ERROR_EMPTY);
    EXPECT_EQ(image.inspections, 0U);
    ASSERT_EQ(example_update_track(&scope, 9U), NX_SUCCESS);
    ASSERT_EQ(example_update_audit_boot(&scope, &image.ports, &report),
              NX_SUCCESS);
    EXPECT_EQ(report.expected, 9U);
    EXPECT_EQ(report.observed, 9U);
    EXPECT_TRUE(report.matches);
    EXPECT_TRUE(report.persisted);
    reopen();
    report = {};
    ASSERT_EQ(example_update_boot_report(&scope, &report), NX_SUCCESS);
    EXPECT_TRUE(report.matches);
    EXPECT_EQ(report.observed, 9U);
}

TEST_F(Delivery, OtherImageAndAuthenticationFailureAreRecordedAsOutcomes) {
    BootImage image;
    example_boot_audit_report_t report{};
    ASSERT_EQ(example_update_track(&scope, 8U), NX_SUCCESS);
    ASSERT_EQ(example_update_audit_boot(&scope, &image.ports, &report),
              NX_SUCCESS);
    EXPECT_FALSE(report.matches);
    EXPECT_EQ(report.observed, 9U);
    image.authentication = NX_ERROR_PERMISSION;
    ASSERT_EQ(example_update_audit_boot(&scope, &image.ports, &report),
              NX_SUCCESS);
    EXPECT_FALSE(report.matches);
    EXPECT_EQ(report.observed, 0U);
    EXPECT_EQ(report.authentication, NX_ERROR_PERMISSION);
}

TEST_F(Delivery, FailedBootReportPersistenceNeverClaimsDurableResult) {
    BootImage image;
    example_boot_audit_report_t report{};
    ASSERT_EQ(example_update_track(&scope, 9U), NX_SUCCESS);
    fail_program = true;
    EXPECT_EQ(example_update_audit_boot(&scope, &image.ports, &report),
              NX_ERROR_IO);
    EXPECT_TRUE(report.matches);
    EXPECT_FALSE(report.persisted);
}

TEST(RemoteCommand, InvalidOrStaleCommandsNeverAdvanceTheAcceptedSequence) {
    example_command_gate_t gate{};
    ASSERT_EQ(example_command_gate_init(&gate, 7U, 32U), NX_SUCCESS);
    uint8_t payload[]{1U, 2U};
    example_command_t command{7U, 1U, 100U, payload, sizeof(payload)};
    EXPECT_EQ(example_command_admit(&gate, &command, 100U), NX_ERROR_TIMEOUT);
    EXPECT_EQ(gate.last_sequence, 0U);
    EXPECT_EQ(example_command_admit(&gate, &command, 50U), NX_SUCCESS);
    EXPECT_EQ(example_command_admit(&gate, &command, 51U), NX_ERROR_STATE);
    command.epoch = 6U;
    command.sequence = 2U;
    EXPECT_EQ(example_command_admit(&gate, &command, 51U), NX_ERROR_STATE);
    EXPECT_EQ(gate.last_sequence, 1U);
    command.epoch = 7U;
    command.bytes = 33U;
    EXPECT_EQ(example_command_admit(&gate, &command, 51U), NX_ERROR_INVALID);
    EXPECT_EQ(gate.last_sequence, 1U);
}

TEST(RemoteCommand, ClockRegressionAndWrappingInputsRejectBeforeAdmission) {
    example_command_gate_t gate{};
    ASSERT_EQ(example_command_gate_init(&gate, 2U, 8U), NX_SUCCESS);
    const uint8_t payload[]{5U};
    example_command_t command{2U, 1U, 100U, payload, 1U};
    ASSERT_EQ(example_command_admit(&gate, &command, 50U), NX_SUCCESS);
    command.sequence = 2U;
    EXPECT_EQ(example_command_admit(&gate, &command, 49U), NX_ERROR_STATE);
    command.payload = reinterpret_cast<const uint8_t*>(UINTPTR_MAX);
    EXPECT_EQ(example_command_admit(&gate, &command, 51U), NX_ERROR_INVALID);
}
} /* namespace */

extern "C" const nx_flashdb_operation_t* nx_flashdb_operation_get(void) {
    return operation;
}
