/*
 * Copyright 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hci/le_scanning_reassembler.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hci/enum_helper.h"

using ::testing::_;
using ::testing::Eq;

using namespace bluetooth;
using namespace std::chrono_literals;

namespace bluetooth::hci {

static constexpr ExtendedAdvertisingEventType kCompleteEventType{0};

// Defaults for other fields.
static constexpr uint8_t kSidNotPresent = 0xff;

// Test addresses.
static const Address kTestAddress = Address({0, 1, 2, 3, 4, 5});

class LeScanningReassemblerTest : public ::testing::Test {
 public:
  LeScanningReassembler reassembler_;
};

TEST_F(LeScanningReassemblerTest, trim_advertising_data) {
  // TrimAdvertisingData should filter out empty entries.
  ASSERT_EQ(
      LeScanningReassembler::TrimAdvertisingData({0x1, 0x2, 0x0, 0x0, 0x3, 0x4, 0x5, 0x6}),
      std::vector<uint8_t>({0x1, 0x2, 0x3, 0x4, 0x5, 0x6}));

  // TrimAdvertisingData should remove trailing zeros.
  ASSERT_EQ(
      LeScanningReassembler::TrimAdvertisingData({0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x0, 0x0}),
      std::vector<uint8_t>({0x1, 0x2, 0x3, 0x4, 0x5, 0x6}));

  // TrimAdvertisingData should remove overflowing entries.
  ASSERT_EQ(
      LeScanningReassembler::TrimAdvertisingData({0x1, 0x2, 0x3, 0x4, 0x5}),
      std::vector<uint8_t>({0x1, 0x2}));
}

TEST_F(LeScanningReassemblerTest, non_scannable_legacy_advertising) {
  // Test non scannable legacy advertising.
  ASSERT_EQ(
      reassembler_
          .ProcessAdvertisingReport(
              ExtendedAdvertisingEventType::LEGACY,
              (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
              kTestAddress,
              kSidNotPresent,
              {0x1, 0x2})
          .value()
          .data,
      std::vector<uint8_t>({0x1, 0x2}));
}

TEST_F(LeScanningReassemblerTest, scannable_non_connectable_legacy_advertising) {
  // Test scannable legacy advertising with well formed advertising and
  // scan response payload.
  ASSERT_FALSE(
      reassembler_
          .ProcessAdvertisingReport(
              ExtendedAdvertisingEventType::LEGACY | ExtendedAdvertisingEventType::SCANNABLE,
              (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
              kTestAddress,
              kSidNotPresent,
              {0x1, 0x2})
          .has_value());

  ExtendedAdvertisingEventType scan_response_event_type =
      ExtendedAdvertisingEventType::LEGACY | ExtendedAdvertisingEventType::SCANNABLE |
      ExtendedAdvertisingEventType::SCAN_RESPONSE;
  auto processed_report = reassembler_.ProcessAdvertisingReport(
      scan_response_event_type,
      (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
      kTestAddress,
      kSidNotPresent,
      {0x3, 0x4, 0x5, 0x6});
  ASSERT_TRUE(processed_report.has_value());
  ASSERT_EQ(processed_report.value().extended_event_type, scan_response_event_type);
  ASSERT_EQ(processed_report.value().data, std::vector<uint8_t>({0x1, 0x2, 0x3, 0x4, 0x5, 0x6}));

  // Test scannable legacy advertising with padding after the
  // advertising and scan response data.
  ASSERT_FALSE(
      reassembler_
          .ProcessAdvertisingReport(
              ExtendedAdvertisingEventType::LEGACY | ExtendedAdvertisingEventType::SCANNABLE,
              (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
              kTestAddress,
              kSidNotPresent,
              {0x1, 0x2, 0x0, 0x0})
          .has_value());

  ASSERT_EQ(
      reassembler_
          .ProcessAdvertisingReport(
              scan_response_event_type,
              (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
              kTestAddress,
              kSidNotPresent,
              {0x3, 0x4, 0x5, 0x6, 0x0, 0x0})
          .value()
          .data,
      std::vector<uint8_t>({0x1, 0x2, 0x3, 0x4, 0x5, 0x6}));
}

TEST_F(LeScanningReassemblerTest, scannable_connectable_legacy_advertising) {
  ASSERT_FALSE(reassembler_
                   .ProcessAdvertisingReport(
                       ExtendedAdvertisingEventType::LEGACY |
                           ExtendedAdvertisingEventType::SCANNABLE |
                           ExtendedAdvertisingEventType::CONNECTABLE,
                       (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
                       kTestAddress,
                       kSidNotPresent,
                       {0x1, 0x2})
                   .has_value());

  ExtendedAdvertisingEventType scan_response_event_type =
      ExtendedAdvertisingEventType::LEGACY | ExtendedAdvertisingEventType::SCANNABLE |
      ExtendedAdvertisingEventType::SCAN_RESPONSE;
  auto processed_report = reassembler_.ProcessAdvertisingReport(
      scan_response_event_type,
      (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
      kTestAddress,
      kSidNotPresent,
      {0x3, 0x4, 0x5, 0x6});
  ASSERT_TRUE(processed_report.has_value());
  ASSERT_EQ(
      processed_report.value().extended_event_type,
      scan_response_event_type | ExtendedAdvertisingEventType::CONNECTABLE);
  ASSERT_EQ(processed_report.value().data, std::vector<uint8_t>({0x1, 0x2, 0x3, 0x4, 0x5, 0x6}));
}

TEST_F(LeScanningReassemblerTest, non_scannable_extended_advertising) {
  // Test fragmented non scannable extended advertising.
  // The split may occur in the middle of a GAP entry.
  ASSERT_FALSE(reassembler_
                   .ProcessAdvertisingReport(
                       ExtendedAdvertisingEventType::CONTINUING,
                       (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
                       kTestAddress,
                       kSidNotPresent,
                       {0x1, 0x2, 0x3})
                   .has_value());

  auto processed_report = reassembler_.ProcessAdvertisingReport(
      kCompleteEventType,
      (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
      kTestAddress,
      kSidNotPresent,
      {0x4, 0x5, 0x6});
  ASSERT_TRUE(processed_report.has_value());
  ASSERT_EQ(processed_report.value().extended_event_type, kCompleteEventType);
  ASSERT_EQ(processed_report.value().data, std::vector<uint8_t>({0x1, 0x2, 0x3, 0x4, 0x5, 0x6}));

  // Test fragmented and truncated non scannable extended advertising.
  // The split may occur in the middle of a GAP entry.
  ASSERT_FALSE(reassembler_
                   .ProcessAdvertisingReport(
                       ExtendedAdvertisingEventType::CONTINUING,
                       (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
                       kTestAddress,
                       kSidNotPresent,
                       {0x1, 0x2, 0x3})
                   .has_value());

  ASSERT_EQ(
      reassembler_
          .ProcessAdvertisingReport(
              ExtendedAdvertisingEventType::TRUNCATED,
              (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
              kTestAddress,
              kSidNotPresent,
              {0x4, 0x5, 0x6, 0x7})
          .value()
          .data,
      std::vector<uint8_t>({0x1, 0x2, 0x3, 0x4, 0x5, 0x6}));

  // Test fragmented and truncated anonymous, non scannable
  // extended advertising. The split may occur in the middle of a GAP entry.
  ASSERT_FALSE(reassembler_
                   .ProcessAdvertisingReport(
                       ExtendedAdvertisingEventType::CONTINUING,
                       (uint8_t)DirectAdvertisingAddressType::NO_ADDRESS_PROVIDED,
                       Address::kEmpty,
                       kSidNotPresent,
                       {0x1, 0x2, 0x3})
                   .has_value());

  ASSERT_EQ(
      reassembler_
          .ProcessAdvertisingReport(
              ExtendedAdvertisingEventType::TRUNCATED,
              (uint8_t)DirectAdvertisingAddressType::NO_ADDRESS_PROVIDED,
              Address::kEmpty,
              kSidNotPresent,
              {0x4, 0x5, 0x6, 0x7})
          .value()
          .data,
      std::vector<uint8_t>({0x1, 0x2, 0x3, 0x4, 0x5, 0x6}));
}

TEST_F(LeScanningReassemblerTest, scannable_extended_advertising) {
  // Test fragmented scannable extended advertising.
  // The split may occur in the middle of a GAP entry.
  // Padding may occur at the end of the advertising data.
  ASSERT_FALSE(
      reassembler_
          .ProcessAdvertisingReport(
              ExtendedAdvertisingEventType::SCANNABLE | ExtendedAdvertisingEventType::CONTINUING,
              (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
              kTestAddress,
              kSidNotPresent,
              {0x1, 0x2, 0x3})
          .has_value());

  ASSERT_FALSE(reassembler_
                   .ProcessAdvertisingReport(
                       ExtendedAdvertisingEventType::SCANNABLE,
                       (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
                       kTestAddress,
                       kSidNotPresent,
                       {0x4, 0x5, 0x6, 0x0, 0x0})
                   .has_value());

  ASSERT_FALSE(reassembler_
                   .ProcessAdvertisingReport(
                       ExtendedAdvertisingEventType::CONTINUING,
                       (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
                       kTestAddress,
                       kSidNotPresent,
                       {0x7, 0x8, 0x9, 0xa})
                   .has_value());

  ASSERT_EQ(
      reassembler_
          .ProcessAdvertisingReport(
              ExtendedAdvertisingEventType::TRUNCATED,
              (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
              kTestAddress,
              kSidNotPresent,
              {0xb, 0xc, 0xd, 0xe, 0x0})
          .value()
          .data,
      std::vector<uint8_t>({0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe}));
}

TEST_F(LeScanningReassemblerTest, ignore_scan_responses) {
  // Scan response without advertising data are ignored.
  ASSERT_FALSE(
      reassembler_
          .ProcessAdvertisingReport(
              ExtendedAdvertisingEventType::SCANNABLE | ExtendedAdvertisingEventType::SCAN_RESPONSE,
              (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
              kTestAddress,
              kSidNotPresent,
              {0x1, 0x2})
          .has_value());

  ASSERT_EQ(
      reassembler_
          .ProcessAdvertisingReport(
              kCompleteEventType,
              (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
              kTestAddress,
              kSidNotPresent,
              {0x1, 0x2})
          .value()
          .data,
      std::vector<uint8_t>({0x1, 0x2}));

  // The option ignore_scan_responses forces scan responses to be dropped.
  reassembler_.SetIgnoreScanResponses(true);
  ASSERT_EQ(
      reassembler_
          .ProcessAdvertisingReport(
              ExtendedAdvertisingEventType::SCANNABLE,
              (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
              kTestAddress,
              kSidNotPresent,
              {0x1, 0x2})
          .value()
          .data,
      std::vector<uint8_t>({0x1, 0x2}));
}

TEST_F(LeScanningReassemblerTest, interleaved_advertising) {
  // The reassembler must disambiguate advertising events by address,
  // address type, and SID.
  ASSERT_FALSE(reassembler_
                   .ProcessAdvertisingReport(
                       ExtendedAdvertisingEventType::CONTINUING,
                       (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
                       kTestAddress,
                       kSidNotPresent,
                       {0x2, 0x0})
                   .has_value());

  ASSERT_FALSE(reassembler_
                   .ProcessAdvertisingReport(
                       ExtendedAdvertisingEventType::CONTINUING,
                       (uint8_t)AddressType::RANDOM_DEVICE_ADDRESS,
                       kTestAddress,
                       kSidNotPresent,
                       {0x2, 0x1})
                   .has_value());

  ASSERT_FALSE(reassembler_
                   .ProcessAdvertisingReport(
                       ExtendedAdvertisingEventType::CONTINUING,
                       (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
                       kTestAddress,
                       0x1,
                       {0x2, 0x2})
                   .has_value());

  ASSERT_FALSE(reassembler_
                   .ProcessAdvertisingReport(
                       ExtendedAdvertisingEventType::CONTINUING,
                       (uint8_t)DirectAdvertisingAddressType::NO_ADDRESS_PROVIDED,
                       Address::kEmpty,
                       0x1,
                       {0x2, 0x3})
                   .has_value());

  ASSERT_EQ(
      reassembler_
          .ProcessAdvertisingReport(
              kCompleteEventType,
              (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
              kTestAddress,
              kSidNotPresent,
              {0x0})
          .value()
          .data,
      std::vector<uint8_t>({0x2, 0x0, 0x0}));

  ASSERT_EQ(
      reassembler_
          .ProcessAdvertisingReport(
              kCompleteEventType,
              (uint8_t)AddressType::RANDOM_DEVICE_ADDRESS,
              kTestAddress,
              kSidNotPresent,
              {0x1})
          .value()
          .data,
      std::vector<uint8_t>({0x2, 0x1, 0x1}));

  ASSERT_EQ(
      reassembler_
          .ProcessAdvertisingReport(
              kCompleteEventType,
              (uint8_t)AddressType::PUBLIC_DEVICE_ADDRESS,
              kTestAddress,
              0x1,
              {0x2})
          .value()
          .data,
      std::vector<uint8_t>({0x2, 0x2, 0x2}));

  ASSERT_EQ(
      reassembler_
          .ProcessAdvertisingReport(
              kCompleteEventType,
              (uint8_t)DirectAdvertisingAddressType::NO_ADDRESS_PROVIDED,
              Address::kEmpty,
              0x1,
              {0x3})
          .value()
          .data,
      std::vector<uint8_t>({0x2, 0x3, 0x3}));
}

}  // namespace bluetooth::hci
