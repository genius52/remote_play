// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_EVENTS_OZONE_EVDEV_KEYBOARD_UTIL_EVDEV_H_
#define UI_EVENTS_OZONE_EVDEV_KEYBOARD_UTIL_EVDEV_H_

#include "ui/events/ozone/evdev/events_ozone_evdev_export.h"

namespace ui {

int EVENTS_OZONE_EVDEV_EXPORT NativeCodeToEvdevCode(int native_code);
int EVENTS_OZONE_EVDEV_EXPORT EvdevCodeToNativeCode(int evdev_code);

}  // namespace ui

#endif  // UI_EVENTS_OZONE_EVDEV_KEYBOARD_UTIL_EVDEV_H_
