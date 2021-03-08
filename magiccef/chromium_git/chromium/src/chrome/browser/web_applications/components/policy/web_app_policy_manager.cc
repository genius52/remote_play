// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/components/policy/web_app_policy_manager.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/task/post_task.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/components/install_options.h"
#include "chrome/browser/web_applications/components/policy/web_app_policy_constants.h"
#include "chrome/browser/web_applications/components/web_app_constants.h"
#include "chrome/common/pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"

namespace web_app {

namespace {

void ParseInstallOptionsFromPolicyEntry(const base::Value& entry,
                                        InstallOptions* install_options) {
  DCHECK(install_options);

  const base::Value& url = *entry.FindKey(kUrlKey);
  const base::Value* default_launch_container =
      entry.FindKey(kDefaultLaunchContainerKey);
  const base::Value* create_desktop_shortcut =
      entry.FindKey(kCreateDesktopShorcutKey);

  DCHECK(!default_launch_container ||
         default_launch_container->GetString() ==
             kDefaultLaunchContainerWindowValue ||
         default_launch_container->GetString() ==
             kDefaultLaunchContainerTabValue);

  install_options->url = GURL(url.GetString());
  install_options->install_source = web_app::InstallSource::kExternalPolicy;

  if (!default_launch_container) {
    install_options->launch_container = LaunchContainer::kTab;
  } else if (default_launch_container->GetString() ==
             kDefaultLaunchContainerTabValue) {
    install_options->launch_container = LaunchContainer::kTab;
  } else {
    install_options->launch_container = LaunchContainer::kWindow;
  }

  bool create_shortcut = false;
  if (create_desktop_shortcut)
    create_shortcut = create_desktop_shortcut->GetBool();

  install_options->add_to_applications_menu = true;
  install_options->add_to_desktop = create_shortcut;

  // It's not yet clear how pinning to shelf will work for policy installed
  // Web Apps, but for now never pin them. See crbug.com/880125.
  install_options->add_to_quick_launch_bar = false;
}

}  // namespace

WebAppPolicyManager::WebAppPolicyManager(Profile* profile,
                                         PendingAppManager* pending_app_manager)
    : profile_(profile),
      pref_service_(profile_->GetPrefs()),
      pending_app_manager_(pending_app_manager) {}

WebAppPolicyManager::~WebAppPolicyManager() = default;

void WebAppPolicyManager::Start() {
  base::PostTaskWithTraits(
      FROM_HERE, {content::BrowserThread::UI, base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&WebAppPolicyManager::
                         InitChangeRegistrarAndRefreshPolicyInstalledApps,
                     weak_ptr_factory_.GetWeakPtr()));
}

void WebAppPolicyManager::ReinstallPlaceholderAppIfNecessary(const GURL& url) {
  const base::Value* web_apps =
      pref_service_->GetList(prefs::kWebAppInstallForceList);
  const auto& web_apps_list = web_apps->GetList();

  const auto it =
      std::find_if(web_apps_list.begin(), web_apps_list.end(),
                   [&url](const base::Value& entry) {
                     return entry.FindKey(kUrlKey)->GetString() == url.spec();
                   });

  if (it == web_apps_list.end())
    return;

  InstallOptions install_options;
  ParseInstallOptionsFromPolicyEntry(*it, &install_options);

  // No need to install a placeholder because there should be one already.
  install_options.wait_for_windows_closed = true;
  install_options.reinstall_placeholder = true;

  // If the app is not a placeholder app, PendingAppManager will ignore the
  // request.
  pending_app_manager_->Install(std::move(install_options), base::DoNothing());
}

// static
void WebAppPolicyManager::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(prefs::kWebAppInstallForceList);
}

void WebAppPolicyManager::InitChangeRegistrarAndRefreshPolicyInstalledApps() {
  pref_change_registrar_.Init(pref_service_);
  pref_change_registrar_.Add(
      prefs::kWebAppInstallForceList,
      base::BindRepeating(&WebAppPolicyManager::RefreshPolicyInstalledApps,
                          weak_ptr_factory_.GetWeakPtr()));

  RefreshPolicyInstalledApps();
}

void WebAppPolicyManager::RefreshPolicyInstalledApps() {
  // If this is called again while in progress, we will run it again once the
  // |SynchronizeInstalledApps| call is finished.
  if (is_refreshing_) {
    needs_refresh_ = true;
    return;
  }

  is_refreshing_ = true;
  needs_refresh_ = false;

  const base::Value* web_apps =
      pref_service_->GetList(prefs::kWebAppInstallForceList);
  std::vector<InstallOptions> install_options_list;
  // No need to validate the types or values of the policy members because we
  // are using a SimpleSchemaValidatingPolicyHandler which should validate them
  // for us.
  for (const base::Value& entry : web_apps->GetList()) {
    InstallOptions install_options;
    ParseInstallOptionsFromPolicyEntry(entry, &install_options);

    install_options.install_placeholder = true;
    // When the policy gets refreshed, we should try to reinstall placeholder
    // apps but only if they are not being used.
    install_options.wait_for_windows_closed = true;
    install_options.reinstall_placeholder = true;

    install_options_list.push_back(std::move(install_options));
  }

  pending_app_manager_->SynchronizeInstalledApps(
      std::move(install_options_list), InstallSource::kExternalPolicy,
      base::BindOnce(&WebAppPolicyManager::OnAppsSynchronized,
                     weak_ptr_factory_.GetWeakPtr()));
}

void WebAppPolicyManager::OnAppsSynchronized(
    PendingAppManager::SynchronizeResult result) {
  is_refreshing_ = false;
  if (needs_refresh_)
    RefreshPolicyInstalledApps();
}

}  // namespace web_app
