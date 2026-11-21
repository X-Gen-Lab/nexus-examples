/**
 * \file            diagnostics_reference_test.cpp
 * \brief           Real diagnostic components under external caller policy
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "maintenance.h"
#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
namespace {
/** \brief Finite external byte transport; real Shell and Log are not mocked. */
class Transport {
  public:
    std::string input;
    std::string output;
    size_t consumed{0U};
    unsigned reads{0U};
    unsigned flushes{0U};
    nx_time_us_t last_deadline{0U};
    nx_result_t read_result{NX_SUCCESS};
    nx_result_t write_result{NX_SUCCESS};
    nx_result_t flush_result{NX_SUCCESS};
    static nx_result_t read(void* context, uint8_t* bytes, size_t capacity,
                            size_t* count) {
        auto& self = *static_cast<Transport*>(context);
        ++self.reads;
        *count = 0U;
        if (self.read_result != NX_SUCCESS)
            return self.read_result;
        if (self.consumed == self.input.size())
            return NX_ERROR_EMPTY;
        *count = std::min(capacity, self.input.size() - self.consumed);
        std::memcpy(bytes, self.input.data() + self.consumed, *count);
        self.consumed += *count;
        return NX_SUCCESS;
    }
    static nx_result_t write(void* context, const void* bytes, size_t count,
                             nx_time_us_t deadline) {
        auto& self = *static_cast<Transport*>(context);
        self.last_deadline = deadline;
        if (self.write_result == NX_SUCCESS)
            self.output.append(static_cast<const char*>(bytes), count);
        return self.write_result;
    }
    static nx_result_t log(void* context, nx_log_level_t, const void* bytes,
                           size_t count, nx_time_us_t deadline) {
        return write(context, bytes, count, deadline);
    }
    static nx_result_t flush(void* context, nx_time_us_t deadline) {
        auto& self = *static_cast<Transport*>(context);
        ++self.flushes;
        self.last_deadline = deadline;
        return self.flush_result;
    }
    nx_shell_io_t io() {
        return {this, read, write};
    }
    nx_log_sink_port_t sink() {
        return {this, log, flush};
    }
};
class DiagnosticsRecipe : public testing::Test {
  protected:
    Transport transport;
    example_diagnostics_t state{};
    nx_fault_record_t retained{};
    nx_progress_t progress{10U, 1U, true};
    nx_health_facts_t facts{&progress, nullptr, 40U, 20U};
    nx_result_t start() {
        return example_diagnostics_start(&state, transport.io(),
                                         transport.sink(), "recipe-1",
                                         &retained, 5U, &facts, 100U);
    }
    void TearDown() override {
        transport.flush_result = NX_SUCCESS;
        EXPECT_EQ(example_diagnostics_stop(&state, 200U), NX_SUCCESS);
    }
};
TEST_F(DiagnosticsRecipe,
       CopiesBootFactsAndExecutesReadOnlyCommandsWithinBudget) {
    nx_fault_capture_t fault{};
    fault.sequence = 7U;
    fault.reason = 3U;
    ASSERT_EQ(nx_fault_record_capture(&retained, &fault), NX_SUCCESS);
    const auto before = retained;
    ASSERT_EQ(start(), NX_SUCCESS);
    EXPECT_TRUE(state.reset.has_fault);
    EXPECT_EQ(state.reset.fault.sequence, 7U);
    EXPECT_EQ(state.health.findings & NX_HEALTH_PROGRESS_OVERDUE,
              NX_HEALTH_PROGRESS_OVERDUE);
    transport.input = "health\rfault\rreset\r";
    ASSERT_EQ(example_diagnostics_step(&state, &facts, 3U, 120U), NX_SUCCESS);
    EXPECT_EQ(transport.consumed, 3U);
    for (size_t i = 0; i < transport.input.size(); ++i) {
        const auto result = example_diagnostics_step(&state, &facts, 1U, 120U);
        ASSERT_TRUE(result == NX_SUCCESS || result == NX_ERROR_EMPTY);
    }
    EXPECT_NE(transport.output.find("findings="), std::string::npos);
    EXPECT_NE(transport.output.find("sequence=7"), std::string::npos);
    EXPECT_NE(transport.output.find("reset=0x00000005"), std::string::npos);
    EXPECT_EQ(transport.last_deadline, 120U);
    EXPECT_EQ(std::memcmp(&retained, &before, sizeof(retained)), 0);
}
TEST_F(DiagnosticsRecipe, InvalidBudgetCannotConsumeInputOrChangeHealth) {
    ASSERT_EQ(start(), NX_SUCCESS);
    const auto before = state.health;
    transport.input = "reset\r";
    facts.observed_at = 99U;
    EXPECT_EQ(example_diagnostics_step(&state, &facts, 0U, 120U),
              NX_ERROR_INVALID);
    EXPECT_EQ(example_diagnostics_step(&state, &facts, 17U, 120U),
              NX_ERROR_INVALID);
    EXPECT_EQ(transport.reads, 0U);
    EXPECT_EQ(std::memcmp(&state.health, &before, sizeof(before)), 0);
    EXPECT_TRUE(state.shell_live);
}
TEST_F(DiagnosticsRecipe, PromptAndReadFailuresReleaseTheRealShellSlot) {
    transport.write_result = NX_ERROR_IO;
    EXPECT_EQ(start(), NX_ERROR_IO);
    EXPECT_FALSE(state.shell_live);
    EXPECT_FALSE(state.log_live);
    transport.write_result = NX_SUCCESS;
    ASSERT_EQ(start(), NX_SUCCESS);
    transport.read_result = NX_ERROR_IO;
    EXPECT_EQ(example_diagnostics_step(&state, &facts, 4U, 150U), NX_ERROR_IO);
    EXPECT_FALSE(state.shell_live);
    EXPECT_FALSE(state.log_live);
    EXPECT_EQ(state.exit_result, NX_SUCCESS);
    EXPECT_EQ(example_diagnostics_step(&state, &facts, 1U, 160U),
              NX_ERROR_STATE);
}
TEST_F(DiagnosticsRecipe, BusyFlushRetainsSinkStateUntilCallerRetriesExit) {
    ASSERT_EQ(start(), NX_SUCCESS);
    transport.flush_result = NX_ERROR_BUSY;
    EXPECT_EQ(example_diagnostics_stop(&state, 170U), NX_ERROR_BUSY);
    EXPECT_FALSE(state.shell_live);
    EXPECT_TRUE(state.log_live);
    EXPECT_EQ(transport.flushes, 1U);
    EXPECT_EQ(transport.last_deadline, 170U);
    EXPECT_EQ(example_diagnostics_step(&state, &facts, 1U, 180U),
              NX_ERROR_STATE);
    transport.flush_result = NX_SUCCESS;
    EXPECT_EQ(example_diagnostics_stop(&state, 190U), NX_SUCCESS);
    EXPECT_FALSE(state.log_live);
    EXPECT_EQ(transport.flushes, 2U);
}
} /* namespace */
