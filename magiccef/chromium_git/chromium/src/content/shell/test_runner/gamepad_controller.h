// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_TEST_RUNNER_GAMEPAD_CONTROLLER_H_
#define CONTENT_SHELL_TEST_RUNNER_GAMEPAD_CONTROLLER_H_

#include <bitset>
#include <memory>
#include <set>

#include "base/containers/unique_ptr_adapters.h"
#include "base/macros.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/memory/weak_ptr.h"
#include "content/shell/test_runner/test_runner_export.h"
#include "device/gamepad/public/cpp/gamepads.h"
#include "device/gamepad/public/mojom/gamepad.mojom.h"
#include "device/gamepad/public/mojom/gamepad_hardware_buffer.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/system/buffer.h"

namespace blink {
class WebLocalFrame;
}

namespace test_runner {

class TEST_RUNNER_EXPORT GamepadController
    : public base::SupportsWeakPtr<GamepadController> {
 public:
  GamepadController();
  ~GamepadController();

  void Reset();
  void Install(blink::WebLocalFrame* frame);

 private:
  class MonitorImpl : public device::mojom::GamepadMonitor {
   public:
    MonitorImpl(GamepadController* controller,
                device::mojom::GamepadMonitorRequest request);
    ~MonitorImpl() override;

    // Returns true if this monitor has a pending connection event for the
    // gamepad at |index|. The event will be dispatched once an observer is set.
    bool HasPendingConnect(int index);

    void Reset();
    void DispatchConnected(int index, const device::Gamepad& pad);
    void DispatchDisconnected(int index, const device::Gamepad& pad);

    // GamepadMonitor implementation.
    void GamepadStartPolling(GamepadStartPollingCallback callback) override;
    void GamepadStopPolling(GamepadStopPollingCallback callback) override;
    void SetObserver(device::mojom::GamepadObserverPtr observer) override;

   private:
    GamepadController* controller_;
    mojo::Binding<device::mojom::GamepadMonitor> binding_;
    device::mojom::GamepadObserverPtr observer_;
    std::bitset<device::Gamepads::kItemsLengthCap> missed_dispatches_;
  };

  friend class GamepadControllerBindings;

  // TODO(b.kelemen): for historical reasons Connect just initializes the
  // object. The 'gamepadconnected' event will be dispatched via
  // DispatchConnected. Tests for connected events need to first connect(),
  // then set the gamepad data and finally call dispatchConnected().
  // We should consider renaming Connect to Init and DispatchConnected to
  // Connect and at the same time updating all the gamepad tests.
  void Connect(int index);
  void DispatchConnected(int index);
  void Disconnect(int index);

  void SetId(int index, const std::string& src);
  void SetButtonCount(int index, int buttons);
  void SetButtonData(int index, int button, double data);
  void SetAxisCount(int index, int axes);
  void SetAxisData(int index, int axis, double data);
  void SetDualRumbleVibrationActuator(int index, bool enabled);

  void OnInterfaceRequest(mojo::ScopedMessagePipeHandle handle);

  base::ReadOnlySharedMemoryRegion GetSharedMemoryRegion() const;

  void OnConnectionError(MonitorImpl* monitor);

  // Notifies |monitor| for any gamepad connections that occurred before
  // SetObserver was called.
  void NotifyForMissedDispatches(MonitorImpl* monitor);

  std::set<std::unique_ptr<MonitorImpl>, base::UniquePtrComparator> monitors_;
  base::ReadOnlySharedMemoryRegion shared_memory_region_;
  base::WritableSharedMemoryMapping shared_memory_mapping_;

  device::GamepadHardwareBuffer* gamepads_;

  base::WeakPtrFactory<GamepadController> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(GamepadController);
};

}  // namespace test_runner

#endif  // CONTENT_SHELL_TEST_RUNNER_GAMEPAD_CONTROLLER_H_
