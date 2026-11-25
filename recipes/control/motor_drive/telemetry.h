/**
 * \file            telemetry.h
 * \brief           Task-only versioned drive telemetry with bounded progress
 * \author          Nexus Team
 */
#ifndef EXAMPLE_MOTOR_DRIVE_TELEMETRY_H
#define EXAMPLE_MOTOR_DRIVE_TELEMETRY_H
#include "observation.h"
#ifdef __cplusplus
extern "C" {
#endif
#define EXAMPLE_MOTOR_TELEMETRY_BYTES 116U
/** \brief One bounded synchronous copy attempt; never retain bytes.
 * \note written is the committed prefix, including on BUSY/error, and must
 *       not exceed length. SUCCESS may be short. No wait, hidden task or DMA
 *       borrow is permitted. An async UART needs its own settled adapter.
 */
typedef nx_result_t (*example_motor_telemetry_write_t)(void* context,
                                                       const uint8_t* bytes,
                                                       size_t length,
                                                       size_t* written);
/** \brief Single Task owns the transport and pending frame through drain.
 * \note Zero before initialize. dropped is the latest cumulative channel
 *       loss observed by this Task; changing it affects the next frame only.
 *       Pending frames stay associated with the retained channel head. Join
 *       this consumer before reclaiming state/context. broken requires a new
 *       transport/frame boundary before reinitialization; never replay a
 *       prefix whose writer reported impossible progress.
 */
typedef struct {
    example_motor_telemetry_write_t write;
    void* context;
    uint8_t frame[EXAMPLE_MOTOR_TELEMETRY_BYTES];
    size_t offset;
    uint64_t sequence;
    uint64_t epoch;
    uint32_t tick_hz;
    uint32_t dropped;
    bool pending;
    bool broken;
} example_motor_telemetry_t;
/** \brief Initialize unused single-consumer transport state before publication.
 * \param[out] telemetry: Zeroed or explicitly withdrawn unborrowed storage.
 * \param[in] write: Stable bounded byte-copy port, called at most once per
 * sink. \param[in,out] context: Port state retained until this consumer joins.
 * \param[in] tick_hz: Actual selected carrier tick rate, not a measured clock.
 * \return SUCCESS, INVALID or CONTEXT; initialization performs no transport
 * I/O. \note Task only. A new transport must establish a frame boundary after a
 *       failed partial stream; initialization never discards a live borrow.
 */
nx_result_t
example_motor_telemetry_initialize(example_motor_telemetry_t* telemetry,
                                   example_motor_telemetry_write_t write,
                                   void* context, uint32_t tick_hz);
/** \brief Use directly as the observation channel's Task sink.
 * \param[in,out] context: Initialized telemetry state and unique byte-stream
 * port. \param[in] record: Same retained channel head until SUCCESS; never
 * retained. \return SUCCESS only when the entire encoded frame was committed,
 * BUSY for a short write, or exact transport/validation/context error. \note
 * One write attempt per call. A transport error retains committed progress; no
 * prefix is replayed. IRQ publication is independent.
 */
nx_result_t
example_motor_telemetry_sink(void* context,
                             const example_motor_observation_t* record);
#ifdef __cplusplus
}
#endif
#endif
