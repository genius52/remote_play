// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/device/usb/usb_configuration_android.h"

#include "jni/ChromeUsbConfiguration_jni.h"
#include "services/device/usb/usb_interface_android.h"

using base::android::ScopedJavaLocalRef;

namespace device {

// static
UsbConfigDescriptor UsbConfigurationAndroid::Convert(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& usb_configuration) {
  ScopedJavaLocalRef<jobject> wrapper =
      Java_ChromeUsbConfiguration_create(env, usb_configuration);

  UsbConfigDescriptor config(
      Java_ChromeUsbConfiguration_getConfigurationValue(env, wrapper),
      Java_ChromeUsbConfiguration_isSelfPowered(env, wrapper),
      Java_ChromeUsbConfiguration_isRemoteWakeup(env, wrapper),
      Java_ChromeUsbConfiguration_getMaxPower(env, wrapper));

  base::android::JavaObjectArrayReader<jobject> interfaces(
      Java_ChromeUsbConfiguration_getInterfaces(env, wrapper));
  config.interfaces.reserve(interfaces.size());
  for (auto interface : interfaces) {
    config.interfaces.push_back(UsbInterfaceAndroid::Convert(env, interface));
  }

  return config;
}

}  // namespace device
