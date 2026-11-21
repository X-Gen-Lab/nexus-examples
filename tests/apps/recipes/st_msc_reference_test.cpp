/**
 * \file            st_msc_reference_test.cpp
 * \brief           Real official MSC profile and exclusive copied block bridge
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-21
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "st_msc_reference.h"
#include <array>
#include <cstring>
#include <gtest/gtest.h>
namespace {
struct MscReference : testing::Test {
    std::array<uint8_t, 2048> bytes{};
    unsigned reads{};
    unsigned writes{};
    unsigned syncs{};
    bool short_read{};
    nx_block_geometry_t geometry{4, 512, 32, true};
    static const nx_block_geometry_t* describe(const void* p) {
        return &static_cast<const MscReference*>(p)->geometry;
    }
    static nx_block_state_t state(const void*) {
        return NX_BLOCK_READY;
    }
    static nx_time_us_t clock(void*) {
        return 10;
    }
    static nx_block_io_result_t read(void* p, uint64_t first, void* out,
                                     size_t n, uint32_t count,
                                     nx_time_us_t deadline) {
        auto& self = *static_cast<MscReference*>(p);
        ++self.reads;
        EXPECT_EQ(reinterpret_cast<uintptr_t>(out) % 32, 0U);
        EXPECT_EQ(deadline, 110U);
        EXPECT_EQ(count, 1U);
        EXPECT_EQ(n, 512U);
        std::memcpy(out, self.bytes.data() + first * 512, n);
        return {NX_SUCCESS, self.short_read ? 0U : count};
    }
    static nx_block_io_result_t write(void* p, uint64_t first, const void* in,
                                      size_t n, uint32_t count,
                                      nx_time_us_t deadline) {
        auto& self = *static_cast<MscReference*>(p);
        ++self.writes;
        EXPECT_EQ(reinterpret_cast<uintptr_t>(in) % 32, 0U);
        EXPECT_EQ(deadline, 110U);
        std::memcpy(self.bytes.data() + first * 512, in, n);
        return {NX_SUCCESS, count};
    }
    static nx_result_t sync(void* p, nx_time_us_t deadline) {
        EXPECT_EQ(deadline, 110U);
        ++static_cast<MscReference*>(p)->syncs;
        return NX_SUCCESS;
    }
    const nx_block_ops_t ops{describe, state, nullptr, read,
                             write,    sync,  nullptr, nullptr};
    const nx_block_port_t port{&ops, this};
    void SetUp() override {
        ASSERT_EQ(example_st_msc_bind(&port, {clock, nullptr}, 100),
                  NX_SUCCESS);
        ASSERT_EQ(example_st_msc_activate(), NX_SUCCESS);
    }
    void TearDown() override {
        EXPECT_EQ(example_st_msc_unbind(), NX_SUCCESS);
    }
};
TEST_F(MscReference, SingleLunUsesAlignedCopyAndWriteThroughBeforeSuccess) {
    uint32_t blocks = 0;
    uint16_t block_size = 0;
    ASSERT_EQ(example_st_msc_storage.GetCapacity(0, &blocks, &block_size), 0);
    EXPECT_EQ(blocks, 4U);
    EXPECT_EQ(block_size, 512U);
    EXPECT_EQ(example_st_msc_storage.GetMaxLun(), 0);
    uint8_t data[512]{};
    data[0] = 42;
    ASSERT_EQ(example_st_msc_storage.Write(0, data, 1, 1), 0);
    EXPECT_EQ(writes, 1U);
    EXPECT_EQ(syncs, 1U);
    data[0] = 0;
    ASSERT_EQ(example_st_msc_storage.Read(0, data, 1, 1), 0);
    EXPECT_EQ(data[0], 42);
    EXPECT_EQ(reads, 1U);
    EXPECT_EQ(example_st_msc_storage.Read(1, data, 1, 1), -1);
}
TEST_F(MscReference, PartialBlockFailureClosesFurtherIoWithoutSilentRetry) {
    short_read = true;
    uint8_t data[512]{};
    EXPECT_EQ(example_st_msc_storage.Read(0, data, 0, 1), -1);
    EXPECT_EQ(example_st_msc_fault(), NX_ERROR_IO);
    EXPECT_EQ(example_st_msc_storage.Read(0, data, 0, 1), -1);
    EXPECT_EQ(reads, 1U);
}
TEST_F(MscReference, OfficialMscRecordsAndExactStaticClassAllocationAreReal) {
    USBD_HandleTypeDef device{};
    EXPECT_EQ(USBD_MSC_RegisterStorage(&device, &example_st_msc_storage),
              USBD_OK);
    EXPECT_EQ(device.pUserData[0], &example_st_msc_storage);
    uint16_t size = 0;
    auto* configuration = USBD_MSC.GetFSConfigDescriptor(&size);
    ASSERT_EQ(size, 32U);
    EXPECT_EQ(configuration[14], 8);
    EXPECT_EQ(configuration[15], 6);
    EXPECT_EQ(configuration[16], 0x50);
    auto* allocation =
        example_st_msc_allocate(sizeof(USBD_MSC_BOT_HandleTypeDef));
    ASSERT_NE(allocation, nullptr);
    EXPECT_EQ(example_st_msc_allocate(sizeof(USBD_MSC_BOT_HandleTypeDef)),
              nullptr);
    EXPECT_EQ(example_st_msc_unbind(), NX_ERROR_BUSY);
    example_st_msc_free(allocation);
    auto* descriptor =
        example_st_msc_descriptors.GetDeviceDescriptor(USBD_SPEED_FULL, &size);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(size, 18U);
}
} /* namespace */
