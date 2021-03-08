// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_SERVICES_MEDIA_GALLERY_UTIL_MEDIA_GALLERY_UTIL_SERVICE_H_
#define CHROME_SERVICES_MEDIA_GALLERY_UTIL_MEDIA_GALLERY_UTIL_SERVICE_H_

#include <memory>

#include "services/service_manager/public/cpp/binder_registry.h"
#include "services/service_manager/public/cpp/service.h"
#include "services/service_manager/public/cpp/service_binding.h"
#include "services/service_manager/public/cpp/service_keepalive.h"
#include "services/service_manager/public/mojom/service.mojom.h"

class MediaGalleryUtilService : public service_manager::Service {
 public:
  explicit MediaGalleryUtilService(
      service_manager::mojom::ServiceRequest request);
  ~MediaGalleryUtilService() override;

  // Lifescycle events that occur after the service has started to spinup.
  void OnStart() override;
  void OnBindInterface(const service_manager::BindSourceInfo& source_info,
                       const std::string& interface_name,
                       mojo::ScopedMessagePipeHandle interface_pipe) override;

 private:
  service_manager::ServiceBinding service_binding_;
  service_manager::ServiceKeepalive service_keepalive_;
  service_manager::BinderRegistry registry_;

  DISALLOW_COPY_AND_ASSIGN(MediaGalleryUtilService);
};

#endif  // CHROME_SERVICES_MEDIA_GALLERY_UTIL_MEDIA_GALLERY_UTIL_SERVICE_H_
