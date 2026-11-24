/**
 * \file            as5047p_wire.h
 * \brief           Shared SPI-only AS5047P protocol leaf for actual consumers
 * \author          Nexus Team
 */
#ifndef EXAMPLE_TEST_AS5047P_WIRE_H
#define EXAMPLE_TEST_AS5047P_WIRE_H
#include "as5047p_rotor.h"

namespace example_test {
struct As5047pWire {
    nx_time_us_t now = 10U;
    uint16_t pending = 0U;
    uint16_t angle = 1000U;
    unsigned transfers = 0U;
    bool corrupt = false;
    static uint16_t parity(uint16_t word) {
        uint16_t bits = word;
        bits ^= static_cast<uint16_t>(bits >> 8U);
        bits ^= static_cast<uint16_t>(bits >> 4U);
        bits ^= static_cast<uint16_t>(bits >> 2U);
        bits ^= static_cast<uint16_t>(bits >> 1U);
        return static_cast<uint16_t>(word | ((bits & 1U) << 15U));
    }
    static nx_time_us_t time(void* context) {
        return static_cast<As5047pWire*>(context)->now;
    }
    static nx_result_t transfer(void* context, const uint8_t* tx, uint8_t* rx,
                                size_t length, nx_time_us_t,
                                size_t* transferred) {
        auto& wire = *static_cast<As5047pWire*>(context);
        if (length != 2U) {
            return NX_ERROR_INVALID;
        }
        ++wire.transfers;
        uint16_t response = wire.pending;
        if (wire.corrupt) {
            response ^= 0x8000U;
        }
        rx[0] = static_cast<uint8_t>(response >> 8U);
        rx[1] = static_cast<uint8_t>(response);
        const uint16_t reg = static_cast<uint16_t>(
            ((static_cast<uint16_t>(tx[0]) << 8U) | tx[1]) & 0x3fffU);
        wire.pending = parity(reg == 0x0018U   ? 1U
                              : reg == 0x3ffcU ? 0x0100U
                              : reg == 0x3ffeU ? wire.angle
                                               : 0U);
        *transferred = length;
        return NX_SUCCESS;
    }
    static const nx_spi_endpoint_ops_t ops;
    nx_spi_endpoint_t endpoint = {&ops, this};
};
inline const nx_spi_endpoint_ops_t As5047pWire::ops = {
    transfer, nullptr, nullptr, nullptr, nullptr};
} /* namespace example_test */
#endif /* EXAMPLE_TEST_AS5047P_WIRE_H */
