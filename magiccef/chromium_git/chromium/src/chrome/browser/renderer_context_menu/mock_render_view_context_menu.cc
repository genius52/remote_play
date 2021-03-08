// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_context_menu/mock_render_view_context_menu.h"

#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/testing_profile.h"
#include "components/prefs/pref_service.h"
#include "components/renderer_context_menu/render_view_context_menu_observer.h"
#include "content/public/browser/browser_context.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"

MockRenderViewContextMenu::MockMenuItem::MockMenuItem()
    : command_id(0), enabled(false), checked(false), hidden(true) {}

MockRenderViewContextMenu::MockMenuItem::MockMenuItem(
    const MockMenuItem& other) = default;

MockRenderViewContextMenu::MockMenuItem::~MockMenuItem() {}

MockRenderViewContextMenu::MockMenuItem&
MockRenderViewContextMenu::MockMenuItem::operator=(const MockMenuItem& other) =
    default;

MockRenderViewContextMenu::MockRenderViewContextMenu(bool incognito)
    : observer_(nullptr),
      original_profile_(TestingProfile::Builder().Build()),
      profile_(incognito ? original_profile_->GetOffTheRecordProfile()
                         : original_profile_.get()) {}

MockRenderViewContextMenu::~MockRenderViewContextMenu() {}

bool MockRenderViewContextMenu::IsCommandIdChecked(int command_id) const {
  return observer_->IsCommandIdChecked(command_id);
}

bool MockRenderViewContextMenu::IsCommandIdEnabled(int command_id) const {
  return observer_->IsCommandIdEnabled(command_id);
}

void MockRenderViewContextMenu::ExecuteCommand(int command_id,
                                               int event_flags) {
  observer_->ExecuteCommand(command_id);
}

void MockRenderViewContextMenu::AddMenuItem(int command_id,
                                            const base::string16& title) {
  MockMenuItem item;
  item.command_id = command_id;
  item.enabled = observer_->IsCommandIdEnabled(command_id);
  item.checked = false;
  item.hidden = false;
  item.title = title;
  items_.push_back(item);
}

void MockRenderViewContextMenu::AddCheckItem(int command_id,
                                             const base::string16& title) {
  MockMenuItem item;
  item.command_id = command_id;
  item.enabled = observer_->IsCommandIdEnabled(command_id);
  item.checked = observer_->IsCommandIdChecked(command_id);
  item.hidden = false;
  item.title = title;
  items_.push_back(item);
}

void MockRenderViewContextMenu::AddSeparator() {
  MockMenuItem item;
  item.command_id = -1;
  item.enabled = false;
  item.checked = false;
  item.hidden = false;
  items_.push_back(item);
}

void MockRenderViewContextMenu::AddSubMenu(int command_id,
                                           const base::string16& label,
                                           ui::MenuModel* model) {
  MockMenuItem item;
  item.command_id = command_id;
  item.enabled = observer_->IsCommandIdEnabled(command_id);
  item.checked = observer_->IsCommandIdChecked(command_id);
  item.hidden = false;
  item.title = label;
  items_.push_back(item);

  // Add items in the submenu |model| to |items_| so that the items can be
  // updated later via the RenderViewContextMenuProxy interface.
  // NOTE: this is a hack for the mock class. Ideally, RVCMProxy should neither
  // know (directly) about submenu items nor it should be responsible to update
  // them. This works in non-mock because of toolkit_delegate_ in RVCMBase.
  // TODO(yusukes,lazyboy): This is a hack. RVCMProxy should neither directly
  // know about but submenu items nor it should update them.
  for (int i = 0; i < model->GetItemCount(); ++i) {
    MockMenuItem sub_item;
    sub_item.command_id = model->GetCommandIdAt(i);
    sub_item.enabled = model->IsEnabledAt(i);
    sub_item.checked = model->IsItemCheckedAt(i);
    sub_item.hidden = false;
    sub_item.title = model->GetSublabelAt(i);
    items_.push_back(sub_item);
  }
}

void MockRenderViewContextMenu::UpdateMenuItem(int command_id,
                                               bool enabled,
                                               bool hidden,
                                               const base::string16& title) {
  for (auto& item : items_) {
    if (item.command_id == command_id) {
      item.enabled = enabled;
      item.hidden = hidden;
      item.title = title;
      return;
    }
  }

  FAIL() << "Menu observer is trying to change a menu item it doesn't own."
         << " command_id: " << command_id;
}

void MockRenderViewContextMenu::UpdateMenuIcon(int command_id,
                                               const gfx::Image& image) {
  for (auto& item : items_) {
    if (item.command_id == command_id) {
      item.icon = image;
      return;
    }
  }

  FAIL() << "Menu observer is trying to change a menu item it doesn't own."
         << " command_id: " << command_id;
}

void MockRenderViewContextMenu::RemoveMenuItem(int command_id) {}

void MockRenderViewContextMenu::RemoveAdjacentSeparators() {}

void MockRenderViewContextMenu::AddSpellCheckServiceItem(bool is_checked) {
  AddCheckItem(
      IDC_CONTENT_CONTEXT_SPELLING_TOGGLE,
      l10n_util::GetStringUTF16(IDS_CONTENT_CONTEXT_SPELLING_ASK_GOOGLE));
}

void MockRenderViewContextMenu::AddAccessibilityLabelsServiceItem(
    bool is_checked) {
  // TODO(katie): Is there a way not to repeat this logic from
  // render_view_context_menu.cc?
  if (is_checked) {
    AddCheckItem(IDC_CONTENT_CONTEXT_ACCESSIBILITY_LABELS_TOGGLE,
                 l10n_util::GetStringUTF16(
                     IDS_CONTENT_CONTEXT_ACCESSIBILITY_LABELS_MENU_OPTION));
  } else {
    ui::SimpleMenuModel accessibility_labels_submenu_model_(this);
    accessibility_labels_submenu_model_.AddItemWithStringId(
        IDC_CONTENT_CONTEXT_ACCESSIBILITY_LABELS_TOGGLE,
        IDS_CONTENT_CONTEXT_ACCESSIBILITY_LABELS_SEND);
    accessibility_labels_submenu_model_.AddItemWithStringId(
        IDC_CONTENT_CONTEXT_ACCESSIBILITY_LABELS_TOGGLE_ONCE,
        IDS_CONTENT_CONTEXT_ACCESSIBILITY_LABELS_SEND_ONCE);
    AddSubMenu(IDC_CONTENT_CONTEXT_ACCESSIBILITY_LABELS,
               l10n_util::GetStringUTF16(
                   IDS_CONTENT_CONTEXT_ACCESSIBILITY_LABELS_MENU_OPTION),
               &accessibility_labels_submenu_model_);
  }
}

content::RenderViewHost* MockRenderViewContextMenu::GetRenderViewHost() const {
  return nullptr;
}

content::BrowserContext* MockRenderViewContextMenu::GetBrowserContext() const {
  return profile_;
}

content::WebContents* MockRenderViewContextMenu::GetWebContents() const {
  return nullptr;
}

void MockRenderViewContextMenu::SetObserver(
    RenderViewContextMenuObserver* observer) {
  observer_ = observer;
}

size_t MockRenderViewContextMenu::GetMenuSize() const {
  return items_.size();
}

bool MockRenderViewContextMenu::GetMenuItem(size_t index,
                                            MockMenuItem* item) const {
  if (index >= items_.size())
    return false;
  *item = items_[index];
  return true;
}

PrefService* MockRenderViewContextMenu::GetPrefs() {
  return profile_->GetPrefs();
}
