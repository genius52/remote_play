// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_AUDIO_SERVICE_FACTORY_H_
#define SERVICES_AUDIO_SERVICE_FACTORY_H_

#include <memory>

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "services/audio/service.h"
#include "services/service_manager/public/cpp/binder_map.h"
#include "services/service_manager/public/mojom/service.mojom.h"

namespace media {
class AudioManager;
}  // namespace media

namespace audio {

// Creates an instance of Audio service which will live in the current process
// on top of AudioManager instance belonging to that process. Must be called on
// the device thread of AudioManager.
std::unique_ptr<Service> CreateEmbeddedService(
    media::AudioManager* audio_manager,
    mojo::PendingReceiver<service_manager::mojom::Service> receiver);

// Creates an instance of Audio service which will live in the current process
// and will create and own an AudioManager instance. |extra_binders| can provide
// additional interface binders for the service to include. Useful for e.g.
// test-only environments.
std::unique_ptr<Service> CreateStandaloneService(
    std::unique_ptr<service_manager::BinderMap> extra_binders,
    mojo::PendingReceiver<service_manager::mojom::Service> receiver);

}  // namespace audio

#endif  // SERVICES_AUDIO_SERVICE_FACTORY_H_
