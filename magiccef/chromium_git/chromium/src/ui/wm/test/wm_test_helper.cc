// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/wm/test/wm_test_helper.h"

#include <memory>
#include <utility>

#include "ui/aura/client/default_capture_client.h"
#include "ui/aura/test/test_focus_client.h"
#include "ui/aura/test/test_screen.h"
#include "ui/aura/window.h"
#include "ui/platform_window/platform_window_init_properties.h"
#include "ui/wm/core/compound_event_filter.h"
#include "ui/wm/core/default_activation_client.h"
#include "ui/wm/core/wm_state.h"

namespace wm {

WMTestHelper::WMTestHelper(const gfx::Size& default_window_size) {
  wm_state_ = std::make_unique<WMState>();

  // Install a screen, like TestWindowService's AuraTestHelper for InitMusHost.
  test_screen_ = base::WrapUnique(aura::TestScreen::Create(gfx::Size()));
  display::Screen::SetScreenInstance(test_screen_.get());

  host_ = aura::WindowTreeHost::Create(
      ui::PlatformWindowInitProperties{gfx::Rect(default_window_size)});
  host_->InitHost();

  aura::client::SetWindowParentingClient(host_->window(), this);

  focus_client_.reset(new aura::test::TestFocusClient);
  aura::client::SetFocusClient(host_->window(), focus_client_.get());

  root_window_event_filter_.reset(new wm::CompoundEventFilter);
  host_->window()->AddPreTargetHandler(root_window_event_filter_.get());

  new wm::DefaultActivationClient(host_->window());

  capture_client_.reset(
      new aura::client::DefaultCaptureClient(host_->window()));
}

WMTestHelper::~WMTestHelper() {
  host_->window()->RemovePreTargetHandler(root_window_event_filter_.get());

  if (display::Screen::GetScreen() == test_screen_.get())
    display::Screen::SetScreenInstance(nullptr);
}

aura::Window* WMTestHelper::GetDefaultParent(aura::Window* window,
                                             const gfx::Rect& bounds) {
  return host_->window();
}

}  // namespace wm
