/**
 * \file            dual_motor_test.cpp
 * \brief           Real three-loop and dual PMSM consumer contracts
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "sil.h"
#include <gtest/gtest.h>

namespace {
TEST(DualPmsm, DistinctPmsmsTrackOpposingVelocityCommands) {
    const auto experiment = example_dual_motor_reference();
    example_dual_motor_report_t report{};
    ASSERT_EQ(example_dual_motor_run(&experiment, &report), NX_SUCCESS);
    ASSERT_TRUE(report.all_settled);
    ASSERT_GT(report.evaluation_samples, 0U);
    for (size_t axis = 0; axis < 2; ++axis) {
        EXPECT_EQ(report.fault[axis], NX_SUCCESS);
        EXPECT_GT(report.adopted_generation[axis], 10U);
        EXPECT_LT(report.rms_velocity_error_rad_s[axis], 0.8);
        EXPECT_GT(report.completed_cycles[axis], 10000U);
    }
    EXPECT_GT(report.velocity_rad_s[0], 15);
    EXPECT_LT(report.velocity_rad_s[1], -5);
}

TEST(DualPmsm, CurrentModeClosesPmsmDqLoopThroughAdcCodes) {
    auto experiment = example_dual_motor_reference();
    experiment.duration_us = 30000;
    experiment.evaluation_start_us = 20000;
    for (auto& control : experiment.control) {
        control.initial_mode = NX_AXIS_CURRENT;
        control.initial_velocity_rad_s = 0;
        control.iq_a = 0.3F;
    }
    example_dual_motor_report_t report{};
    ASSERT_EQ(example_dual_motor_run(&experiment, &report), NX_SUCCESS);
    ASSERT_TRUE(report.all_settled);
    for (size_t axis = 0; axis < 2; ++axis) {
        EXPECT_EQ(report.fault[axis], NX_SUCCESS);
        EXPECT_LT(report.rms_current_error_a[axis], 0.08);
    }
}

TEST(DualPmsm, PositionLoopUsesActualQuantizedMechanicalFeedback) {
    auto experiment = example_dual_motor_reference();
    for (size_t axis = 0; axis < 2; ++axis) {
        experiment.control[axis].initial_mode = NX_AXIS_POSITION;
        experiment.control[axis].initial_velocity_rad_s = 0;
        experiment.control[axis].initial_position_rad =
            axis == 0 ? 0.5F : -0.3F;
    }
    example_dual_motor_report_t report{};
    ASSERT_EQ(example_dual_motor_run(&experiment, &report), NX_SUCCESS);
    ASSERT_TRUE(report.all_settled);
    EXPECT_EQ(report.fault[0], NX_SUCCESS);
    EXPECT_EQ(report.fault[1], NX_SUCCESS);
    EXPECT_NEAR(report.position_rad[0], 0.5, 0.015);
    EXPECT_NEAR(report.position_rad[1], -0.3, 0.015);
    EXPECT_NEAR(report.measured_position_rad[0], report.position_rad[0], 0.002);
    EXPECT_NEAR(report.measured_position_rad[1], report.position_rad[1], 0.002);
}

TEST(DualPmsm, ThreeShuntProfileCanUseTwoSelectedObservablePhases) {
    auto experiment = example_dual_motor_reference();
    for (auto& control : experiment.control) {
        control.phase_samples = 2;
    }
    example_dual_motor_report_t report{};
    ASSERT_EQ(example_dual_motor_run(&experiment, &report), NX_SUCCESS);
    EXPECT_TRUE(report.all_settled);
    for (size_t axis = 0; axis < 2; ++axis) {
        EXPECT_EQ(report.fault[axis], NX_SUCCESS);
        EXPECT_LT(report.rms_velocity_error_rad_s[axis], 0.8);
        EXPECT_GT(report.completed_cycles[axis], 10000U);
    }
}

TEST(DualPmsm, LostTaskAuthorityFaultsOnlyTheAffectedAxisAndJoinsBoth) {
    auto experiment = example_dual_motor_reference();
    experiment.task_pause_at_us = 100000;
    experiment.task_pause_for_us = 10000;
    example_dual_motor_report_t report{};
    ASSERT_EQ(example_dual_motor_run(&experiment, &report), NX_SUCCESS);
    EXPECT_TRUE(report.all_settled);
    EXPECT_EQ(report.fault[0], NX_ERROR_TIMEOUT);
    EXPECT_EQ(report.fault[1], NX_SUCCESS);
    EXPECT_LT(report.completed_cycles[0], report.completed_cycles[1]);
}

TEST(DualPmsm, SaturatedSensorClosesOneAxisWithoutInventingFeedback) {
    auto experiment = example_dual_motor_reference();
    experiment.sensor_fault_at_us = 100000;
    example_dual_motor_report_t report{};
    ASSERT_EQ(example_dual_motor_run(&experiment, &report), NX_SUCCESS);
    EXPECT_TRUE(report.all_settled);
    EXPECT_NE(report.fault[0], NX_SUCCESS);
    EXPECT_EQ(report.fault[1], NX_SUCCESS);
    EXPECT_LT(report.completed_cycles[0], report.completed_cycles[1]);
}

TEST(DualPmsm, InhibitAfterDwellIsSuccessfulWithdrawalNotAnInventedFault) {
    auto experiment = example_dual_motor_reference();
    experiment.controlled_stop_at_us = 300000;
    for (auto& control : experiment.control) {
        control.axis.stop_policy = NX_AXIS_INHIBIT_AFTER_DWELL;
    }
    example_dual_motor_report_t report{};
    ASSERT_EQ(example_dual_motor_run(&experiment, &report), NX_SUCCESS);
    EXPECT_TRUE(report.all_settled);
    EXPECT_EQ(report.fault[0], NX_SUCCESS);
    EXPECT_EQ(report.fault[1], NX_SUCCESS);
    EXPECT_LT(report.completed_cycles[0], 20000U);
    EXPECT_LT(report.completed_cycles[1], 20000U);
}

TEST(DualPmsm, SharedMotoringBudgetReachesBothActualCurrentLoops) {
    auto experiment = example_dual_motor_reference();
    experiment.bus_limits_at_us = 100000;
    experiment.motoring_budget_w = 10;
    experiment.regeneration_budget_w = 100;
    example_dual_motor_report_t report{};
    ASSERT_EQ(example_dual_motor_run(&experiment, &report), NX_SUCCESS);
    ASSERT_TRUE(report.all_settled);
    ASSERT_GT(report.budget_updates, 100U);
    EXPECT_GT(report.motoring_samples, 100U);
    EXPECT_LE(report.admitted_power_bound_w, 10.001F);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(report.fault[i], NX_SUCCESS);
        EXPECT_LE(report.admitted_current_a[i], 0.251F);
        EXPECT_GT(report.admitted_current_a[i], 0.01F);
    }
}

TEST(DualPmsm, RegenerationIsReservedSeparatelyWithoutOpposingPowerCredit) {
    auto experiment = example_dual_motor_reference();
    experiment.bus_limits_at_us = 300000;
    experiment.controlled_stop_at_us = 400000;
    experiment.stop_axis_mask = 1;
    experiment.motoring_budget_w = 100;
    experiment.regeneration_budget_w = 2;
    example_dual_motor_report_t report{};
    ASSERT_EQ(example_dual_motor_run(&experiment, &report), NX_SUCCESS);
    ASSERT_TRUE(report.all_settled);
    EXPECT_GT(report.regenerating_samples, 0U);
    EXPECT_GT(report.opposed_flow_samples, 0U);
    EXPECT_GT(report.budget_updates, 100U);
    EXPECT_LE(report.admitted_power_bound_w, 2.001F);
    EXPECT_EQ(report.fault[0], NX_SUCCESS);
    EXPECT_EQ(report.fault[1], NX_SUCCESS);
}

TEST(DualPmsm, BothRegeneratingAxesUseTheirOwnReservedCurrentRanges) {
    auto experiment = example_dual_motor_reference();
    experiment.bus_limits_at_us = 300000;
    experiment.controlled_stop_at_us = 400000;
    experiment.motoring_budget_w = 100;
    experiment.regeneration_budget_w = 2;
    example_dual_motor_report_t report{};
    ASSERT_EQ(example_dual_motor_run(&experiment, &report), NX_SUCCESS);
    EXPECT_TRUE(report.all_settled);
    EXPECT_GT(report.both_regenerating_samples, 0U);
    EXPECT_LE(report.admitted_power_bound_w, 2.001F);
    EXPECT_EQ(report.fault[0], NX_SUCCESS);
    EXPECT_EQ(report.fault[1], NX_SUCCESS);
}

TEST(DualPmsm, ExpiredSharedBusAuthorityCannotBeRenewedByOuterLoop) {
    auto experiment = example_dual_motor_reference();
    experiment.bus_limits_at_us = 50000;
    experiment.bus_source_stop_at_us = 100000;
    experiment.motoring_budget_w = 20;
    experiment.regeneration_budget_w = 20;
    example_dual_motor_report_t report{};
    ASSERT_EQ(example_dual_motor_run(&experiment, &report), NX_SUCCESS);
    EXPECT_TRUE(report.all_settled);
    EXPECT_GT(report.budget_updates, 0U);
    EXPECT_EQ(report.fault[0], NX_ERROR_TIMEOUT);
    EXPECT_EQ(report.fault[1], NX_ERROR_TIMEOUT);
    EXPECT_LT(report.completed_cycles[0], 2200U);
    EXPECT_LT(report.completed_cycles[1], 2200U);
}

TEST(DualPmsm, IqOnlyBudgetCannotPretendToCoverIndependentDCurrentPower) {
    auto experiment = example_dual_motor_reference();
    experiment.bus_limits_at_us = 50000;
    experiment.motoring_budget_w = 20;
    experiment.regeneration_budget_w = 20;
    experiment.control[0].id_a = 0.25F;
    example_dual_motor_report_t report{};
    EXPECT_EQ(example_dual_motor_run(&experiment, &report), NX_ERROR_INVALID);
}
} /* namespace */
