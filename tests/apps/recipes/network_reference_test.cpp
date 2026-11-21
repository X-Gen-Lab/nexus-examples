/**
 * \file            network_reference_test.cpp
 * \brief           Upgrade authorization ordering and exclusive MSC media
 * owners \author          Nexus Team \version         1.0.0 \date 2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "msc_handoff.h"
#include "update_reference.h"
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>
namespace {
struct Update {
    example_update_t update{};
    std::array<uint8_t, 256> media{};
    nx_time_us_t now{10};
    unsigned programmed{};
    unsigned authorized{};
    unsigned requested{};
    unsigned quiesced{};
    nx_result_t auth_result{NX_SUCCESS};
    nx_result_t quiesce_result{NX_SUCCESS};
    nx_flash_erase_span_t span{0, 256, 1};
    nx_flash_geometry_t geometry{0, 256, 4, &span, 1, 0};
    static const nx_flash_geometry_t* describe(const void* p) {
        return &static_cast<const Update*>(p)->geometry;
    }
    static nx_result_t read(const void* p, uint32_t offset, void* out,
                            size_t n) {
        std::memcpy(out, static_cast<const Update*>(p)->media.data() + offset,
                    n);
        return NX_SUCCESS;
    }
    static nx_result_t program(void* p, uint32_t offset, const void* in,
                               size_t n, nx_time_us_t deadline) {
        auto& self = *static_cast<Update*>(p);
        EXPECT_EQ(deadline, 100U);
        std::memcpy(self.media.data() + offset, in, n);
        ++self.programmed;
        return NX_SUCCESS;
    }
    static nx_time_us_t clock(void* p) {
        return static_cast<Update*>(p)->now;
    }
    static nx_result_t authorize(void* p, const nx_flash_region_t* region,
                                 uint32_t bytes, nx_time_us_t deadline) {
        auto& self = *static_cast<Update*>(p);
        ++self.authorized;
        EXPECT_EQ(bytes, 8U);
        EXPECT_EQ(deadline, 100U);
        uint8_t actual[8]{};
        EXPECT_EQ(nx_flash_region_read(region, 0, actual, sizeof(actual)),
                  NX_SUCCESS);
        EXPECT_EQ(actual[0], 42);
        return self.auth_result;
    }
    static nx_result_t request(void* p, nx_time_us_t deadline) {
        auto& self = *static_cast<Update*>(p);
        EXPECT_EQ(self.authorized, 1U);
        EXPECT_EQ(deadline, 100U);
        ++self.requested;
        return NX_SUCCESS;
    }
    static nx_result_t quiesce(void* p, nx_time_us_t deadline) {
        auto& self = *static_cast<Update*>(p);
        EXPECT_EQ(deadline, 100U);
        ++self.quiesced;
        return self.quiesce_result;
    }
    const nx_flash_ops_t ops{describe, read, program, nullptr, nullptr};
    const nx_flash_port_t flash{&ops, this};
    example_update_ports_t ports{{&flash, 0, 256, true},
                                 {clock, this},
                                 authorize,
                                 request,
                                 quiesce,
                                 this};
    void begin() {
        ASSERT_EQ(example_update_begin(&update, &ports, 8, 100), NX_SUCCESS);
    }
    void stage() {
        const uint8_t bytes[]{42, 1, 2, 3, 4, 5, 6, 7};
        ASSERT_EQ(example_update_write(&update, bytes, sizeof(bytes)),
                  NX_SUCCESS);
    }
};
TEST(NetworkRecipe,
     UpgradeCannotRequestBeforeAllBytesOrAfterAuthenticationFailure) {
    Update h;
    h.begin();
    EXPECT_EQ(example_update_request(&h.update), NX_ERROR_STATE);
    EXPECT_EQ(h.requested, 0U);
    h.stage();
    h.auth_result = NX_ERROR_PERMISSION;
    EXPECT_EQ(example_update_request(&h.update), NX_ERROR_PERMISSION);
    EXPECT_EQ(h.requested, 0U);
    EXPECT_EQ(h.update.state, EXAMPLE_UPDATE_FAILED);
}
TEST(NetworkRecipe, UpgradeDeadlineIsFixedAndResetWaitsForRealQuiescence) {
    Update h;
    h.begin();
    h.stage();
    ASSERT_EQ(example_update_request(&h.update), NX_SUCCESS);
    EXPECT_EQ(h.requested, 1U);
    h.quiesce_result = NX_ERROR_BUSY;
    EXPECT_EQ(example_update_prepare_reset(&h.update), NX_ERROR_BUSY);
    EXPECT_EQ(h.update.state, EXAMPLE_UPDATE_REQUESTED);
    h.quiesce_result = NX_SUCCESS;
    EXPECT_EQ(example_update_prepare_reset(&h.update), NX_SUCCESS);
    EXPECT_EQ(h.update.state, EXAMPLE_UPDATE_READY_TO_RESET);
}
TEST(NetworkRecipe, ExpiredUpgradeHasNoFurtherFlashOrAuthenticationEffect) {
    Update h;
    h.begin();
    h.now = 100;
    const uint8_t bytes[8]{};
    EXPECT_EQ(example_update_write(&h.update, bytes, sizeof(bytes)),
              NX_ERROR_TIMEOUT);
    EXPECT_EQ(h.programmed, 0U);
    EXPECT_EQ(h.authorized, 0U);
    EXPECT_EQ(h.requested, 0U);
}
struct Media {
    example_msc_handoff_t handoff{};
    std::vector<unsigned> calls;
    nx_result_t start_result{NX_SUCCESS};
    nx_result_t stop_result{NX_SUCCESS};
    static nx_result_t unmount(void* p, nx_time_us_t d) {
        EXPECT_EQ(d, 100U);
        static_cast<Media*>(p)->calls.push_back(1);
        return NX_SUCCESS;
    }
    static nx_result_t mount(void* p, nx_time_us_t d) {
        EXPECT_EQ(d, 100U);
        static_cast<Media*>(p)->calls.push_back(2);
        return NX_SUCCESS;
    }
    static nx_result_t start(void* p, nx_time_us_t d) {
        EXPECT_EQ(d, 100U);
        auto& self = *static_cast<Media*>(p);
        self.calls.push_back(3);
        return self.start_result;
    }
    static nx_result_t stop(void* p, nx_time_us_t d) {
        EXPECT_EQ(d, 100U);
        auto& self = *static_cast<Media*>(p);
        self.calls.push_back(4);
        return self.stop_result;
    }
    static nx_result_t sync(void* p, nx_time_us_t d) {
        EXPECT_EQ(d, 100U);
        static_cast<Media*>(p)->calls.push_back(5);
        return NX_SUCCESS;
    }
    void bind() {
        example_msc_ports_t ports{unmount, mount, start, stop, sync, this};
        ASSERT_EQ(example_msc_handoff_init(&handoff, &ports), NX_SUCCESS);
    }
};
TEST(NetworkRecipe, MscHandoffNeverMountsLocalBeforeUsbJoin) {
    Media h;
    h.bind();
    ASSERT_EQ(example_msc_request_usb(&h.handoff, 100), NX_SUCCESS);
    EXPECT_EQ(h.calls, (std::vector<unsigned>{1, 5, 3}));
    h.stop_result = NX_ERROR_TIMEOUT;
    EXPECT_EQ(example_msc_request_local(&h.handoff, 100), NX_ERROR_TIMEOUT);
    EXPECT_EQ(h.handoff.owner, EXAMPLE_MEDIA_USB);
    EXPECT_EQ(h.calls.back(), 4U);
    h.stop_result = NX_SUCCESS;
    EXPECT_EQ(example_msc_request_local(&h.handoff, 100), NX_SUCCESS);
    EXPECT_EQ(h.calls, (std::vector<unsigned>{1, 5, 3, 4, 4, 5, 2}));
}
TEST(NetworkRecipe, FailedUsbStartRetainsCleanupAndCannotBlindlyStartAgain) {
    Media h;
    h.bind();
    h.start_result = NX_ERROR_IO;
    EXPECT_EQ(example_msc_request_usb(&h.handoff, 100), NX_ERROR_IO);
    EXPECT_EQ(h.handoff.owner, EXAMPLE_MEDIA_USB_CLEANUP);
    EXPECT_EQ(example_msc_request_usb(&h.handoff, 100), NX_ERROR_STATE);
    EXPECT_EQ(example_msc_request_local(&h.handoff, 100), NX_SUCCESS);
    EXPECT_EQ(h.calls, (std::vector<unsigned>{1, 5, 3, 4, 5, 2}));
}
} /* namespace */
