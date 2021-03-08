// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/device/geolocation/fake_position_cache.h"

#include <algorithm>

#include "services/device/geolocation/wifi_data.h"
#include "services/device/public/cpp/geolocation/geoposition.h"

namespace device {
namespace {

template <typename Set>
bool SetsEqual(const Set& lhs, const Set& rhs) {
  // Since sets order elements via an operator, std::equal doesn't work. It
  // would require elements to be equal-comparable. Check if symmetric
  // difference is empty instead.
  std::vector<typename Set::value_type> symmetric_difference;
  std::set_symmetric_difference(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                std::back_inserter(symmetric_difference),
                                typename Set::value_compare());
  return symmetric_difference.empty();
}

}  // namespace

FakePositionCache::FakePositionCache() = default;
FakePositionCache::~FakePositionCache() = default;

void FakePositionCache::CachePosition(const WifiData& wifi_data,
                                      const mojom::Geoposition& position) {
  data.push_back(std::make_pair(wifi_data, position));
}

const mojom::Geoposition* FakePositionCache::FindPosition(
    const WifiData& wifi_data) const {
  auto it = std::find_if(
      data.begin(), data.end(), [&wifi_data](const auto& candidate_pair) {
        return SetsEqual(wifi_data.access_point_data,
                         candidate_pair.first.access_point_data);
      });
  return it == data.end() ? nullptr : &(it->second);
}

size_t FakePositionCache::GetPositionCacheSize() const {
  return data.size();
}

const mojom::Geoposition& FakePositionCache::GetLastUsedNetworkPosition()
    const {
  return last_used_position;
}

void FakePositionCache::SetLastUsedNetworkPosition(
    const mojom::Geoposition& position) {
  last_used_position = position;
}

}  // namespace device
