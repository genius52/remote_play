// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/welcome/nux/set_as_default_handler.h"

namespace nux {

SetAsDefaultHandler::SetAsDefaultHandler()
    : settings::DefaultBrowserHandler() {}

SetAsDefaultHandler::~SetAsDefaultHandler() {}

void SetAsDefaultHandler::RecordSetAsDefaultUMA() {
  // TODO(hcarmona): Add UMA tracking.
}

}  // namespace nux
