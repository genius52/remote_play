// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/chrome_new_window_client.h"

#include <string>
#include <utility>

#include "ash/public/cpp/arc_custom_tab.h"
#include "ash/public/cpp/ash_features.h"
#include "ash/public/cpp/keyboard_shortcut_viewer.h"
#include "ash/public/interfaces/constants.mojom.h"
#include "base/macros.h"
#include "base/metrics/histogram_macros.h"
#include "base/timer/elapsed_timer.h"
#include "chrome/browser/chromeos/arc/arc_web_contents_data.h"
#include "chrome/browser/chromeos/arc/fileapi/arc_content_file_system_url_util.h"
#include "chrome/browser/chromeos/file_manager/app_id.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/extensions/api/terminal/terminal_extension_helper.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/extensions/app_launch_params.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "chrome/browser/ui/scoped_tabbed_browser_displayer.h"
#include "chrome/browser/ui/settings_window_manager_chromeos.h"
#include "chrome/browser/ui/webui/chrome_web_contents_handler.h"
#include "chrome/common/url_constants.h"
#include "chrome/common/webui_url_constants.h"
#include "components/arc/intent_helper/arc_intent_helper_bridge.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_service_observer.h"
#include "components/url_formatter/url_fixer.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_manager.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/service_manager_connection.h"
#include "content/public/common/user_agent.h"
#include "content/public/common/was_activated_option.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "services/service_manager/public/cpp/connector.h"
#include "ui/aura/window.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/url_constants.h"

namespace {

void RestoreTabUsingProfile(Profile* profile) {
  sessions::TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfile(profile);
  service->RestoreMostRecentEntry(nullptr);
}

bool IsIncognitoAllowed() {
  Profile* profile = ProfileManager::GetActiveUserProfile();
  return profile && !profile->IsGuestSession() &&
         IncognitoModePrefs::GetAvailability(profile->GetPrefs()) !=
             IncognitoModePrefs::DISABLED;
}

// Converts the given ARC URL to an external file URL to read it via ARC content
// file system when necessary. Otherwise, returns the given URL unchanged.
GURL ConvertArcUrlToExternalFileUrlIfNeeded(const GURL& url) {
  if (url.SchemeIs(url::kFileScheme) || url.SchemeIs(url::kContentScheme)) {
    // Chrome cannot open this URL. Read the contents via ARC content file
    // system with an external file URL.
    return arc::ArcUrlToExternalFileUrl(url);
  }
  return url;
}

// Returns URL path and query without the "/" prefix. For example, for the URL
// "chrome://settings/networks/?type=WiFi" returns "networks/?type=WiFi".
std::string GetPathAndQuery(const GURL& url) {
  std::string result = url.path();
  if (!result.empty() && result[0] == '/')
    result.erase(0, 1);
  if (url.has_query()) {
    result += '?';
    result += url.query();
  }
  return result;
}

// Implementation of CustomTabSession interface.
class CustomTabSessionImpl : public arc::mojom::CustomTabSession {
 public:
  static arc::mojom::CustomTabSessionPtr Create(
      Profile* profile,
      const GURL& url,
      std::unique_ptr<ash::ArcCustomTab> custom_tab) {
    if (!custom_tab)
      return nullptr;

    // This object will be deleted when the mojo connection is closed.
    auto* tab = new CustomTabSessionImpl(profile, url, std::move(custom_tab));
    arc::mojom::CustomTabSessionPtr ptr;
    tab->Bind(&ptr);
    return ptr;
  }

  // arc::mojom::CustomTabSession override:
  void OnOpenInChromeClicked() override { forwarded_to_normal_tab_ = true; }

 private:
  CustomTabSessionImpl(Profile* profile,
                       const GURL& url,
                       std::unique_ptr<ash::ArcCustomTab> custom_tab)
      : binding_(this),
        custom_tab_(std::move(custom_tab)),
        web_contents_(CreateWebContents(profile, url)),
        weak_ptr_factory_(this) {
    aura::Window* window = web_contents_->GetNativeView();
    custom_tab_->Attach(window);
    window->Show();
  }

  ~CustomTabSessionImpl() override {
    // Keep in sync with ArcCustomTabsSessionEndReason in
    // //tools/metrics/histograms/enums.xml.
    enum class SessionEndReason {
      CLOSED = 0,
      FORWARDED_TO_NORMAL_TAB = 1,
      kMaxValue = FORWARDED_TO_NORMAL_TAB,
    } session_end_reason = forwarded_to_normal_tab_
                               ? SessionEndReason::FORWARDED_TO_NORMAL_TAB
                               : SessionEndReason::CLOSED;
    UMA_HISTOGRAM_ENUMERATION("Arc.CustomTabs.SessionEndReason",
                              session_end_reason);
    auto elapsed = lifetime_timer_.Elapsed();
    UMA_HISTOGRAM_MEDIUM_TIMES("Arc.CustomTabs.SessionLifetime.All", elapsed);
    switch (session_end_reason) {
      case SessionEndReason::CLOSED:
        UMA_HISTOGRAM_MEDIUM_TIMES("Arc.CustomTabs.SessionLifetime.Closed",
                                   elapsed);
        break;
      case SessionEndReason::FORWARDED_TO_NORMAL_TAB:
        UMA_HISTOGRAM_MEDIUM_TIMES(
            "Arc.CustomTabs.SessionLifetime.ForwardedToNormalTab", elapsed);
        break;
    }
  }

  void Bind(arc::mojom::CustomTabSessionPtr* ptr) {
    binding_.Bind(mojo::MakeRequest(ptr));
    binding_.set_connection_error_handler(base::BindOnce(
        &CustomTabSessionImpl::Close, weak_ptr_factory_.GetWeakPtr()));
  }

  // Deletes this object when the mojo connection is closed.
  void Close() { delete this; }

  std::unique_ptr<content::WebContents> CreateWebContents(Profile* profile,
                                                          const GURL& url) {
    scoped_refptr<content::SiteInstance> site_instance =
        tab_util::GetSiteInstanceForNewTab(profile, url);
    content::WebContents::CreateParams create_params(profile, site_instance);
    std::unique_ptr<content::WebContents> web_contents =
        content::WebContents::Create(create_params);

    // Override the user agent to request mobile version web sites.
    chrome::SetAndroidOsForTabletSite(web_contents.get());

    content::NavigationController::LoadURLParams load_url_params(url);
    load_url_params.source_site_instance = site_instance;
    load_url_params.override_user_agent =
        content::NavigationController::UA_OVERRIDE_TRUE;
    web_contents->GetController().LoadURLWithParams(load_url_params);

    // Add a flag to remember this tab originated in the ARC context.
    web_contents->SetUserData(&arc::ArcWebContentsData::kArcTransitionFlag,
                              std::make_unique<arc::ArcWebContentsData>());
    return web_contents;
  }

  mojo::Binding<arc::mojom::CustomTabSession> binding_;
  std::unique_ptr<ash::ArcCustomTab> custom_tab_;
  std::unique_ptr<content::WebContents> web_contents_;
  base::ElapsedTimer lifetime_timer_;
  bool forwarded_to_normal_tab_ = false;
  base::WeakPtrFactory<CustomTabSessionImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(CustomTabSessionImpl);
};

}  // namespace

ChromeNewWindowClient::ChromeNewWindowClient() {
  arc::ArcIntentHelperBridge::SetOpenUrlDelegate(this);
}

ChromeNewWindowClient::~ChromeNewWindowClient() {
  arc::ArcIntentHelperBridge::SetOpenUrlDelegate(nullptr);
}

// static
ChromeNewWindowClient* ChromeNewWindowClient::Get() {
  return static_cast<ChromeNewWindowClient*>(
      ash::NewWindowDelegate::GetInstance());
}

// TabRestoreHelper is used to restore a tab. In particular when the user
// attempts to a restore a tab if the TabRestoreService hasn't finished loading
// this waits for it. Once the TabRestoreService finishes loading the tab is
// restored.
class ChromeNewWindowClient::TabRestoreHelper
    : public sessions::TabRestoreServiceObserver {
 public:
  TabRestoreHelper(ChromeNewWindowClient* delegate,
                   Profile* profile,
                   sessions::TabRestoreService* service)
      : delegate_(delegate), profile_(profile), tab_restore_service_(service) {
    tab_restore_service_->AddObserver(this);
  }

  ~TabRestoreHelper() override { tab_restore_service_->RemoveObserver(this); }

  sessions::TabRestoreService* tab_restore_service() {
    return tab_restore_service_;
  }

  void TabRestoreServiceChanged(sessions::TabRestoreService* service) override {
  }

  void TabRestoreServiceDestroyed(
      sessions::TabRestoreService* service) override {
    // This destroys us.
    delegate_->tab_restore_helper_.reset();
  }

  void TabRestoreServiceLoaded(sessions::TabRestoreService* service) override {
    RestoreTabUsingProfile(profile_);
    // This destroys us.
    delegate_->tab_restore_helper_.reset();
  }

 private:
  ChromeNewWindowClient* delegate_;
  Profile* profile_;
  sessions::TabRestoreService* tab_restore_service_;

  DISALLOW_COPY_AND_ASSIGN(TabRestoreHelper);
};

void ChromeNewWindowClient::NewTab() {
  Browser* browser = chrome::FindBrowserWithActiveWindow();
  if (browser && browser->is_type_tabbed()) {
    chrome::NewTab(browser);
    return;
  }

  // Display a browser, setting the focus to the location bar after it is shown.
  {
    chrome::ScopedTabbedBrowserDisplayer displayer(
        ProfileManager::GetActiveUserProfile());
    browser = displayer.browser();
    chrome::NewTab(browser);
  }

  browser->SetFocusToLocationBar();
}

void ChromeNewWindowClient::NewTabWithUrl(const GURL& url,
                                          bool from_user_interaction) {
  OpenUrlImpl(url, from_user_interaction);
}

void ChromeNewWindowClient::NewWindow(bool is_incognito) {
  if (is_incognito && !IsIncognitoAllowed())
    return;

  Browser* browser = chrome::FindBrowserWithActiveWindow();
  Profile* profile = (browser && browser->profile())
                         ? browser->profile()->GetOriginalProfile()
                         : ProfileManager::GetActiveUserProfile();
  chrome::NewEmptyWindow(is_incognito ? profile->GetOffTheRecordProfile()
                                      : profile);
}

void ChromeNewWindowClient::OpenFileManager() {
  using file_manager::kFileManagerAppId;
  Profile* const profile = ProfileManager::GetActiveUserProfile();
  const extensions::ExtensionService* const service =
      extensions::ExtensionSystem::Get(profile)->extension_service();
  if (!service || !extensions::util::IsAppLaunchableWithoutEnabling(
                      kFileManagerAppId, profile)) {
    return;
  }

  const extensions::Extension* const extension =
      service->GetInstalledExtension(kFileManagerAppId);
  OpenApplication(CreateAppLaunchParamsUserContainer(
      profile, extension, WindowOpenDisposition::NEW_FOREGROUND_TAB,
      extensions::SOURCE_KEYBOARD));
}

void ChromeNewWindowClient::OpenCrosh() {
  Profile* profile = ProfileManager::GetActiveUserProfile();
  GURL crosh_url =
      extensions::TerminalExtensionHelper::GetCroshExtensionURL(profile);
  if (!crosh_url.is_valid())
    return;
  chrome::ScopedTabbedBrowserDisplayer displayer(profile);
  Browser* browser = displayer.browser();
  content::WebContents* page = browser->OpenURL(content::OpenURLParams(
      crosh_url, content::Referrer(), WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui::PAGE_TRANSITION_GENERATED, false));
  browser->window()->Show();
  browser->window()->Activate();
  page->Focus();
}

void ChromeNewWindowClient::OpenGetHelp() {
  Profile* const profile = ProfileManager::GetActiveUserProfile();
  chrome::ShowHelpForProfile(profile, chrome::HELP_SOURCE_KEYBOARD);
}

void ChromeNewWindowClient::RestoreTab() {
  if (tab_restore_helper_.get()) {
    DCHECK(!tab_restore_helper_->tab_restore_service()->IsLoaded());
    return;
  }

  Browser* browser = chrome::FindBrowserWithActiveWindow();
  Profile* profile = browser ? browser->profile() : nullptr;
  if (!profile)
    profile = ProfileManager::GetActiveUserProfile();
  if (profile->IsOffTheRecord())
    return;
  sessions::TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfile(profile);
  if (!service)
    return;

  if (service->IsLoaded()) {
    RestoreTabUsingProfile(profile);
  } else {
    tab_restore_helper_.reset(new TabRestoreHelper(this, profile, service));
    service->LoadTabsFromLastSession();
  }
}

void ChromeNewWindowClient::ShowKeyboardShortcutViewer() {
  ash::ToggleKeyboardShortcutViewer();
}

void ChromeNewWindowClient::ShowTaskManager() {
  chrome::OpenTaskManager(nullptr);
}

void ChromeNewWindowClient::OpenFeedbackPage(bool from_assistant) {
  chrome::FeedbackSource source;
  source = from_assistant ? chrome::kFeedbackSourceAssistant
                          : chrome::kFeedbackSourceAsh;
  chrome::OpenFeedbackDialog(chrome::FindBrowserWithActiveWindow(), source);
}

void ChromeNewWindowClient::OpenUrlFromArc(const GURL& url) {
  if (!url.is_valid())
    return;

  GURL url_to_open = ConvertArcUrlToExternalFileUrlIfNeeded(url);
  content::WebContents* tab =
      OpenUrlImpl(url_to_open, false /* from_user_interaction */);
  if (!tab)
    return;

  // Add a flag to remember this tab originated in the ARC context.
  tab->SetUserData(&arc::ArcWebContentsData::kArcTransitionFlag,
                   std::make_unique<arc::ArcWebContentsData>());
}

void ChromeNewWindowClient::OpenWebAppFromArc(const GURL& url) {
  DCHECK(url.is_valid() && url.SchemeIs(url::kHttpsScheme));

  // Fetch the profile associated with ARC. This method should only be called
  // for a |url| which was installed via ARC, and so we want the web app that is
  // opened through here to be installed in the profile associated with ARC.
  // |user| may be null if sign-in hasn't happened yet
  const auto* user = user_manager::UserManager::Get()->GetPrimaryUser();
  if (!user)
    return;

  // |profile| may be null if sign-in has happened but the profile isn't loaded
  // yet.
  Profile* profile = chromeos::ProfileHelper::Get()->GetProfileByUser(user);
  if (!profile)
    return;

  const extensions::Extension* extension =
      extensions::util::GetInstalledPwaForUrl(
          profile, url, extensions::LAUNCH_CONTAINER_WINDOW);
  if (!extension) {
    OpenUrlFromArc(url);
    return;
  }

  AppLaunchParams params = CreateAppLaunchParamsUserContainer(
      profile, extension, WindowOpenDisposition::NEW_WINDOW,
      extensions::SOURCE_ARC);
  params.override_url = url;
  content::WebContents* tab = OpenApplication(params);
  if (!tab)
    return;

  // Add a flag to remember this tab originated in the ARC context.
  tab->SetUserData(&arc::ArcWebContentsData::kArcTransitionFlag,
                   std::make_unique<arc::ArcWebContentsData>());
}

void ChromeNewWindowClient::OpenArcCustomTab(
    const GURL& url,
    int32_t task_id,
    int32_t surface_id,
    int32_t top_margin,
    arc::mojom::IntentHelperHost::OnOpenCustomTabCallback callback) {
  GURL url_to_open = ConvertArcUrlToExternalFileUrlIfNeeded(url);
  Profile* profile = ProfileManager::GetActiveUserProfile();
  auto custom_tab = ash::ArcCustomTab::Create(task_id, surface_id, top_margin);
  std::move(callback).Run(
      CustomTabSessionImpl::Create(profile, url, std::move(custom_tab)));
}

content::WebContents* ChromeNewWindowClient::OpenUrlImpl(
    const GURL& url,
    bool from_user_interaction) {
  Profile* profile = ProfileManager::GetActiveUserProfile();
  if ((url.SchemeIs(url::kAboutScheme) ||
       url.SchemeIs(content::kChromeUIScheme))) {
    // Show browser settings (e.g. chrome://settings). This may open in a window
    // or a tab depending on feature SplitSettings.
    if (url.host() == chrome::kChromeUISettingsHost) {
      std::string sub_page = GetPathAndQuery(url);
      chrome::ShowSettingsSubPageForProfile(profile, sub_page);
      return nullptr;
    }
    // OS settings are shown in a window.
    if (url.host() == chrome::kChromeUIOSSettingsHost) {
      std::string sub_page = GetPathAndQuery(url);
      chrome::SettingsWindowManager::GetInstance()->ShowOSSettings(profile,
                                                                   sub_page);
      return nullptr;
    }
  }

  NavigateParams navigate_params(
      profile, url,
      ui::PageTransitionFromInt(ui::PAGE_TRANSITION_LINK |
                                ui::PAGE_TRANSITION_FROM_API));

  if (from_user_interaction)
    navigate_params.was_activated = content::WasActivatedOption::kYes;

  Navigate(&navigate_params);

  if (navigate_params.browser) {
    // The browser window might be on another user's desktop, and hence not
    // visible. Ensure the browser becomes visible on this user's desktop.
    multi_user_util::MoveWindowToCurrentDesktop(
        navigate_params.browser->window()->GetNativeWindow());
  }
  return navigate_params.navigated_or_inserted_contents;
}
