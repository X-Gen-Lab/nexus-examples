/**
 * \file            provider_model.cpp
 * \brief           MMIO-only F407 model for the actual application/provider
 * chain \author          Nexus Team \version         1.0.0 \date 2026-11-25
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "provider_model.h"
#include <initializer_list>
extern "C" {
nx_stm32_system_t g_nx_stm32_system;
}
namespace {
example_test::ProviderModel* model;
}
namespace example_test {
ProviderModel::ProviderModel() {
    model = this;
    g_nx_stm32_system = {};
    g_nx_stm32_system.rcc = &rcc;
    g_nx_stm32_system.timer = &core;
    g_nx_stm32_system.started = true;
    g_nx_stm32_system.core_hz = NEXUS_CORE_HZ;
    g_nx_stm32_system.time_epoch = 1;
    core.CR1 = TIM_CR1_CEN;
    core.DIER = TIM_DIER_UIE;
    core.PSC = NEXUS_APB1_TIMER_HZ / 1000000U - 1U;
    core.ARR = UINT32_MAX;
    rcc.PLLCFGR = NEXUS_CLOCK_PLLCFGR;
    rcc.CFGR = NEXUS_CLOCK_CFGR | RCC_CFGR_SW_PLL | RCC_CFGR_SWS_PLL;
    rcc.CR = RCC_CR_HSEON | RCC_CR_HSERDY | RCC_CR_PLLON | RCC_CR_PLLRDY;
    rcc.APB1ENR = RCC_APB1ENR_TIM2EN;
    rcc.AHB1ENR = RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN |
                  RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIODEN;
    pwm[0] = {&c, &snapshots[0], 2, 6, 3};
    pwm[1] = {&a, &snapshots[1], 0, 7, 3};
    pwm[2] = {&c, &snapshots[2], 2, 7, 3};
    pwm[3] = {&b, &snapshots[3], 1, 0, 3};
    pwm[4] = {&c, &snapshots[4], 2, 8, 3};
    pwm[5] = {&b, &snapshots[5], 1, 1, 3};
    pwm[6] = {&a, &snapshots[6], 0, 6, 3};
    for (uint8_t i = 0; i < 4; ++i) {
        analog[i] = {&a, &snapshots[7 + i], 0, i, 0};
    }
    encoder_pins[0] = {&d, &snapshots[11], 3, 12, 2};
    encoder_pins[1] = {&d, &snapshots[12], 3, 13, 2};
    config.pwm.common = {pwm, 7, nullptr, 0, 8, 5};
    config.pwm.registers = &timer;
    config.pwm.break_input = &a.IDR;
    config.pwm.break_mask = 1U << 6;
    config.pwm.timer_hz = NEXUS_APB2_TIMER_HZ;
    config.pwm.tick_hz = 1000000;
    config.pwm.period_ticks = 1000;
    config.pwm.deadtime_ticks = 84;
    config.pwm.irq = TIM8_BRK_TIM12_IRQn;
    config.pwm.kind = NX_STM32_WAVE_GROUP;
    config.pwm.controller = 8;
    config.pwm.channels = 7;
    config.pwm.master_mode = 7;
    config.pwm.complementary = true;
    config.pwm.internal_trigger = true;
    config.pwm.break_active_high = true;
    config.analog = {&adc, {&adc2, &adc3}, &common, analog, channels, times,
                     4,    NEXUS_ADC_HZ,   3300};
    config.encoder = &encoder;
    config.encoder_pins = encoder_pins;
    config.trigger_ticks = 800;
    config.minimum_duty_ticks = 50;
    config.maximum_duty_ticks = 700;
    config.settling_ticks = 25;
    config.commit_before_ticks = 990;
    config.compute_budget_us = 100;
    config.max_irq_delay_us = 20;
    config.current_count = 3;
    config.current_phase_mask = 7;
    config.sample_bus = true;
    config.trigger_selector = 14U << ADC_CR2_JEXTSEL_Pos;
}

void ProviderModel::capture() {
    now = owner.cycle_anchor + owner.staged_trigger_ticks + owner.scan_span_us;
    timer.CNT = owner.staged_trigger_ticks + owner.scan_span_us;
    /* Every later capture follows a real modeled carrier wrap. The provider
     * requires this UEV evidence and clears it before shadow submission. */
    if (!owner.first_cycle) {
        timer.SR |= TIM_SR_UIF;
    }
    adc.SR |= ADC_SR_JEOC;
    volatile uint32_t* results[4] = {&adc.JDR1, &adc.JDR2, &adc.JDR3,
                                     &adc.JDR4};
    const uint32_t ranks = ((adc.JSQR & ADC_JSQR_JL) >> ADC_JSQR_JL_Pos) + 1U;
    for (uint32_t rank = 0; rank < ranks; ++rank) {
        const uint32_t channel = (adc.JSQR >> ((4U - ranks + rank) * 5U)) & 31U;
        *results[rank] = channel < 3U ? phase_code[channel] : bus_code;
    }
    isr = true;
    nx_stm32_power_stage_adc_irq(&owner);
    isr = false;
}
} /* namespace example_test */

extern "C" uint32_t nx_av_model_read(volatile uint32_t* reg) {
    return *reg;
}
extern "C" void nx_av_model_write(volatile uint32_t* reg, uint32_t value) {
    if (reg == model->refused) {
        return;
    }
    if (reg == &model->timer.SR || reg == &model->encoder.SR ||
        reg == &model->adc.SR || reg == &model->adc2.SR ||
        reg == &model->adc3.SR) {
        *reg &= value;
    } else {
        *reg = value;
    }
    if (reg == &model->timer.EGR && (value & TIM_EGR_UG) != 0U) {
        model->timer.SR |= TIM_SR_UIF;
    }
    if (reg == &model->encoder.EGR && (value & TIM_EGR_UG) != 0U) {
        model->encoder.SR |= TIM_SR_UIF;
    }
    for (auto* gpio : {&model->a, &model->b, &model->c, &model->d}) {
        if (reg == &gpio->BSRR) {
            gpio->ODR = (gpio->ODR | (value & 0xffffU)) & ~(value >> 16U);
        }
    }
}
extern "C" void nx_av_model_poll(void) {
}
extern "C" nx_time_us_t nx_av_model_now(void) {
    return model->now;
}
extern "C" bool nx_av_model_privileged(void) {
    return true;
}
extern "C" bool nx_av_model_in_isr(void) {
    return model->isr;
}
extern "C" bool nx_av_model_masked(void) {
    return false;
}
extern "C" uint32_t nx_av_model_irq_active(IRQn_Type irq) {
    return model->active[irq];
}
extern "C" uint32_t nx_av_model_irq_pending(IRQn_Type irq) {
    return model->pending[irq];
}
extern "C" uint32_t nx_av_model_irq_enabled(IRQn_Type irq) {
    return model->enabled[irq];
}
extern "C" void nx_av_model_irq_enable(IRQn_Type irq) {
    model->enabled[irq] = 1;
}
extern "C" void nx_av_model_irq_disable(IRQn_Type irq) {
    model->enabled[irq] = 0;
}
extern "C" void nx_av_model_irq_clear(IRQn_Type irq) {
    model->pending[irq] = 0;
}
extern "C" void nx_av_model_irq_priority(IRQn_Type irq, uint32_t priority) {
    model->priority[irq] = priority;
}
extern "C" uint32_t nx_av_model_irq_priority_read(IRQn_Type irq) {
    return model->priority[irq];
}
