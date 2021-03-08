// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/display/manager/display_change_observer.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/stl_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/user_activity/user_activity_detector.h"
#include "ui/display/display.h"
#include "ui/display/display_features.h"
#include "ui/display/display_layout.h"
#include "ui/display/display_switches.h"
#include "ui/display/manager/display_layout_store.h"
#include "ui/display/manager/display_manager.h"
#include "ui/display/manager/display_manager_utilities.h"
#include "ui/display/manager/touch_device_manager.h"
#include "ui/display/types/display_mode.h"
#include "ui/display/types/display_snapshot.h"
#include "ui/display/util/display_util.h"
#include "ui/display/util/edid_parser.h"
#include "ui/events/devices/input_device_manager.h"
#include "ui/events/devices/touchscreen_device.h"
#include "ui/strings/grit/ui_strings.h"

namespace display {
namespace {

// The DPI threshold to determine the device scale factor.
// DPI higher than |dpi| will use |device_scale_factor|.
struct DeviceScaleFactorDPIThreshold {
  float dpi;
  float device_scale_factor;
};

// Update the list of zoom levels whenever a new device scale factor is added
// here. See zoom level list in /ui/display/manager/display_util.cc
const DeviceScaleFactorDPIThreshold kThresholdTableForInternal[] = {
    {270.0f, 2.25f}, {220.0f, 2.0f}, {180.0f, 1.6f},
    {150.0f, 1.25f}, {0.0f, 1.0f},
};

// Returns a list of display modes for the given |output| that doesn't exclude
// any mode. The returned list is sorted by size, then by refresh rate, then by
// is_interlaced.
ManagedDisplayInfo::ManagedDisplayModeList GetModeListWithAllRefreshRates(
    const DisplaySnapshot& output) {
  ManagedDisplayInfo::ManagedDisplayModeList display_mode_list;
  for (const auto& mode_info : output.modes()) {
    display_mode_list.emplace_back(mode_info->size(), mode_info->refresh_rate(),
                                   mode_info->is_interlaced(),
                                   output.native_mode() == mode_info.get(),
                                   1.0);
  }

  std::sort(
      display_mode_list.begin(), display_mode_list.end(),
      [](const ManagedDisplayMode& lhs, const ManagedDisplayMode& rhs) {
        return std::forward_as_tuple(lhs.size().width(), lhs.size().height(),
                                     lhs.refresh_rate(), lhs.is_interlaced()) <
               std::forward_as_tuple(rhs.size().width(), rhs.size().height(),
                                     rhs.refresh_rate(), rhs.is_interlaced());
      });

  return display_mode_list;
}

}  // namespace

// static
ManagedDisplayInfo::ManagedDisplayModeList
DisplayChangeObserver::GetInternalManagedDisplayModeList(
    const ManagedDisplayInfo& display_info,
    const DisplaySnapshot& output) {
  const DisplayMode* ui_native_mode = output.native_mode();
  ManagedDisplayMode native_mode(ui_native_mode->size(),
                                 ui_native_mode->refresh_rate(),
                                 ui_native_mode->is_interlaced(), true,
                                 display_info.device_scale_factor());
  return CreateInternalManagedDisplayModeList(native_mode);
}

// static
ManagedDisplayInfo::ManagedDisplayModeList
DisplayChangeObserver::GetExternalManagedDisplayModeList(
    const DisplaySnapshot& output) {
  if (display::features::IsListAllDisplayModesEnabled())
    return GetModeListWithAllRefreshRates(output);

  struct SizeComparator {
    constexpr bool operator()(const gfx::Size& lhs,
                              const gfx::Size& rhs) const {
      return std::forward_as_tuple(lhs.width(), lhs.height()) <
             std::forward_as_tuple(rhs.width(), rhs.height());
    }
  };

  using DisplayModeMap =
      std::map<gfx::Size, ManagedDisplayMode, SizeComparator>;
  DisplayModeMap display_mode_map;

  ManagedDisplayMode native_mode;
  for (const auto& mode_info : output.modes()) {
    const gfx::Size size = mode_info->size();

    ManagedDisplayMode display_mode(
        mode_info->size(), mode_info->refresh_rate(),
        mode_info->is_interlaced(), output.native_mode() == mode_info.get(),
        1.0);
    if (display_mode.native())
      native_mode = display_mode;

    // Add the display mode if it isn't already present and override interlaced
    // display modes with non-interlaced ones. We prioritize having non
    // interlaced mode over refresh rate. A mode having lower refresh rate
    // but is not interlaced will be picked over a mode having high refresh
    // rate but is interlaced.
    auto display_mode_it = display_mode_map.find(size);
    if (display_mode_it == display_mode_map.end()) {
      display_mode_map.emplace(size, display_mode);
    } else if (display_mode_it->second.is_interlaced() &&
               !display_mode.is_interlaced()) {
      display_mode_it->second = std::move(display_mode);
    } else if (!display_mode.is_interlaced() &&
               display_mode_it->second.refresh_rate() <
                   display_mode.refresh_rate()) {
      display_mode_it->second = std::move(display_mode);
    }
  }

  if (output.native_mode()) {
    const gfx::Size size = native_mode.size();

    auto it = display_mode_map.find(size);
    DCHECK(it != display_mode_map.end())
        << "Native mode must be part of the mode list.";

    // If the native mode was replaced (e.g. by a mode with similar size but
    // higher refresh rate), we overwrite that mode with the native mode. The
    // native mode will always be chosen as the best mode for this size (see
    // DisplayConfigurator::FindDisplayModeMatchingSize()).
    if (!it->second.native())
      it->second = native_mode;
  }

  ManagedDisplayInfo::ManagedDisplayModeList display_mode_list;
  for (const auto& display_mode_pair : display_mode_map)
    display_mode_list.push_back(std::move(display_mode_pair.second));

  return display_mode_list;
}

DisplayChangeObserver::DisplayChangeObserver(DisplayManager* display_manager)
    : display_manager_(display_manager) {
  ui::InputDeviceManager::GetInstance()->AddObserver(this);
}

DisplayChangeObserver::~DisplayChangeObserver() {
  ui::InputDeviceManager::GetInstance()->RemoveObserver(this);
}

MultipleDisplayState DisplayChangeObserver::GetStateForDisplayIds(
    const DisplayConfigurator::DisplayStateList& display_states) {
  UpdateInternalDisplay(display_states);
  if (display_states.size() == 1)
    return MULTIPLE_DISPLAY_STATE_SINGLE;
  DisplayIdList list =
      GenerateDisplayIdList(display_states.begin(), display_states.end(),
                            [](const DisplaySnapshot* display_state) {
                              return display_state->display_id();
                            });
  return display_manager_->ShouldSetMirrorModeOn(list)
             ? MULTIPLE_DISPLAY_STATE_MULTI_MIRROR
             : MULTIPLE_DISPLAY_STATE_MULTI_EXTENDED;
}

bool DisplayChangeObserver::GetSelectedModeForDisplayId(
    int64_t display_id,
    ManagedDisplayMode* out_mode) const {
  return display_manager_->GetSelectedModeForDisplayId(display_id, out_mode);
}

void DisplayChangeObserver::OnDisplayModeChanged(
    const DisplayConfigurator::DisplayStateList& display_states) {
  UpdateInternalDisplay(display_states);

  std::vector<ManagedDisplayInfo> displays;
  for (const DisplaySnapshot* state : display_states) {
    const DisplayMode* mode_info = state->current_mode();
    if (!mode_info)
      continue;

    displays.emplace_back(CreateManagedDisplayInfo(state, mode_info));
  }

  display_manager_->touch_device_manager()->AssociateTouchscreens(
      &displays,
      ui::InputDeviceManager::GetInstance()->GetTouchscreenDevices());
  display_manager_->OnNativeDisplaysChanged(displays);

  // For the purposes of user activity detection, ignore synthetic mouse events
  // that are triggered by screen resizes: http://crbug.com/360634
  ui::UserActivityDetector* user_activity_detector =
      ui::UserActivityDetector::Get();
  if (user_activity_detector)
    user_activity_detector->OnDisplayPowerChanging();
}

void DisplayChangeObserver::OnDisplayModeChangeFailed(
    const DisplayConfigurator::DisplayStateList& displays,
    MultipleDisplayState failed_new_state) {
  // If display configuration failed during startup, simply update the display
  // manager with detected displays. If no display is detected, it will
  // create a pseudo display.
  if (display_manager_->GetNumDisplays() == 0)
    OnDisplayModeChanged(displays);
}

void DisplayChangeObserver::OnInputDeviceConfigurationChanged(
    uint8_t input_device_types) {
  if (input_device_types & ui::InputDeviceEventObserver::kTouchscreen) {
    // If there are no cached display snapshots, either there are no attached
    // displays or the cached snapshots have been invalidated. For the first
    // case there aren't any touchscreens to associate. For the second case,
    // the displays and touch input-devices will get associated when display
    // configuration finishes.
    const auto& cached_displays =
        display_manager_->configurator()->cached_displays();
    if (!cached_displays.empty())
      OnDisplayModeChanged(cached_displays);
  }
}

void DisplayChangeObserver::UpdateInternalDisplay(
    const DisplayConfigurator::DisplayStateList& display_states) {
  bool force_first_display_internal = ForceFirstDisplayInternal();

  for (auto* state : display_states) {
    if (state->type() == DISPLAY_CONNECTION_TYPE_INTERNAL ||
        (force_first_display_internal &&
         (!Display::HasInternalDisplay() ||
          state->display_id() == Display::InternalDisplayId()))) {
      if (Display::HasInternalDisplay())
        DCHECK_EQ(Display::InternalDisplayId(), state->display_id());
      Display::SetInternalDisplayId(state->display_id());

      if (state->native_mode() &&
          (!display_manager_->IsDisplayIdValid(state->display_id()) ||
           !state->current_mode())) {
        // Register the internal display info if
        // 1) If it's not already registered. It'll be treated as
        // new display in |UpdateDisplaysWith()|.
        // 2) If it's not connected, because the display info will not
        // be updated in |UpdateDisplaysWith()|, which will skips the
        // disconnected displays.
        ManagedDisplayInfo new_info =
            CreateManagedDisplayInfo(state, state->native_mode());
        display_manager_->UpdateInternalDisplay(new_info);
      }
      return;
    }
  }
}

ManagedDisplayInfo DisplayChangeObserver::CreateManagedDisplayInfo(
    const DisplaySnapshot* snapshot,
    const DisplayMode* mode_info) {
  std::string name = (snapshot->type() == DISPLAY_CONNECTION_TYPE_INTERNAL)
                         ? l10n_util::GetStringUTF8(IDS_DISPLAY_NAME_INTERNAL)
                         : snapshot->display_name();

  if (name.empty())
    name = l10n_util::GetStringUTF8(IDS_DISPLAY_NAME_UNKNOWN);

  const bool has_overscan = snapshot->has_overscan();
  const int64_t id = snapshot->display_id();

  ManagedDisplayInfo new_info = ManagedDisplayInfo(id, name, has_overscan);

  if (snapshot->product_code() != DisplaySnapshot::kInvalidProductCode) {
    uint16_t manufacturer_id = 0;
    uint16_t product_id = 0;
    EdidParser::SplitProductCodeInManufacturerIdAndProductId(
        snapshot->product_code(), &manufacturer_id, &product_id);
    new_info.set_manufacturer_id(
        EdidParser::ManufacturerIdToString(manufacturer_id));
    new_info.set_product_id(EdidParser::ProductIdToString(product_id));
  }
  new_info.set_year_of_manufacture(snapshot->year_of_manufacture());

  new_info.set_sys_path(snapshot->sys_path());
  new_info.set_native(true);

  float device_scale_factor = 1.0f;
  // Sets dpi only if the screen size is not blacklisted.
  const float dpi = IsDisplaySizeBlackListed(snapshot->physical_size())
                        ? 0
                        : kInchInMm * mode_info->size().width() /
                              snapshot->physical_size().width();
  constexpr gfx::Size k225DisplaySizeHack(3000, 2000);

  if (snapshot->type() == DISPLAY_CONNECTION_TYPE_INTERNAL) {
    // TODO(oshima): This is a stopgap hack to deal with b/74845106.
    // Remove this hack when it's resolved.
    if (mode_info->size() == k225DisplaySizeHack)
      device_scale_factor = 2.25f;
    else if (dpi)
      device_scale_factor = FindDeviceScaleFactor(dpi);
  } else {
    ManagedDisplayMode mode;
    if (display_manager_->GetSelectedModeForDisplayId(snapshot->display_id(),
                                                      &mode)) {
      device_scale_factor = mode.device_scale_factor();
      new_info.set_native(mode.native());
    }
  }
  new_info.set_device_scale_factor(device_scale_factor);

  const gfx::Rect display_bounds(snapshot->origin(), mode_info->size());
  new_info.SetBounds(display_bounds);
  new_info.set_is_aspect_preserving_scaling(
      snapshot->is_aspect_preserving_scaling());
  if (dpi)
    new_info.set_device_dpi(dpi);
  new_info.set_color_space(snapshot->color_space());

  new_info.set_refresh_rate(mode_info->refresh_rate());
  new_info.set_is_interlaced(mode_info->is_interlaced());

  ManagedDisplayInfo::ManagedDisplayModeList display_modes =
      (snapshot->type() == DISPLAY_CONNECTION_TYPE_INTERNAL)
          ? GetInternalManagedDisplayModeList(new_info, *snapshot)
          : GetExternalManagedDisplayModeList(*snapshot);
  new_info.SetManagedDisplayModes(display_modes);

  new_info.set_maximum_cursor_size(snapshot->maximum_cursor_size());
  return new_info;
}

// static
float DisplayChangeObserver::FindDeviceScaleFactor(float dpi) {
  for (size_t i = 0; i < base::size(kThresholdTableForInternal); ++i) {
    if (dpi > kThresholdTableForInternal[i].dpi)
      return kThresholdTableForInternal[i].device_scale_factor;
  }
  return 1.0f;
}

}  // namespace display
