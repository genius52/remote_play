// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SERIAL_SERIAL_SERVICE_H_
#define CONTENT_BROWSER_SERIAL_SERIAL_SERVICE_H_

#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "services/device/public/mojom/serial.mojom.h"
#include "third_party/blink/public/mojom/serial/serial.mojom.h"

namespace content {

class RenderFrameHost;
class SerialChooser;

class SerialService : public blink::mojom::SerialService,
                      public device::mojom::SerialPortConnectionWatcher {
 public:
  explicit SerialService(RenderFrameHost* render_frame_host);
  ~SerialService() override;

  void Bind(blink::mojom::SerialServiceRequest request);

  // SerialService implementation
  void GetPorts(GetPortsCallback callback) override;
  void RequestPort(std::vector<blink::mojom::SerialPortFilterPtr> filters,
                   RequestPortCallback callback) override;
  void GetPort(const base::UnguessableToken& token,
               device::mojom::SerialPortRequest request) override;

 private:
  void FinishGetPorts(GetPortsCallback callback,
                      std::vector<device::mojom::SerialPortInfoPtr> ports);
  void FinishRequestPort(RequestPortCallback callback,
                         device::mojom::SerialPortInfoPtr port);
  void OnWatcherConnectionError();
  void DecrementActiveFrameCount();

  RenderFrameHost* const render_frame_host_;
  mojo::BindingSet<blink::mojom::SerialService> bindings_;

  // The last shown serial port chooser UI.
  std::unique_ptr<SerialChooser> chooser_;

  // Each pipe here watches a connection created by GetPort() in order to notify
  // the WebContentsImpl when an active connection indicator should be shown.
  mojo::BindingSet<device::mojom::SerialPortConnectionWatcher> watchers_;

  base::WeakPtrFactory<SerialService> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(SerialService);
};

}  // namespace content

#endif  // CONTENT_BROWSER_SERIAL_SERIAL_SERVICE_H_
