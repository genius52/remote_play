// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOWNLOAD_DEFAULT_DOWNLOAD_DIR_POLICY_HANDLER_H_
#define CHROME_BROWSER_DOWNLOAD_DEFAULT_DOWNLOAD_DIR_POLICY_HANDLER_H_

#include "base/macros.h"
#include "components/policy/core/browser/configuration_policy_handler.h"

// ConfigurationPolicyHandler for the DefaultDownloadDirectory policy.
class DefaultDownloadDirPolicyHandler
    : public policy::TypeCheckingPolicyHandler {
 public:
  DefaultDownloadDirPolicyHandler();
  ~DefaultDownloadDirPolicyHandler() override;

  // ConfigurationPolicyHandler:
  bool CheckPolicySettings(const policy::PolicyMap& policies,
                           policy::PolicyErrorMap* errors) override;

  void ApplyPolicySettingsWithParameters(
      const policy::PolicyMap& policies,
      const policy::PolicyHandlerParameters& parameters,
      PrefValueMap* prefs) override;

 protected:
  void ApplyPolicySettings(const policy::PolicyMap& policies,
                           PrefValueMap* prefs) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(DefaultDownloadDirPolicyHandler);
};

#endif  // CHROME_BROWSER_DOWNLOAD_DEFAULT_DOWNLOAD_DIR_POLICY_HANDLER_H_
