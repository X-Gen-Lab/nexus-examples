/**
 * \file            developer_recipes_test.cpp
 * \brief           Application recipes use actual public ownership mechanisms
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "nexus/io/diagnostic.h"
#include "shared_spi_application.h"
#include "stream_application.h"
#include <gtest/gtest.h>

TEST(DeveloperRecipes, TwoDevicesCompleteAndQueuedCancellationRetainsStorage) {
    example_spi_report_t report = {};
    ASSERT_EQ(example_shared_spi_run(&report), 0);
    EXPECT_EQ(report.completed, 3U);
    EXPECT_EQ(report.cancelled, 1U);
    EXPECT_EQ(report.rejected, 2U);
    EXPECT_TRUE(report.joined);
    EXPECT_TRUE(report.stopped);
}

TEST(DeveloperRecipes, SlowConsumerKeepsLoansAndReportsLossBeforeRecovery) {
    example_stream_report_t report = {};
    ASSERT_EQ(example_stream_run(&report), 0);
    EXPECT_GT(report.lost_blocks, 0U);
    EXPECT_TRUE(report.borrow_preserved);
    EXPECT_TRUE(report.stop_waited);
    EXPECT_NE(report.diagnostic_findings & NX_IO_DIAG_STREAM_OUTSTANDING, 0U);
    EXPECT_NE(report.diagnostic_findings & NX_IO_DIAG_STOPPING, 0U);
    EXPECT_NE(report.loss_diagnostic_findings & NX_IO_DIAG_STREAM_LOSS, 0U);
    EXPECT_TRUE(report.restarted);
    EXPECT_TRUE(report.stopped);
}
