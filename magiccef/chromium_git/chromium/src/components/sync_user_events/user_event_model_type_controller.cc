// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync_user_events/user_event_model_type_controller.h"

#include <utility>

#include "base/bind.h"
#include "components/sync/driver/sync_auth_util.h"
#include "components/sync/driver/sync_service.h"
#include "components/sync/driver/sync_user_settings.h"
#include "google_apis/gaia/google_service_auth_error.h"

namespace syncer {

UserEventModelTypeController::UserEventModelTypeController(
    SyncService* sync_service,
    std::unique_ptr<ModelTypeControllerDelegate> delegate_on_disk)
    : ModelTypeController(syncer::USER_EVENTS, std::move(delegate_on_disk)),
      sync_service_(sync_service) {
  DCHECK(sync_service_);
  sync_service_->AddObserver(this);
}

UserEventModelTypeController::~UserEventModelTypeController() {
  sync_service_->RemoveObserver(this);
}

bool UserEventModelTypeController::ReadyForStart() const {
  // TODO(crbug.com/906995): Remove the syncer::IsWebSignout() check once we
  // stop sync in this state. Also remove the "+google_apis/gaia" include
  // dependency and the "//google_apis" build dependency of sync_user_events.
  return !sync_service_->GetUserSettings()->IsUsingSecondaryPassphrase() &&
         !syncer::IsWebSignout(sync_service_->GetAuthError());
}

void UserEventModelTypeController::OnStateChanged(syncer::SyncService* sync) {
  sync->ReadyForStartChanged(type());
}

}  // namespace syncer
