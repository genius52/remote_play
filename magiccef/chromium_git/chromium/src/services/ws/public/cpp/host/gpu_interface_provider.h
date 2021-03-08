// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_WS_PUBLIC_CPP_HOST_GPU_INTERFACE_PROVIDER_H_
#define SERVICES_WS_PUBLIC_CPP_HOST_GPU_INTERFACE_PROVIDER_H_

#include <string>

#include "services/service_manager/public/cpp/binder_registry.h"

#if defined(USE_OZONE)
#include "mojo/public/cpp/system/message_pipe.h"
#endif

namespace ws {

// GpuInterfaceProvider is responsible for providing the Gpu related interfaces.
// The implementation of these varies depending upon where the WindowService is
// hosted.
class GpuInterfaceProvider {
 public:
  virtual ~GpuInterfaceProvider() {}

  // Registers the gpu-related interfaces, specifically
  // discardable_memory::mojom::DiscardableSharedMemoryManagerRequest and
  // mojom::GpuRequest.
  virtual void RegisterGpuInterfaces(
      service_manager::BinderRegistry* registry) = 0;

#if defined(USE_OZONE)
  // Binds a gpu-related interface needed by Ozone.
  virtual void BindOzoneGpuInterface(const std::string& interface_name,
                                     mojo::ScopedMessagePipeHandle handle) = 0;
#endif
};

}  // namespace ws

#endif  // SERVICES_WS_PUBLIC_CPP_HOST_GPU_INTERFACE_PROVIDER_H_
