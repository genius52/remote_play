// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/menu/menu_model_adapter.h"

#include <utility>

#include "base/logging.h"
#include "ui/base/models/menu_model.h"
#include "ui/gfx/image/image.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/submenu_view.h"

namespace views {

MenuModelAdapter::MenuModelAdapter(ui::MenuModel* menu_model)
    : MenuModelAdapter(menu_model, base::RepeatingClosure() /*null callback*/) {
}

MenuModelAdapter::MenuModelAdapter(
    ui::MenuModel* menu_model,
    base::RepeatingClosure on_menu_closed_callback)
    : menu_model_(menu_model),
      triggerable_event_flags_(ui::EF_LEFT_MOUSE_BUTTON |
                               ui::EF_RIGHT_MOUSE_BUTTON),
      on_menu_closed_callback_(std::move(on_menu_closed_callback)) {
  DCHECK(menu_model);
  menu_model_->SetMenuModelDelegate(nullptr);
  menu_model_->SetMenuModelDelegate(this);
}

MenuModelAdapter::~MenuModelAdapter() {
  if (menu_model_)
    menu_model_->SetMenuModelDelegate(nullptr);
}

void MenuModelAdapter::BuildMenu(MenuItemView* menu) {
  DCHECK(menu);

  // Clear the menu.
  if (menu->HasSubmenu())
    menu->RemoveAllMenuItems();

  // Leave entries in the map if the menu is being shown.  This
  // allows the map to find the menu model of submenus being closed
  // so ui::MenuModel::MenuClosed() can be called.
  if (!menu->GetMenuController())
    menu_map_.clear();
  menu_map_[menu] = menu_model_;

  // Repopulate the menu.
  BuildMenuImpl(menu, menu_model_);
  menu->ChildrenChanged();
}

MenuItemView* MenuModelAdapter::CreateMenu() {
  menu_ = new MenuItemView(this);
  BuildMenu(menu_);
  return menu_;
}

// Static.
MenuItemView* MenuModelAdapter::AddMenuItemFromModelAt(ui::MenuModel* model,
                                                       int model_index,
                                                       MenuItemView* menu,
                                                       int menu_index,
                                                       int item_id) {
  base::Optional<MenuItemView::Type> type;
  ui::MenuModel::ItemType menu_type = model->GetTypeAt(model_index);
  switch (menu_type) {
    case ui::MenuModel::TYPE_COMMAND:
    case ui::MenuModel::TYPE_BUTTON_ITEM:
      type = MenuItemView::NORMAL;
      break;
    case ui::MenuModel::TYPE_CHECK:
      type = MenuItemView::CHECKBOX;
      break;
    case ui::MenuModel::TYPE_RADIO:
      type = MenuItemView::RADIO;
      break;
    case ui::MenuModel::TYPE_SEPARATOR:
      type = MenuItemView::SEPARATOR;
      break;
    case ui::MenuModel::TYPE_SUBMENU:
      type = MenuItemView::SUBMENU;
      break;
    case ui::MenuModel::TYPE_ACTIONABLE_SUBMENU:
      type = MenuItemView::ACTIONABLE_SUBMENU;
      break;
    case ui::MenuModel::TYPE_HIGHLIGHTED:
      type = MenuItemView::HIGHLIGHTED;
      break;
  }

  if (*type == MenuItemView::SEPARATOR) {
    return menu->AddMenuItemAt(menu_index, item_id, base::string16(),
                               base::string16(), base::string16(), nullptr,
                               gfx::ImageSkia(), *type,
                               model->GetSeparatorTypeAt(model_index));
  }

  gfx::Image icon;
  model->GetIconAt(model_index, &icon);
  return menu->AddMenuItemAt(
      menu_index, item_id, model->GetLabelAt(model_index),
      model->GetSublabelAt(model_index), model->GetMinorTextAt(model_index),
      model->GetMinorIconAt(model_index),
      icon.IsEmpty() ? gfx::ImageSkia() : *icon.ToImageSkia(), *type,
      ui::NORMAL_SEPARATOR);
}

// Static.
MenuItemView* MenuModelAdapter::AppendMenuItemFromModel(ui::MenuModel* model,
                                                        int model_index,
                                                        MenuItemView* menu,
                                                        int item_id) {
  const int menu_index =
      menu->HasSubmenu() ? int{menu->GetSubmenu()->children().size()} : 0;
  return AddMenuItemFromModelAt(model, model_index, menu, menu_index, item_id);
}


MenuItemView* MenuModelAdapter::AppendMenuItem(MenuItemView* menu,
                                               ui::MenuModel* model,
                                               int index) {
  return AppendMenuItemFromModel(model, index, menu,
                                 model->GetCommandIdAt(index));
}

// MenuModelAdapter, MenuDelegate implementation:

void MenuModelAdapter::ExecuteCommand(int id) {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index)) {
    model->ActivatedAt(index);
    return;
  }

  NOTREACHED();
}

void MenuModelAdapter::ExecuteCommand(int id, int mouse_event_flags) {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index)) {
    model->ActivatedAt(index, mouse_event_flags);
    return;
  }

  NOTREACHED();
}

bool MenuModelAdapter::IsTriggerableEvent(MenuItemView* source,
                                          const ui::Event& e) {
  return e.type() == ui::ET_GESTURE_TAP ||
         e.type() == ui::ET_GESTURE_TAP_DOWN ||
         (e.IsMouseEvent() && (triggerable_event_flags_ & e.flags()) != 0);
}

bool MenuModelAdapter::GetAccelerator(int id,
                                      ui::Accelerator* accelerator) const {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index))
    return model->GetAcceleratorAt(index, accelerator);

  NOTREACHED();
  return false;
}

base::string16 MenuModelAdapter::GetLabel(int id) const {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index))
    return model->GetLabelAt(index);

  NOTREACHED();
  return base::string16();
}

void MenuModelAdapter::GetLabelStyle(int id, LabelStyle* style) const {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index)) {
    const gfx::FontList* font_list = model->GetLabelFontListAt(index);
    if (font_list) {
      style->font_list = *font_list;
      return;
    }
  }

  // This line may be reached for the empty menu item.
  return MenuDelegate::GetLabelStyle(id, style);
}

bool MenuModelAdapter::IsCommandEnabled(int id) const {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index))
    return model->IsEnabledAt(index);

  NOTREACHED();
  return false;
}

bool MenuModelAdapter::IsCommandVisible(int id) const {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index))
    return model->IsVisibleAt(index);

  NOTREACHED();
  return false;
}

bool MenuModelAdapter::IsItemChecked(int id) const {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(id, &model, &index))
    return model->IsItemCheckedAt(index);

  NOTREACHED();
  return false;
}

MenuItemView* MenuModelAdapter::GetSiblingMenu(MenuItemView* menu,
                                               const gfx::Point& screen_point,
                                               MenuAnchorPosition* anchor,
                                               bool* has_mnemonics,
                                               MenuButton** button) {
  // Look up the menu model for this menu.
  const std::map<MenuItemView*, ui::MenuModel*>::const_iterator map_iterator =
      menu_map_.find(menu);
  if (map_iterator != menu_map_.end()) {
    map_iterator->second->MouseOutsideMenu(screen_point);
    return nullptr;
  }

  NOTREACHED();
  return nullptr;
}

void MenuModelAdapter::OnUnhandledOpenSubmenu(MenuItemView* menu,
                                              bool is_rtl) {
  // Look up the menu model for this menu.
  const std::map<MenuItemView*, ui::MenuModel*>::const_iterator map_iterator =
      menu_map_.find(menu);
  if (map_iterator != menu_map_.end()) {
    map_iterator->second->UnhandledOpenSubmenu(is_rtl);
    return;
  }

  NOTREACHED();
}

void MenuModelAdapter::OnUnhandledCloseSubmenu(MenuItemView* menu,
                                               bool is_rtl) {
  // Look up the menu model for this menu.
  const std::map<MenuItemView*, ui::MenuModel*>::const_iterator map_iterator =
      menu_map_.find(menu);
  if (map_iterator != menu_map_.end()) {
    map_iterator->second->UnhandledCloseSubmenu(is_rtl);
    return;
  }

  NOTREACHED();
}

bool MenuModelAdapter::GetTextColor(int command_id,
                                    bool is_minor,
                                    bool is_hovered,
                                    SkColor* override_color) const {
  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(command_id, &model, &index))
    return model->GetTextColor(index, is_minor, is_hovered, override_color);

  NOTREACHED();
  return false;
}

bool MenuModelAdapter::GetBackgroundColor(int command_id,
                                          bool is_hovered,
                                          SkColor* override_color) const {
  if (command_id == -1)
    return menu_model_->GetBackgroundColor(-1, is_hovered, override_color);

  ui::MenuModel* model = menu_model_;
  int index = 0;
  if (ui::MenuModel::GetModelAndIndexForCommandId(command_id, &model, &index))
    return model->GetBackgroundColor(index, is_hovered, override_color);

  NOTREACHED();
  return false;
}

void MenuModelAdapter::WillShowMenu(MenuItemView* menu) {
  // Look up the menu model for this menu.
  const std::map<MenuItemView*, ui::MenuModel*>::const_iterator map_iterator =
      menu_map_.find(menu);
  if (map_iterator != menu_map_.end()) {
    map_iterator->second->MenuWillShow();
    return;
  }

  NOTREACHED();
}

void MenuModelAdapter::WillHideMenu(MenuItemView* menu) {
  // Look up the menu model for this menu.
  const std::map<MenuItemView*, ui::MenuModel*>::const_iterator map_iterator =
      menu_map_.find(menu);
  if (map_iterator != menu_map_.end()) {
    map_iterator->second->MenuWillClose();
    return;
  }

  NOTREACHED();
}

void MenuModelAdapter::OnMenuClosed(MenuItemView* menu) {
  if (!on_menu_closed_callback_.is_null())
    on_menu_closed_callback_.Run();
}

// MenuModelDelegate overrides:
void MenuModelAdapter::OnMenuStructureChanged() {
  if (menu_)
    BuildMenu(menu_);
}

void MenuModelAdapter::OnMenuClearingDelegate() {
  menu_model_ = nullptr;
}

// MenuModelAdapter, private:

void MenuModelAdapter::BuildMenuImpl(MenuItemView* menu, ui::MenuModel* model) {
  DCHECK(menu);
  DCHECK(model);
  bool has_icons = model->HasIcons();
  const int item_count = model->GetItemCount();
  for (int i = 0; i < item_count; ++i) {
    MenuItemView* item = AppendMenuItem(menu, model, i);
    if (item) {
      item->SetEnabled(model->IsEnabledAt(i));
      item->SetVisible(model->IsVisibleAt(i));
    }

    if (model->GetTypeAt(i) == ui::MenuModel::TYPE_SUBMENU ||
        model->GetTypeAt(i) == ui::MenuModel::TYPE_ACTIONABLE_SUBMENU) {
      DCHECK(item);
      DCHECK(item->GetType() == MenuItemView::SUBMENU ||
             item->GetType() == MenuItemView::ACTIONABLE_SUBMENU);
      ui::MenuModel* submodel = model->GetSubmenuModelAt(i);
      DCHECK(submodel);
      BuildMenuImpl(item, submodel);
      has_icons = has_icons || item->has_icons();

      menu_map_[item] = submodel;
    }
  }

  menu->set_has_icons(has_icons);
}

}  // namespace views