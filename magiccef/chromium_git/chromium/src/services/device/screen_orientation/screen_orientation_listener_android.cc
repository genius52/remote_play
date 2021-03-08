// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/device/screen_orientation/screen_orientation_listener_android.h"

#include "base/android/jni_android.h"
#include "base/message_loop/message_loop.h"
#include "jni/ScreenOrientationListener_jni.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace device {

// static
void ScreenOrientationListenerAndroid::Create(
    mojom::ScreenOrientationListenerRequest request) {
  mojo::MakeStrongBinding(
      base::WrapUnique(new ScreenOrientationListenerAndroid()),
      std::move(request));
}

ScreenOrientationListenerAndroid::ScreenOrientationListenerAndroid() = default;

ScreenOrientationListenerAndroid::~ScreenOrientationListenerAndroid() {
  DCHECK(base::MessageLoopCurrentForIO::IsSet());
}

void ScreenOrientationListenerAndroid::IsAutoRotateEnabledByUser(
    IsAutoRotateEnabledByUserCallback callback) {
  std::move(callback).Run(
      Java_ScreenOrientationListener_isAutoRotateEnabledByUser(
          base::android::AttachCurrentThread()));
}

}  // namespace device
