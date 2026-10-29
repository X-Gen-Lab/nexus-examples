/**
 * \file            flash_maintenance_test.cpp
 * \brief           Real public Flash regions enforce application pulse order
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-30
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "flash_application.h"
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {
class FlashHardware {
  public:
    MOCK_METHOD(nx_result_t, Erase, (uint32_t, size_t, nx_time_us_t));
    MOCK_METHOD(nx_result_t, Program,
                (uint32_t, const void*, size_t, nx_time_us_t));
    MOCK_METHOD(nx_result_t, Read, (uint32_t, void*, size_t));
};
class FlashMaintenance : public testing::Test {
  protected:
    testing::StrictMock<FlashHardware> hardware;
    static constexpr nx_flash_sector_t sectors[] = {{0U, 131072U},
                                                    {131072U, 131072U}};
    nx_flash_geometry_t geometry{
        0x08000000U, 262144U, 32U,
        sectors,     2U,      NX_FLASH_PROGRAM_REQUIRES_FRESH_ERASE};
    static const nx_flash_geometry_t* describe(const void* context) {
        return &static_cast<const FlashMaintenance*>(context)->geometry;
    }
    static nx_result_t erase(void* context, uint32_t offset, size_t count,
                             nx_time_us_t deadline) {
        return static_cast<FlashMaintenance*>(context)->hardware.Erase(
            offset, count, deadline);
    }
    static nx_result_t program(void* context, uint32_t offset,
                               const void* bytes, size_t count,
                               nx_time_us_t deadline) {
        return static_cast<FlashMaintenance*>(context)->hardware.Program(
            offset, bytes, count, deadline);
    }
    static nx_result_t read(const void* context, uint32_t offset, void* bytes,
                            size_t count) {
        return const_cast<FlashMaintenance*>(
                   static_cast<const FlashMaintenance*>(context))
            ->hardware.Read(offset, bytes, count);
    }
    const nx_flash_ops_t operations{describe, read, program, erase};
    const nx_flash_port_t port{&operations, this};
    nx_flash_region_t region{&port, 131072U, 131072U, true};
    example_flash_report_t report{};
    uint8_t payload[32]{};
    unsigned char programmed[32]{};
    void expect_erase() {
        EXPECT_CALL(hardware, Erase(131072U, 131072U, 1234U))
            .WillOnce(testing::Return(NX_SUCCESS));
    }
    void expect_program() {
        EXPECT_CALL(hardware, Program(131072U, payload, 32U, 1234U))
            .WillOnce([this](uint32_t, const void* input, size_t count,
                             nx_time_us_t) {
                const uint8_t expected[32] = {0x4eU, 0x58U, 0x52U, 0x41U,
                                              0x4dU};
                EXPECT_EQ(std::memcmp(input, expected, count), 0);
                std::memcpy(programmed, input, count);
                return NX_SUCCESS;
            });
    }
};
TEST_F(FlashMaintenance, NoAuthorizationCannotEraseOrProgram) {
    EXPECT_EQ(
        example_flash_maintenance(&region, false, 1234U, payload, &report),
        NX_ERROR_PERMISSION);
    EXPECT_FALSE(report.erased);
}
TEST_F(FlashMaintenance, RejectsPartialSectorReadOnlyAndInvalidDeadline) {
    region.offset += 32U;
    EXPECT_EQ(example_flash_maintenance(&region, true, 1234U, payload, &report),
              NX_ERROR_INVALID);
    region.offset -= 32U;
    region.writable = false;
    EXPECT_EQ(example_flash_maintenance(&region, true, 1234U, payload, &report),
              NX_ERROR_PERMISSION);
    region.writable = true;
    EXPECT_EQ(example_flash_maintenance(&region, true, NX_DEADLINE_NEVER,
                                        payload, &report),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_flash_maintenance(&region, true, 1234U, payload, nullptr),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_flash_maintenance(nullptr, true, 1234U, payload, &report),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_flash_maintenance(&region, true, 1234U, nullptr, &report),
              NX_ERROR_INVALID);
}
TEST_F(FlashMaintenance, EraseFailureNeverConsumesAnEccProgramUnit) {
    EXPECT_CALL(hardware, Erase(131072U, 131072U, 1234U))
        .WillOnce(testing::Return(NX_ERROR_TIMEOUT));
    EXPECT_EQ(example_flash_maintenance(&region, true, 1234U, payload, &report),
              NX_ERROR_TIMEOUT);
    EXPECT_FALSE(report.erased);
    EXPECT_FALSE(report.programmed);
}
TEST_F(FlashMaintenance, ProgramFailureNeverRetriesOrReportsVerification) {
    testing::InSequence order;
    expect_erase();
    EXPECT_CALL(hardware, Program(131072U, testing::_, 32U, 1234U))
        .WillOnce(testing::Return(NX_ERROR_IO));
    EXPECT_EQ(example_flash_maintenance(&region, true, 1234U, payload, &report),
              NX_ERROR_IO);
    EXPECT_TRUE(report.erased);
    EXPECT_FALSE(report.programmed);
    EXPECT_FALSE(report.verified);
}
TEST_F(FlashMaintenance, SuccessfulFreshErasePrecedesSinglePulseAndReadback) {
    testing::InSequence order;
    expect_erase();
    expect_program();
    EXPECT_CALL(hardware, Read(131072U, testing::_, 32U))
        .WillOnce([this](uint32_t, void* destination, size_t count) {
            std::memcpy(destination, programmed, count);
            return NX_SUCCESS;
        });
    EXPECT_EQ(example_flash_maintenance(&region, true, 1234U, payload, &report),
              NX_SUCCESS);
    EXPECT_TRUE(report.erased);
    EXPECT_TRUE(report.programmed);
    EXPECT_TRUE(report.verified);
}
TEST_F(FlashMaintenance, SuccessfulReadWithWrongBytesIsStillFailure) {
    testing::InSequence order;
    expect_erase();
    expect_program();
    EXPECT_CALL(hardware, Read(131072U, testing::_, 32U))
        .WillOnce([](uint32_t, void* destination, size_t count) {
            std::memset(destination, 0xff, count);
            return NX_SUCCESS;
        });
    EXPECT_EQ(example_flash_maintenance(&region, true, 1234U, payload, &report),
              NX_ERROR_IO);
    EXPECT_TRUE(report.programmed);
    EXPECT_FALSE(report.verified);
}
TEST_F(FlashMaintenance, FailedReadPreservesItsErrorAfterTheSettledPulse) {
    testing::InSequence order;
    expect_erase();
    expect_program();
    EXPECT_CALL(hardware, Read(131072U, testing::_, 32U))
        .WillOnce(testing::Return(NX_ERROR_IO));
    EXPECT_EQ(example_flash_maintenance(&region, true, 1234U, payload, &report),
              NX_ERROR_IO);
    EXPECT_TRUE(report.programmed);
    EXPECT_FALSE(report.verified);
}
TEST_F(FlashMaintenance, AnotherProgrammingGeometryRejectsBeforeAnyPulse) {
    geometry.program_unit = 8U;
    EXPECT_EQ(example_flash_maintenance(&region, true, 1234U, payload, &report),
              NX_ERROR_INVALID);
    EXPECT_FALSE(report.erased);
}
} /* namespace */
