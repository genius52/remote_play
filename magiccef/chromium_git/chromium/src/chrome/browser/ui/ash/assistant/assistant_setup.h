// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_ASSISTANT_ASSISTANT_SETUP_H_
#define CHROME_BROWSER_UI_ASH_ASSISTANT_ASSISTANT_SETUP_H_

#include "ash/public/cpp/assistant/assistant_setup.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/arc/voice_interaction/voice_interaction_controller_client.h"
#include "chromeos/services/assistant/public/mojom/settings.mojom.h"

namespace service_manager {
class Connector;
}  // namespace service_manager

// AssistantSetup is the class responsible for start Assistant OptIn flow.
class AssistantSetup : public ash::AssistantSetup,
                       public arc::VoiceInteractionControllerClient::Observer {
 public:
  explicit AssistantSetup(service_manager::Connector* connector);
  ~AssistantSetup() override;

  // ash::AssistantSetup:
  void StartAssistantOptInFlow(
      ash::FlowType type,
      StartAssistantOptInFlowCallback callback) override;

  // If prefs::kVoiceInteractionConsentStatus is nullptr, means the
  // pref is not set by user. Therefore we need to start OOBE.
  void MaybeStartAssistantOptInFlow();

 private:
  // arc::VoiceInteractionControllerClient::Observer overrides
  void OnStateChanged(ash::mojom::VoiceInteractionState state) override;

  void SyncActivityControlState();
  void OnGetSettingsResponse(const std::string& settings);

  service_manager::Connector* connector_;
  chromeos::assistant::mojom::AssistantSettingsManagerPtr settings_manager_;

  base::WeakPtrFactory<AssistantSetup> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(AssistantSetup);
};

#endif  // CHROME_BROWSER_UI_ASH_ASSISTANT_ASSISTANT_SETUP_H_
