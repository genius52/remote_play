// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/printing_handler.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "cef/libcef/features/features.h"
#include "content/public/browser/web_ui.h"

#if !BUILDFLAG(ENABLE_CEF)
#include "chrome/browser/printing/printer_manager_dialog.h"
#endif

namespace settings {

PrintingHandler::PrintingHandler() {}

PrintingHandler::~PrintingHandler() {}

void PrintingHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "openSystemPrintDialog",
      base::BindRepeating(&PrintingHandler::HandleOpenSystemPrintDialog,
                          base::Unretained(this)));
}

void PrintingHandler::OnJavascriptAllowed() {}

void PrintingHandler::OnJavascriptDisallowed() {}

void PrintingHandler::HandleOpenSystemPrintDialog(const base::ListValue* args) {
#if !BUILDFLAG(ENABLE_CEF)
  printing::PrinterManagerDialog::ShowPrinterManagerDialog();
#else
  NOTIMPLEMENTED();
#endif
}

}  // namespace settings
