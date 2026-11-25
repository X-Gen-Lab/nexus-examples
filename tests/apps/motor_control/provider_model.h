/**
 * \file            provider_model.h
 * \brief           Explicit development-only MMIO boundary for the F407 owner
 * \author          Nexus Team
 */
#ifndef EXAMPLE_MOTOR_PROVIDER_MODEL_H
#define EXAMPLE_MOTOR_PROVIDER_MODEL_H
extern "C" {
#include "drivers/pwm_adc/power_stage.h"
}
#include <array>

namespace example_test {
/** \brief Models MMIO and CPU leaves; algorithms/provider execute real C.
 * \note One test owns this model; never shipped in an SDK or MCU image.
 */
struct ProviderModel {
    RCC_TypeDef rcc = {};
    TIM_TypeDef timer = {}, encoder = {}, core = {};
    ADC_TypeDef adc = {}, adc2 = {}, adc3 = {};
    ADC_Common_TypeDef common = {};
    GPIO_TypeDef a = {}, b = {}, c = {}, d = {};
    std::array<uint32_t, 82> enabled = {}, pending = {}, active = {},
                             priority = {};
    nx_stm32_power_stage_t owner = {};
    nx_stm32_power_stage_config_t config = {};
    nx_power_stage_t port = {&nx_stm32_power_stage_ops, &owner};
    nx_stm32_av_pin_t pwm[7] = {}, analog[4] = {}, encoder_pins[2] = {};
    uint16_t snapshots[13] = {};
    uint8_t channels[4] = {0, 1, 2, 3}, times[4] = {};
    uint16_t phase_code[3] = {2048, 2048, 2048}, bus_code = 2400;
    nx_time_us_t now = 0;
    bool isr = false;
    volatile uint32_t* refused = nullptr;
    ProviderModel();
    void capture();
};
} /* namespace example_test */
#endif
