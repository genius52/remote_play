// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_VIZ_SERVICE_H_
#define SERVICES_VIZ_SERVICE_H_

#include "gpu/ipc/service/gpu_init.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "services/service_manager/public/cpp/service.h"
#include "services/service_manager/public/cpp/service_binding.h"
#include "services/service_manager/public/mojom/service.mojom.h"
#include "services/viz/privileged/interfaces/viz_main.mojom.h"

namespace viz {

class VizMainImpl;

class Service : public service_manager::Service, public gpu::GpuSandboxHelper {
 public:
  explicit Service(service_manager::mojom::ServiceRequest request);
  ~Service() override;

 private:
  void BindVizMainRequest(mojom::VizMainRequest request);

  // service_manager::Service:
  void OnStart() override;
  void OnBindInterface(const service_manager::BindSourceInfo& source_info,
                       const std::string& interface_name,
                       mojo::ScopedMessagePipeHandle interface_pipe) override;

  // gpu::GpuSandboxHelper:
  void PreSandboxStartup() override;
  bool EnsureSandboxInitialized(gpu::GpuWatchdogThread* watchdog_thread,
                                const gpu::GPUInfo* gpu_info,
                                const gpu::GpuPreferences& gpu_prefs) override;

  service_manager::ServiceBinding service_binding_;
  service_manager::BinderRegistry registry_;

  std::unique_ptr<VizMainImpl> viz_main_;

  DISALLOW_COPY_AND_ASSIGN(Service);
};

}  // namespace viz

#endif  // SERVICES_VIZ_SERVICE_H_
