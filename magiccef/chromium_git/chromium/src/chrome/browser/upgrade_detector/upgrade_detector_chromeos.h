// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UPGRADE_DETECTOR_UPGRADE_DETECTOR_CHROMEOS_H_
#define CHROME_BROWSER_UPGRADE_DETECTOR_UPGRADE_DETECTOR_CHROMEOS_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/upgrade_detector/upgrade_detector.h"
#include "chromeos/dbus/update_engine_client.h"

class PrefRegistrySimple;
namespace base {
class Clock;
template <typename T>
class NoDestructor;
class TickClock;
}  // namespace base

class UpgradeDetectorChromeos : public UpgradeDetector,
                                public chromeos::UpdateEngineClient::Observer {
 public:
  ~UpgradeDetectorChromeos() override;

  // Register ChromeOS specific Prefs.
  static void RegisterPrefs(PrefRegistrySimple* registry);

  static UpgradeDetectorChromeos* GetInstance();

  // Initializes the object. Starts observing changes from the update
  // engine.
  void Init();

  // Shuts down the object. Stops observing observe changes from the
  // update engine.
  void Shutdown();

  // UpgradeDetector:
  base::TimeDelta GetHighAnnoyanceLevelDelta() override;
  base::Time GetHighAnnoyanceDeadline() override;

 protected:
  UpgradeDetectorChromeos(const base::Clock* clock,
                          const base::TickClock* tick_clock);

 private:
  friend class base::NoDestructor<UpgradeDetectorChromeos>;

  // Returns the period between first notification and Recommended / Required
  // deadline specified via the RelaunchHeadsUpPeriod policy setting, or a
  // zero delta if unset or out of range.
  static base::TimeDelta GetRelaunchHeadsUpPeriod();

  // Calculates |elevated_threshold_| and |high_threshold_|.
  void CalculateThresholds();

  // Handles a change to the browser.relaunch_heads_up_period Local State
  // preference. Calls NotifyUpgrade if an upgrade is available.
  void OnRelaunchHeadsUpPeriodPrefChanged();

  // Returns the threshold to reach high annoyance level.
  static base::TimeDelta DetermineHighThreshold();

  // UpgradeDetector:
  void OnRelaunchNotificationPeriodPrefChanged() override;

  // chromeos::UpdateEngineClient::Observer implementation.
  void UpdateStatusChanged(
      const chromeos::UpdateEngineClient::Status& status) override;
  void OnUpdateOverCellularOneTimePermissionGranted() override;

  // Triggers NotifyOnUpgrade if thresholds have been changed.
  void OnThresholdPrefChanged();

  // The function that sends out a notification (after a certain time has
  // elapsed) that lets the rest of the UI know we should start notifying the
  // user that a new version is available.
  void NotifyOnUpgrade();

  void OnChannelsReceived(std::string current_channel,
                          std::string target_channel);

  // The delta from upgrade detection until elevated annoyance level is reached.
  base::TimeDelta elevated_threshold_;

  // The delta from upgrade detection until high annoyance level is reached.
  base::TimeDelta high_threshold_;

  // Observes changes to the browser.relaunch_heads_up_period Local State
  // preference.
  PrefChangeRegistrar pref_change_registrar_;

  // A timer used to move through the various upgrade notification stages and
  // call UpgradeDetector::NotifyUpgrade.
  base::OneShotTimer upgrade_notification_timer_;
  bool initialized_;

  base::WeakPtrFactory<UpgradeDetectorChromeos> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(UpgradeDetectorChromeos);
};

#endif  // CHROME_BROWSER_UPGRADE_DETECTOR_UPGRADE_DETECTOR_CHROMEOS_H_
