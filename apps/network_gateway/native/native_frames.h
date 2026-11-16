/**
 * \file            native_frames.h
 * \brief           Explicit Native datagram frame port for the gateway model
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-02
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef NEXUS_EXAMPLE_GATEWAY_NATIVE_FRAMES_H
#define NEXUS_EXAMPLE_GATEWAY_NATIVE_FRAMES_H
#include "nexus/io/ethernet_frames.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Exact host-only storage; one nonblocking UNIX datagram is one frame.
 * \note One serialized task owns this port and the descriptor. The descriptor
 *       is borrowed, never closed, and must remain nonblocking and connected.
 *       There is no PHY, DMA, FCS, IRQ, hardware capture or wire timing model.
 *       TX completes only after the kernel copied a datagram. This is software
 *       IPC admission, not a physical MAC or remote-delivery qualification.
 *       RX bytes remain immutable until the exact matching loan is released.
 *       TX accepts at most eight fragments and 1514 total bytes with a finite
 *       deadline. This software reference does not admit a larger VLAN MTU.
 */
typedef struct {
    nx_eth_frames_port_t face;
    nx_clock_t clock;
    int descriptor;
    nx_time_us_t last_time;
    bool entered;
    nx_eth_frames_rx_loan_t loan;
    nx_eth_frames_state_t state;
    uint8_t receive[NX_ETH_FRAME_MAX];
} example_gateway_native_frames_t;

/**
 * \brief Bind a supplied host descriptor without creating a thread or socket.
 * \param[in,out] port: Zeroed cold or fully stopped storage; no external users.
 * \param[in] descriptor: Connected nonblocking AF_UNIX SOCK_DGRAM descriptor.
 * \param[in] clock: Stable finite monotonic microseconds in the adapter domain.
 * \return SUCCESS, INVALID for substrate/clock, or STATE while still started.
 * \note The shared readonly methods are in face after success. Rebinding
 *       preserves the RX incarnation counter. Stop never closes the fd or
 *       releases a held loan. Clock withdrawal belongs to the outer product.
 */
nx_result_t
example_gateway_native_frames_init(example_gateway_native_frames_t* port,
                                   int descriptor, nx_clock_t clock);

#ifdef __cplusplus
}
#endif
#endif /* NEXUS_EXAMPLE_GATEWAY_NATIVE_FRAMES_H */
