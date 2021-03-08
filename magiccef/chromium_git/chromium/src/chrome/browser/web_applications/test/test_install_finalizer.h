// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEB_APPLICATIONS_TEST_TEST_INSTALL_FINALIZER_H_
#define CHROME_BROWSER_WEB_APPLICATIONS_TEST_TEST_INSTALL_FINALIZER_H_

#include <map>
#include <memory>

#include "base/macros.h"
#include "base/optional.h"
#include "chrome/browser/web_applications/components/install_finalizer.h"

struct WebApplicationInfo;

namespace web_app {

class TestInstallFinalizer final : public InstallFinalizer {
 public:
  TestInstallFinalizer();
  ~TestInstallFinalizer() override;

  // InstallFinalizer:
  void FinalizeInstall(const WebApplicationInfo& web_app_info,
                       const FinalizeOptions& options,
                       InstallFinalizedCallback callback) override;
  void UninstallExternalWebApp(
      const GURL& app_url,
      UninstallExternalWebAppCallback callback) override;
  bool CanCreateOsShortcuts() const override;
  void CreateOsShortcuts(const AppId& app_id,
                         bool add_to_desktop,
                         CreateOsShortcutsCallback callback) override;
  bool CanPinAppToShelf() const override;
  void PinAppToShelf(const AppId& app_id) override;
  bool CanReparentTab(const AppId& app_id,
                      bool shortcut_created) const override;
  void ReparentTab(const AppId& app_id,
                   content::WebContents* web_contents) override;
  bool CanRevealAppShim() const override;
  void RevealAppShim(const AppId& app_id) override;
  bool CanSkipAppUpdateForSync(
      const AppId& app_id,
      const WebApplicationInfo& web_app_info) const override;

  void SetNextFinalizeInstallResult(const AppId& app_id,
                                    InstallResultCode code);
  void SetNextUninstallExternalWebAppResult(const GURL& app_url,
                                            bool uninstalled);

  std::unique_ptr<WebApplicationInfo> web_app_info() {
    return std::move(web_app_info_copy_);
  }

  const std::vector<FinalizeOptions>& finalize_options_list() const {
    return finalize_options_list_;
  }

  const std::vector<GURL>& uninstall_external_web_app_urls() const {
    return uninstall_external_web_app_urls_;
  }

  int num_create_os_shortcuts_calls() { return num_create_os_shortcuts_calls_; }
  int num_reparent_tab_calls() { return num_reparent_tab_calls_; }
  int num_reveal_appshim_calls() { return num_reveal_appshim_calls_; }
  int num_pin_app_to_shelf_calls() { return num_pin_app_to_shelf_calls_; }

 private:
  std::unique_ptr<WebApplicationInfo> web_app_info_copy_;
  std::vector<FinalizeOptions> finalize_options_list_;
  std::vector<GURL> uninstall_external_web_app_urls_;

  base::Optional<AppId> next_app_id_;
  base::Optional<InstallResultCode> next_result_code_;
  std::map<GURL, bool> next_uninstall_external_web_app_results_;

  int num_create_os_shortcuts_calls_ = 0;
  int num_reparent_tab_calls_ = 0;
  int num_reveal_appshim_calls_ = 0;
  int num_pin_app_to_shelf_calls_ = 0;

  DISALLOW_COPY_AND_ASSIGN(TestInstallFinalizer);
};

}  // namespace web_app

#endif  // CHROME_BROWSER_WEB_APPLICATIONS_TEST_TEST_INSTALL_FINALIZER_H_
