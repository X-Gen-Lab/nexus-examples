/**
 * \file            terminal_reference_test.cpp
 * \brief           Bounded tiles and persistent raw-fault export composition
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "display_reference.h"
#include "fault_export.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

namespace {
/** \brief Only the physical settled parallel bus and clock are modeled. */
class Panel {
  public:
    nx_time_us_t now{0U};
    uint16_t command{};
    unsigned accesses{};
    bool fail_pixels{};
    std::vector<uint16_t> pixels;
    static nx_time_us_t clock(void* context) {
        return static_cast<Panel*>(context)->now;
    }
    static nx_result_t send(void* context, uint16_t value, nx_time_us_t) {
        auto& self = *static_cast<Panel*>(context);
        ++self.accesses;
        self.command = value;
        return NX_SUCCESS;
    }
    static nx_result_t write(void* context, const uint16_t* values,
                             size_t count, size_t* done, nx_time_us_t) {
        auto& self = *static_cast<Panel*>(context);
        ++self.accesses;
        *done = self.command == 0x2cU && self.fail_pixels ? 1U : count;
        if (self.command == 0x2cU)
            self.pixels.insert(self.pixels.end(), values, values + *done);
        return self.fail_pixels && self.command == 0x2cU ? NX_ERROR_IO
                                                         : NX_SUCCESS;
    }
    static nx_result_t read(void*, uint16_t* values, size_t count, size_t* done,
                            nx_time_us_t) {
        if (count != 4U)
            return NX_ERROR_IO;
        const uint16_t identity[4] = {0U, 0U, 0x93U, 0x41U};
        std::copy_n(identity, count, values);
        *done = count;
        return NX_SUCCESS;
    }
    const nx_parallel16_ops_t ops{send, write, read};
    const nx_parallel16_endpoint_t endpoint{&ops, this};
    const nx_ili9341_config_t config{&endpoint, {clock, this}, 0U};
};
class TerminalDisplay : public testing::Test {
  protected:
    Panel bus;
    nx_ili9341_t display{};
    example_ui_fill_t fill{};
    std::array<uint16_t, 12U> tile{};
    void SetUp() override {
        ASSERT_EQ(nx_ili9341_initialize(&display, &bus.config, 1000000U),
                  NX_SUCCESS);
        bus.now = 120000U;
        ASSERT_EQ(nx_ili9341_service(&display), NX_ERROR_BUSY);
        bus.now = 240000U;
        ASSERT_EQ(nx_ili9341_service(&display), NX_SUCCESS);
        bus.accesses = 0U;
    }
};
TEST_F(TerminalDisplay, EachStepUsesOneCallerSizedTileAndSettlesItsSpan) {
    ASSERT_EQ(example_ui_fill_begin(&fill, 240U, 320U, 3U, 5U, 4U, 7U, 0x1234U),
              NX_SUCCESS);
    EXPECT_EQ(example_ui_fill_step(&fill, &display, tile.data(), tile.size(),
                                   500000U),
              NX_ERROR_BUSY);
    EXPECT_EQ(fill.completed_pixels, 12U);
    EXPECT_EQ(bus.pixels.size(), 12U);
    std::fill(tile.begin(), tile.end(), 0U);
    EXPECT_EQ(example_ui_fill_step(&fill, &display, tile.data(), tile.size(),
                                   500000U),
              NX_ERROR_BUSY);
    EXPECT_EQ(example_ui_fill_step(&fill, &display, tile.data(), tile.size(),
                                   500000U),
              NX_SUCCESS);
    ASSERT_EQ(bus.pixels.size(), 28U);
    EXPECT_TRUE(std::all_of(bus.pixels.begin(), bus.pixels.end(),
                            [](uint16_t value) { return value == 0x1234U; }));
    EXPECT_FALSE(fill.active);
    EXPECT_EQ(example_ui_fill_step(&fill, &display, tile.data(), tile.size(),
                                   500000U),
              NX_ERROR_EMPTY);
}
TEST_F(TerminalDisplay, InvalidRectangleAndSmallTileCauseNoBusEffect) {
    EXPECT_EQ(example_ui_fill_begin(&fill, 240U, 320U, 239U, 0U, 2U, 1U, 1U),
              NX_ERROR_INVALID);
    EXPECT_EQ(bus.accesses, 0U);
    ASSERT_EQ(example_ui_fill_begin(&fill, 240U, 320U, 0U, 0U, 4U, 1U, 1U),
              NX_SUCCESS);
    EXPECT_EQ(example_ui_fill_step(&fill, &display, tile.data(), 3U, 500000U),
              NX_ERROR_EXHAUSTED);
    EXPECT_EQ(fill.completed_pixels, 0U);
    EXPECT_EQ(bus.accesses, 0U);
}
TEST_F(TerminalDisplay, PartialPanelErrorClosesWorkWithoutAutomaticReplay) {
    ASSERT_EQ(example_ui_fill_begin(&fill, 240U, 320U, 0U, 0U, 4U, 2U, 1U),
              NX_SUCCESS);
    bus.fail_pixels = true;
    EXPECT_EQ(example_ui_fill_step(&fill, &display, tile.data(), tile.size(),
                                   500000U),
              NX_ERROR_IO);
    EXPECT_EQ(fill.completed_pixels, 1U);
    EXPECT_TRUE(fill.failed);
    EXPECT_EQ(display.phase, NX_ILI9341_UNCERTAIN);
    const unsigned before = bus.accesses;
    EXPECT_EQ(example_ui_fill_step(&fill, &display, tile.data(), tile.size(),
                                   500000U),
              NX_ERROR_STATE);
    EXPECT_EQ(bus.accesses, before);
    bus.fail_pixels = false;
    EXPECT_EQ(nx_ili9341_stop(&display, 500000U), NX_SUCCESS);
}
TEST_F(TerminalDisplay, ActiveWorkAndAliasedScratchCannotReplaceLiveProgress) {
    ASSERT_EQ(example_ui_fill_begin(&fill, 240U, 320U, 0U, 0U, 4U, 2U, 1U),
              NX_SUCCESS);
    EXPECT_EQ(example_ui_fill_begin(&fill, 240U, 320U, 0U, 0U, 1U, 1U, 2U),
              NX_ERROR_STATE);
    EXPECT_EQ(example_ui_fill_step(&fill, &display,
                                   reinterpret_cast<uint16_t*>(&fill), 4U,
                                   500000U),
              NX_ERROR_INVALID);
    EXPECT_EQ(fill.completed_pixels, 0U);
    EXPECT_EQ(fill.color, 1U);
    EXPECT_EQ(bus.accesses, 0U);
}

/** \brief Only completed SPI exchanges and live pen voltage are modeled. */
class TouchBus {
  public:
    unsigned transfers{};
    bool pressed{true};
    static nx_time_us_t clock(void*) {
        return 10U;
    }
    static nx_result_t transfer(void* context, const uint8_t* tx, uint8_t* rx,
                                size_t count, nx_time_us_t, size_t* done) {
        auto& self = *static_cast<TouchBus*>(context);
        ++self.transfers;
        if (count != 3U)
            return NX_ERROR_IO;
        const uint16_t value = tx[0] == 0xd0U ? 100U : 200U;
        const uint16_t wire = static_cast<uint16_t>(value << 3U);
        rx[0] = 0U;
        rx[1] = static_cast<uint8_t>(wire >> 8U);
        rx[2] = static_cast<uint8_t>(wire);
        *done = count;
        return NX_SUCCESS;
    }
    static nx_result_t pen(const void* context, uint32_t* value) {
        *value = static_cast<const TouchBus*>(context)->pressed ? 0U : 1U;
        return NX_SUCCESS;
    }
    const nx_spi_endpoint_ops_t spi_ops{transfer, nullptr, nullptr, nullptr,
                                        nullptr};
    const nx_gpio_ops_t gpio_ops{nullptr, pen, nullptr};
    const nx_spi_endpoint_t spi{&spi_ops, this};
    const nx_gpio_port_t gpio{&gpio_ops, this};
    const nx_xpt2046_config_t config{&spi, &gpio, 1U,    {clock, this},
                                     3U,   4U,    false, nullptr};
};
TEST(TerminalTouch, RealRawSampleFlowsThroughExistingMeasuredCalibration) {
    TouchBus bus;
    nx_xpt2046_t touch{};
    ASSERT_EQ(nx_xpt2046_init(&touch, &bus.config, 1000U), NX_SUCCESS);
    const example_touch_calibration_t calibration{
        100U, 3900U, 200U, 3800U, 240U, 320U, false, false, false};
    example_touch_point_t point{};
    ASSERT_EQ(example_ui_touch(&touch, &calibration, &point, 1000U),
              NX_SUCCESS);
    EXPECT_EQ(point.x, 0U);
    EXPECT_EQ(point.y, 0U);
    EXPECT_EQ(point.raw.x, 100U);
    EXPECT_EQ(point.raw.y, 200U);
    EXPECT_EQ(point.raw.observed.domain, 3U);
    EXPECT_EQ(point.raw.observed.epoch, 4U);
    EXPECT_EQ(point.raw.observed.source, NX_IO_TIMESTAMP_SERVICE_OBSERVATION);
    EXPECT_EQ(nx_xpt2046_stop(&touch, 1000U), NX_SUCCESS);
}
TEST(TerminalTouch, InvalidCalibrationAndPenUpPreserveOutput) {
    TouchBus bus;
    nx_xpt2046_t touch{};
    ASSERT_EQ(nx_xpt2046_init(&touch, &bus.config, 1000U), NX_SUCCESS);
    example_touch_calibration_t calibration{100U, 3900U, 200U,  3800U, 240U,
                                            320U, false, false, false};
    example_touch_point_t point{};
    point.x = 77U;
    calibration.raw_x_min = calibration.raw_x_max;
    const unsigned before = bus.transfers;
    EXPECT_EQ(example_ui_touch(&touch, &calibration, &point, 1000U),
              NX_ERROR_INVALID);
    EXPECT_EQ(bus.transfers, before);
    EXPECT_EQ(point.x, 77U);
    calibration.raw_x_min = 100U;
    bus.pressed = false;
    EXPECT_EQ(example_ui_touch(&touch, &calibration, &point, 1000U),
              NX_ERROR_EMPTY);
    EXPECT_EQ(bus.transfers, before);
    EXPECT_EQ(point.x, 77U);
    EXPECT_EQ(nx_xpt2046_stop(&touch, 1000U), NX_SUCCESS);
}

/** \brief Physical Flash effects only; the real two-bank Storage executes. */
class Medium {
  public:
    std::array<uint8_t, 1024U> bytes{};
    bool fail_program{};
    Medium() {
        bytes.fill(0xffU);
    }
    static nx_storage_status_t read(void* context, size_t offset, void* output,
                                    size_t count) {
        auto& self = *static_cast<Medium*>(context);
        std::memcpy(output, self.bytes.data() + offset, count);
        return NX_STORAGE_OK;
    }
    static nx_storage_status_t program(void* context, size_t offset,
                                       const void* source, size_t count) {
        auto& self = *static_cast<Medium*>(context);
        if (self.fail_program)
            return NX_STORAGE_IO;
        const auto* input = static_cast<const uint8_t*>(source);
        for (size_t i = 0U; i < count; ++i)
            self.bytes[offset + i] &= input[i];
        return NX_STORAGE_OK;
    }
    static nx_storage_status_t erase(void* context, size_t offset,
                                     size_t count) {
        auto& self = *static_cast<Medium*>(context);
        std::fill_n(self.bytes.begin() + static_cast<ptrdiff_t>(offset), count,
                    0xffU);
        return NX_STORAGE_OK;
    }
    static nx_storage_status_t sync(void*) {
        return NX_STORAGE_OK;
    }
    nx_storage_port_t port{this, bytes.size(), 512U,  4U,
                           read, program,      erase, sync};
};
class FaultExport : public testing::Test {
  protected:
    Medium media;
    nx_storage_t storage{};
    std::array<uint8_t, 32U> workspace{};
    nx_fault_record_t retained{};
    nx_fault_capture_t capture{};
    example_fault_export_t state{};
    std::vector<uint8_t> output;
    size_t capacity{7U};
    nx_time_us_t observed_deadline{};
    static nx_result_t write(void* context, const void* source, size_t count,
                             size_t* accepted, nx_time_us_t deadline) {
        auto& self = *static_cast<FaultExport*>(context);
        *accepted = std::min(count, self.capacity);
        const auto* bytes = static_cast<const uint8_t*>(source);
        self.output.insert(self.output.end(), bytes, bytes + *accepted);
        self.observed_deadline = deadline;
        return *accepted == count ? NX_SUCCESS : NX_ERROR_BUSY;
    }
    void SetUp() override {
        ASSERT_EQ(nx_storage_open(&storage, &media.port, 0U, 512U,
                                  workspace.data(), workspace.size()),
                  NX_STORAGE_OK);
        capture.sequence = 11U;
        capture.build_id[0] = 0x31U;
        capture.build_id[15] = 0xabU;
        ASSERT_EQ(nx_fault_record_capture(&retained, &capture), NX_SUCCESS);
    }
};
TEST_F(FaultExport, DurableCopyPrecedesRetainedAckAndExportNeverDeletesIt) {
    const nx_fault_record_t original = retained;
    ASSERT_EQ(example_fault_export_begin(&state, &retained, &storage,
                                         capture.build_id),
              NX_STORAGE_OK);
    EXPECT_EQ(nx_fault_record_read(&retained, &capture), NX_ERROR_EMPTY);
    while (state.offset < NX_FAULT_RECORD_BYTES) {
        const auto result =
            example_fault_export_step(&state, this, write, 19U, 123U);
        ASSERT_TRUE(result == NX_SUCCESS || result == NX_ERROR_BUSY);
    }
    ASSERT_EQ(output.size(), sizeof(original));
    EXPECT_EQ(std::memcmp(output.data(), &original, sizeof(original)), 0);
    EXPECT_EQ(observed_deadline, 123U);
    example_fault_export_t reboot{};
    ASSERT_EQ(example_fault_export_begin(&reboot, &retained, &storage,
                                         original.bytes + 44U),
              NX_STORAGE_OK);
    EXPECT_EQ(reboot.offset, 0U);
}
TEST_F(FaultExport, FailedPersistencePreservesRetainedSourceAndOutputState) {
    media.fail_program = true;
    EXPECT_EQ(example_fault_export_begin(&state, &retained, &storage,
                                         capture.build_id),
              NX_STORAGE_IO);
    EXPECT_EQ(nx_fault_record_read(&retained, &capture), NX_SUCCESS);
    EXPECT_FALSE(state.ready);
}
TEST_F(FaultExport, WrongBuildIdentityCannotExportOrDiscardPersistentData) {
    std::array<uint8_t, NX_FAULT_BUILD_ID_BYTES> wrong{};
    wrong[0] = 1U;
    EXPECT_EQ(
        example_fault_export_begin(&state, &retained, &storage, wrong.data()),
        NX_STORAGE_UNSUPPORTED);
    EXPECT_FALSE(state.ready);
    EXPECT_EQ(example_fault_export_begin(&state, &retained, &storage,
                                         capture.build_id),
              NX_STORAGE_OK);
}
TEST_F(FaultExport, FullSinkAndInvalidBudgetsRetainTheSameSnapshot) {
    ASSERT_EQ(example_fault_export_begin(&state, &retained, &storage,
                                         capture.build_id),
              NX_STORAGE_OK);
    EXPECT_EQ(example_fault_export_step(&state, this, write, 0U, 100U),
              NX_ERROR_INVALID);
    capacity = 0U;
    EXPECT_EQ(example_fault_export_step(&state, this, write, 16U, 100U),
              NX_ERROR_BUSY);
    EXPECT_EQ(state.offset, 0U);
    EXPECT_TRUE(state.ready);
}
TEST_F(FaultExport, CorruptDurableCaptureCannotBecomeAnExportSnapshot) {
    ASSERT_EQ(nx_fault_record_persist(&retained, &storage), NX_STORAGE_OK);
    nx_fault_record_t corrupt{};
    ASSERT_EQ(nx_fault_record_capture(&corrupt, &capture), NX_SUCCESS);
    corrupt.bytes[12] ^= 1U;
    ASSERT_EQ(nx_storage_save(&storage, &corrupt, sizeof(corrupt)),
              NX_STORAGE_OK);
    EXPECT_EQ(example_fault_export_begin(&state, &retained, &storage,
                                         capture.build_id),
              NX_STORAGE_CORRUPT);
    EXPECT_FALSE(state.ready);
}
TEST_F(FaultExport, ImpossibleSinkPrefixCannotAdvanceTheSnapshot) {
    ASSERT_EQ(example_fault_export_begin(&state, &retained, &storage,
                                         capture.build_id),
              NX_STORAGE_OK);
    const auto invalid = [](void*, const void*, size_t count, size_t* copied,
                            nx_time_us_t) {
        *copied = count + 1U;
        return NX_SUCCESS;
    };
    EXPECT_EQ(example_fault_export_step(&state, this, invalid, 16U, 100U),
              NX_ERROR_IO);
    EXPECT_EQ(state.offset, 0U);
    EXPECT_TRUE(state.ready);
}
} /* namespace */
