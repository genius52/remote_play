// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/impl/quic_port_utils_impl.h"

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"

namespace quic {

int QuicPickUnusedPortOrDieImpl() {
  return 12345 + (QuicRandom::GetInstance()->RandUint64() % 20000);
}

void QuicRecyclePortImpl(int port) {}

}  // namespace quic
