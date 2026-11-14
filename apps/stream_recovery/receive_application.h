/**
 * \file            receive_application.h
 * \brief           Application-owned receive loans and bounded shutdown
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-28
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_RECEIVE_APPLICATION_H
#define EXAMPLE_RECEIVE_APPLICATION_H

#include "nexus/io/stream.h"
#include "nexus/io/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \brief Two consumer tokens; static storage survives every failed release. */
typedef struct {
    nx_stream_block_t held;
    nx_stream_block_t pending;
    bool has_held;
    bool has_pending;
    uint32_t received;
    uint32_t losses;
} example_receive_consumer_t;

/**
 * \brief           Consume at most one block, retaining every failed release.
 * \param[in,out]   stream: Initialized stream with this serialized consumer.
 * \param[in,out]   consumer: Zero-initialized application storage, never reset
 *                  while either token remains borrowed.
 * \param[in]       retain: Delay the first held block; process later blocks.
 * \return          SUCCESS on empty or a consumed block; exact release error.
 * \note            No allocation, wait or payload access after release.
 */
nx_result_t example_receive_consume(nx_stream_t* stream,
                                    example_receive_consumer_t* consumer,
                                    bool retain);

/**
 * \brief           Release pending and delayed loans without losing tokens.
 * \param[in,out]   stream: Same stream that issued both exact loan identities.
 * \param[in,out]   consumer: Same serialized consumer; clear flags on success.
 * \return          SUCCESS only after both releases succeed; first error.
 */
nx_result_t example_receive_release(nx_stream_t* stream,
                                    example_receive_consumer_t* consumer);

/**
 * \brief           Close production and attempt at most 64 service/drain steps.
 * \param[in]       uart: Started or rejected-start port owned by this executor.
 * \param[in,out]   stream: Storage remains live after failure or BUSY.
 * \param[in,out]   consumer: Tokens remain unchanged until release succeeds.
 * \return          SUCCESS after producer stop and consumer release; BUSY or
 *                  provider/release error otherwise retains responsibility.
 * \note            No time reads; callers may invoke this during shutdown.
 */
nx_result_t example_receive_stop(const nx_uart_port_t* uart,
                                 nx_stream_t* stream,
                                 example_receive_consumer_t* consumer);

#ifdef __cplusplus
}
#endif
#endif
