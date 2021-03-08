// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/media_session/audio_focus_manager.h"

#include <iterator>
#include <utility>

#include "base/bind.h"
#include "base/containers/adapters.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/unguessable_token.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "services/media_session/audio_focus_manager_metrics_helper.h"
#include "services/media_session/audio_focus_request.h"
#include "services/media_session/public/cpp/features.h"
#include "services/media_session/public/mojom/audio_focus.mojom.h"

namespace media_session {

namespace {

mojom::EnforcementMode GetDefaultEnforcementMode() {
  if (base::FeatureList::IsEnabled(features::kAudioFocusEnforcement)) {
    if (base::FeatureList::IsEnabled(features::kAudioFocusSessionGrouping))
      return mojom::EnforcementMode::kSingleGroup;
    return mojom::EnforcementMode::kSingleSession;
  }

  return mojom::EnforcementMode::kNone;
}

}  // namespace

void AudioFocusManager::RequestAudioFocus(
    mojom::AudioFocusRequestClientRequest request,
    mojom::MediaSessionPtr media_session,
    mojom::MediaSessionInfoPtr session_info,
    mojom::AudioFocusType type,
    RequestAudioFocusCallback callback) {
  RequestGroupedAudioFocus(
      std::move(request), std::move(media_session), std::move(session_info),
      type, base::UnguessableToken::Create(), std::move(callback));
}

void AudioFocusManager::RequestGroupedAudioFocus(
    mojom::AudioFocusRequestClientRequest request,
    mojom::MediaSessionPtr media_session,
    mojom::MediaSessionInfoPtr session_info,
    mojom::AudioFocusType type,
    const base::UnguessableToken& group_id,
    RequestGroupedAudioFocusCallback callback) {
  base::UnguessableToken request_id = base::UnguessableToken::Create();

  RequestAudioFocusInternal(
      std::make_unique<AudioFocusRequest>(
          this, std::move(request), std::move(media_session),
          std::move(session_info), type, request_id, GetBindingSourceName(),
          group_id),
      type);

  std::move(callback).Run(request_id);
}

void AudioFocusManager::GetFocusRequests(GetFocusRequestsCallback callback) {
  std::vector<mojom::AudioFocusRequestStatePtr> requests;

  for (const auto& row : audio_focus_stack_)
    requests.push_back(row->ToAudioFocusRequestState());

  std::move(callback).Run(std::move(requests));
}

void AudioFocusManager::GetDebugInfoForRequest(
    const RequestId& request_id,
    GetDebugInfoForRequestCallback callback) {
  for (auto& row : audio_focus_stack_) {
    if (row->id() != request_id)
      continue;

    row->ipc()->GetDebugInfo(base::BindOnce(
        [](const base::UnguessableToken& group_id,
           GetDebugInfoForRequestCallback callback,
           mojom::MediaSessionDebugInfoPtr info) {
          // Inject the |group_id| into the state string. This is because in
          // some cases the group id is automatically generated by the media
          // session service so the session is unaware of it.
          if (!info->state.empty())
            info->state += " ";
          info->state += "GroupId=" + group_id.ToString();

          std::move(callback).Run(std::move(info));
        },
        row->group_id(), std::move(callback)));
    return;
  }

  std::move(callback).Run(mojom::MediaSessionDebugInfo::New());
}

void AudioFocusManager::AbandonAudioFocusInternal(RequestId id) {
  if (audio_focus_stack_.empty())
    return;

  bool was_top_most_session = audio_focus_stack_.back()->id() == id;

  auto row = RemoveFocusEntryIfPresent(id);
  if (!row)
    return;

  EnforceAudioFocus();
  MaybeUpdateActiveSession();

  // Notify observers that we lost audio focus.
  mojom::AudioFocusRequestStatePtr session_state =
      row->ToAudioFocusRequestState();
  observers_.ForAllPtrs([&session_state](mojom::AudioFocusObserver* observer) {
    observer->OnFocusLost(session_state.Clone());
  });

  if (!was_top_most_session || audio_focus_stack_.empty())
    return;

  // Notify observers that the session on top gained focus.
  AudioFocusRequest* new_session = audio_focus_stack_.back().get();
  observers_.ForAllPtrs([&new_session](mojom::AudioFocusObserver* observer) {
    observer->OnFocusGained(new_session->ToAudioFocusRequestState());
  });
}

void AudioFocusManager::AddObserver(mojom::AudioFocusObserverPtr observer) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  observers_.AddPtr(std::move(observer));
}

void AudioFocusManager::SetSourceName(const std::string& name) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  bindings_.dispatch_context()->source_name = name;
}

void AudioFocusManager::SetEnforcementMode(mojom::EnforcementMode mode) {
  if (mode == mojom::EnforcementMode::kDefault)
    mode = GetDefaultEnforcementMode();

  if (mode == enforcement_mode_)
    return;

  enforcement_mode_ = mode;

  if (audio_focus_stack_.empty())
    return;

  EnforceAudioFocus();
}

void AudioFocusManager::CreateActiveMediaController(
    mojom::MediaControllerRequest request) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  active_media_controller_.BindToInterface(std::move(request));
}

void AudioFocusManager::CreateMediaControllerForSession(
    mojom::MediaControllerRequest request,
    const base::UnguessableToken& request_id) {
  for (auto& row : audio_focus_stack_) {
    if (row->id() != request_id)
      continue;

    row->BindToMediaController(std::move(request));
    break;
  }
}

void AudioFocusManager::SuspendAllSessions() {
  for (auto& row : audio_focus_stack_)
    row->ipc()->Suspend(mojom::MediaSession::SuspendType::kUI);
}

void AudioFocusManager::BindToInterface(
    mojom::AudioFocusManagerRequest request) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  bindings_.AddBinding(this, std::move(request),
                       std::make_unique<BindingContext>());
}

void AudioFocusManager::BindToDebugInterface(
    mojom::AudioFocusManagerDebugRequest request) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  debug_bindings_.AddBinding(this, std::move(request));
}

void AudioFocusManager::BindToControllerManagerInterface(
    mojom::MediaControllerManagerRequest request) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  controller_bindings_.AddBinding(this, std::move(request));
}

void AudioFocusManager::RequestAudioFocusInternal(
    std::unique_ptr<AudioFocusRequest> row,
    mojom::AudioFocusType type) {
  row->set_audio_focus_type(type);
  audio_focus_stack_.push_back(std::move(row));

  EnforceAudioFocus();
  MaybeUpdateActiveSession();

  // Notify observers that we were gained audio focus.
  mojom::AudioFocusRequestStatePtr session_state =
      audio_focus_stack_.back()->ToAudioFocusRequestState();
  observers_.ForAllPtrs([&session_state](mojom::AudioFocusObserver* observer) {
    observer->OnFocusGained(session_state.Clone());
  });
}

void AudioFocusManager::EnforceAudioFocus() {
  DCHECK_NE(mojom::EnforcementMode::kDefault, enforcement_mode_);
  if (audio_focus_stack_.empty())
    return;

  EnforcementState state;

  for (auto& session : base::Reversed(audio_focus_stack_)) {
    EnforceSingleSession(session.get(), state);

    // Update the flags based on the audio focus type of this session. If the
    // session is suspended then any transient audio focus type should not have
    // an effect.
    switch (session->audio_focus_type()) {
      case mojom::AudioFocusType::kGain:
        state.should_stop = true;
        break;
      case mojom::AudioFocusType::kGainTransient:
        if (!session->IsSuspended())
          state.should_suspend = true;
        break;
      case mojom::AudioFocusType::kGainTransientMayDuck:
        if (!session->IsSuspended())
          state.should_duck = true;
        break;
      case mojom::AudioFocusType::kAmbient:
        break;
    }
  }
}

void AudioFocusManager::MaybeUpdateActiveSession() {
  AudioFocusRequest* active = nullptr;

  for (auto& row : base::Reversed(audio_focus_stack_)) {
    if (!row->info()->is_controllable)
      continue;

    active = row.get();
    break;
  }

  if (active) {
    active_media_controller_.SetMediaSession(active);
  } else {
    active_media_controller_.ClearMediaSession();
  }
}

AudioFocusManager::AudioFocusManager()
    : enforcement_mode_(GetDefaultEnforcementMode()) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
}

AudioFocusManager::~AudioFocusManager() = default;

std::unique_ptr<AudioFocusRequest> AudioFocusManager::RemoveFocusEntryIfPresent(
    RequestId id) {
  std::unique_ptr<AudioFocusRequest> row;

  for (auto iter = audio_focus_stack_.begin(); iter != audio_focus_stack_.end();
       ++iter) {
    if ((*iter)->id() == id) {
      row.swap((*iter));
      audio_focus_stack_.erase(iter);
      break;
    }
  }

  return row;
}

const std::string& AudioFocusManager::GetBindingSourceName() const {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return bindings_.dispatch_context()->source_name;
}

bool AudioFocusManager::IsSessionOnTopOfAudioFocusStack(
    RequestId id,
    mojom::AudioFocusType type) const {
  return !audio_focus_stack_.empty() && audio_focus_stack_.back()->id() == id &&
         audio_focus_stack_.back()->audio_focus_type() == type;
}

bool AudioFocusManager::ShouldSessionBeSuspended(
    const AudioFocusRequest* session,
    const EnforcementState& state) const {
  bool should_suspend_any = state.should_stop || state.should_suspend;

  switch (enforcement_mode_) {
    case mojom::EnforcementMode::kSingleSession:
      return should_suspend_any;
    case mojom::EnforcementMode::kSingleGroup:
      return should_suspend_any &&
             session->group_id() != audio_focus_stack_.back()->group_id();
    case mojom::EnforcementMode::kNone:
      return false;
    case mojom::EnforcementMode::kDefault:
      NOTIMPLEMENTED();
      return false;
  }
}

bool AudioFocusManager::ShouldSessionBeDucked(
    const AudioFocusRequest* session,
    const EnforcementState& state) const {
  switch (enforcement_mode_) {
    case mojom::EnforcementMode::kSingleSession:
    case mojom::EnforcementMode::kSingleGroup:
      if (session->info()->force_duck)
        return state.should_duck || ShouldSessionBeSuspended(session, state);
      return state.should_duck;
    case mojom::EnforcementMode::kNone:
      return false;
    case mojom::EnforcementMode::kDefault:
      NOTIMPLEMENTED();
      return false;
  }
}

void AudioFocusManager::EnforceSingleSession(AudioFocusRequest* session,
                                             const EnforcementState& state) {
  if (ShouldSessionBeDucked(session, state)) {
    session->ipc()->StartDucking();
  } else {
    session->ipc()->StopDucking();
  }

  // If the session wants to be ducked instead of suspended we should stop now.
  if (session->info()->force_duck)
    return;

  if (ShouldSessionBeSuspended(session, state)) {
    session->Suspend(state);
  } else {
    session->MaybeResume();
  }
}

}  // namespace media_session
