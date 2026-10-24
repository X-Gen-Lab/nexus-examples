/**
 * \file            stream_application.h
 * \brief           Explicit slow-consumer stream lifetime example
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-24
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#ifndef EXAMPLE_STREAM_APPLICATION_H
#define EXAMPLE_STREAM_APPLICATION_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Copied outcomes; never a reference to borrowed payload storage. */
typedef struct {
    uint32_t lost_blocks;
    uint32_t diagnostic_findings;
    uint32_t loss_diagnostic_findings;
    bool borrow_preserved;
    bool stop_waited;
    bool restarted;
    bool stopped;
} example_stream_report_t;
/**
 * \brief           Execute the deterministic Native block-receive composition.
 * \param[out]      report: Non-null caller output, copied before return.
 * \return          Zero on verified lifetime/recovery behavior.
 * \note            Task/main context with exclusive platform ownership. Static
 *                  payloads survive failure. Native model injection establishes
 *                  ownership behavior, not DMA timing or physical
 * qualification.
 */
int example_stream_run(example_stream_report_t* report);
#ifdef __cplusplus
}
#endif
#endif
