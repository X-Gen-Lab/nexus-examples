/**
 * \file            imu_reference.h
 * \brief           Explicit LSM6DSOX calibration, mounting and attitude recipe
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-22
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_IMU_REFERENCE_H
#define EXAMPLE_IMU_REFERENCE_H
#include "nexus/algorithms/attitude.h"
#include "nexus/components/lsm6dsox.h"
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Product coefficients, mounting and initial estimate; explicit units.
 * \note Accelerometer offsets use m/s^2, gyro offsets rad/s, and scales are
 *       dimensionless. Sensor SI conversion precedes this product calibration.
 */
typedef struct {
    nx_vec3_calibration_t accelerometer;
    nx_vec3_calibration_t gyroscope;
    nx_quatf_t sensor_to_body;
    nx_quatf_t initial_body_to_world;
    nx_attitude6_config_t fusion;
    uint32_t calibration_version;
} example_imu_config_t;
/** \brief Independent values with the original software observation facts. */
typedef struct {
    nx_lsm6dsox_sample_t sample;
    nx_vec3f_t acceleration_body;
    nx_vec3f_t angular_rate_body;
    nx_attitude6_result_t attitude;
    uint32_t calibration_version;
} example_imu_observation_t;
/** \brief Caller-owned single-task composition; join users before reinit. */
typedef struct {
    nx_lsm6dsox_t* sensor;
    example_imu_config_t config;
    nx_attitude6_t fusion;
    uint32_t generation;
    uint32_t sequence;
    uint32_t clock_domain;
    uint32_t clock_epoch;
    bool have_sample;
    bool faulted;
    bool entered;
} example_imu_reference_t;
/**
 * \brief           Bind the already running DIRECT sensor without I/O.
 * \param[in,out]   reference: Fresh zeroed or fully joined caller storage.
 * \param[in,out]   sensor: Stable single-owner device, retained until join.
 * \param[in]       config: Separate explicit calibration and axis snapshot.
 * \return          SUCCESS, INVALID or BUSY; rejection preserves reference.
 * \note            Product owns coefficients, units, mounting, identity and
 *                  startup qualification. Both quaternion conventions are
 *                  right-handed Hamilton. No device initialization or worker.
 */
nx_result_t example_imu_init(example_imu_reference_t* reference,
                             nx_lsm6dsox_t* sensor,
                             const example_imu_config_t* config);
/**
 * \brief           Read, calibrate, rotate and advance one explicit estimate.
 * \param[in,out]   reference: Initialized serialized task owner.
 * \param[in]       dt_s: Positive finite caller-proven sampling interval.
 * \param[in]       deadline: Finite whole sensor-read deadline.
 * \param[out]      out: Separate copied observation, unchanged on failure.
 * \return          SUCCESS or exact read/math error; reset/sequence/clock
 *                  discontinuity returns STATE and latches resynchronization.
 * \note            dt_s is not derived from SERVICE observation timestamps.
 *                  Product proves interval and pairing; direct reads do not
 *                  detect missed physical sensor samples. Calibration/math
 *                  failure after a read also requires explicit reinit. Join
 *                  users before reinit; device stop/reset remains a separate
 *                  responsibility. No allocation, persistence, scheduler,
 *                  automatic reset or absolute heading guarantee.
 */
nx_result_t example_imu_step(example_imu_reference_t* reference, float dt_s,
                             nx_time_us_t deadline,
                             example_imu_observation_t* out);
#ifdef __cplusplus
}
#endif
#endif
