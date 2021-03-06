// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_API_DIAGNOSTICS_DIAGNOSTICS_API_H_
#define EXTENSIONS_BROWSER_API_DIAGNOSTICS_DIAGNOSTICS_API_H_

#include <memory>
#include <string>

#include "base/optional.h"
#include "extensions/browser/extension_function.h"
#include "extensions/common/api/diagnostics.h"

namespace extensions {

class DiagnosticsSendPacketFunction : public UIThreadExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("diagnostics.sendPacket", DIAGNOSTICS_SENDPACKET)

  DiagnosticsSendPacketFunction();

 protected:
  ~DiagnosticsSendPacketFunction() override;

  // UIThreadExtensionFunction:
  ResponseAction Run() override;

 private:
  void OnTestICMPCompleted(base::Optional<std::string> status);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_API_DIAGNOSTICS_DIAGNOSTICS_API_H_
