/**
 * \file            shared_spi_application.h
 * \brief           Product-owned two-device SPI task composition
 * \author          Nexus Team
 */
#ifndef EXAMPLE_SHARED_SPI_APPLICATION_H
#define EXAMPLE_SHARED_SPI_APPLICATION_H
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/** \brief Observed finite application outcomes, not physical qualification. */
typedef struct {
    size_t completed;
    size_t cancelled;
    size_t rejected;
    bool joined;
    bool stopped;
    bool platform_retained;
} example_spi_report_t;
/**
 * \brief Run two producer rounds with one explicit SPI execution owner.
 * \param[out] report: Caller storage copied after completion; never retained.
 * \return Zero after settlement, joins and platform stop; nonzero on failure.
 * \note Single startup caller. Static application storage retains all borrows.
 *       Native joins real tasks; the FreeRTOS variant owns scheduler startup.
 *       MCU scheduler shutdown is unsupported and does not return normally.
 */
int example_shared_spi_run(example_spi_report_t* report);
#ifdef __cplusplus
}
#endif
#endif
