// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_CHROME_WEB_DIALOG_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_CHROME_WEB_DIALOG_VIEW_H_

#include "ui/gfx/native_widget_types.h"
#include "ui/views/widget/widget.h"

namespace content {
class BrowserContext;
}

namespace ui {
class WebDialogDelegate;
}

namespace chrome {

// Views specific implementation allows extra InitParams.
gfx::NativeWindow ShowWebDialogWithParams(
    gfx::NativeView parent,
    content::BrowserContext* context,
    ui::WebDialogDelegate* delegate,
    const views::Widget::InitParams* extra_params);

}  // namespace chrome

#endif  // CHROME_BROWSER_UI_VIEWS_CHROME_WEB_DIALOG_VIEW_H_
