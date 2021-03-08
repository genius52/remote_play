// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/media_session/audio_focus_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/callback.h"
#include "base/run_loop.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_task_environment.h"
#include "build/build_config.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "services/media_session/audio_focus_manager_metrics_helper.h"
#include "services/media_session/media_session_service.h"
#include "services/media_session/public/cpp/test/audio_focus_test_util.h"
#include "services/media_session/public/cpp/test/mock_media_session.h"
#include "services/media_session/public/mojom/audio_focus.mojom.h"
#include "services/media_session/public/mojom/constants.mojom.h"
#include "services/service_manager/public/cpp/test/test_connector_factory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media_session {

namespace {

const char kExampleSourceName[] = "test";
const char kExampleSourceName2[] = "test2";

}  // anonymous namespace

// This tests the Audio Focus Manager API. The parameter determines whether
// audio focus is enabled or not. If it is not enabled it should track the media
// sessions but not enforce single session focus.
class AudioFocusManagerTest
    : public testing::TestWithParam<mojom::EnforcementMode> {
 public:
  AudioFocusManagerTest() = default;

  void SetUp() override {
    // Create an instance of the MediaSessionService.
    service_ = std::make_unique<MediaSessionService>(
        connector_factory_.RegisterInstance(mojom::kServiceName));
    connector_factory_.GetDefaultConnector()->BindInterface(mojom::kServiceName,
                                                            &audio_focus_ptr_);
    connector_factory_.GetDefaultConnector()->BindInterface(
        mojom::kServiceName, &audio_focus_debug_ptr_);

    audio_focus_ptr_->SetEnforcementMode(GetParam());
    audio_focus_ptr_.FlushForTesting();
  }

  void TearDown() override {
    // Run pending tasks.
    base::RunLoop().RunUntilIdle();
  }

  AudioFocusManager::RequestId GetAudioFocusedSession() {
    const auto audio_focus_requests = GetRequests();
    for (auto iter = audio_focus_requests.rbegin();
         iter != audio_focus_requests.rend(); ++iter) {
      if ((*iter)->audio_focus_type == mojom::AudioFocusType::kGain)
        return (*iter)->request_id.value();
    }
    return base::UnguessableToken::Null();
  }

  int GetTransientCount() {
    return GetCountForType(mojom::AudioFocusType::kGainTransient);
  }

  int GetTransientMaybeDuckCount() {
    return GetCountForType(mojom::AudioFocusType::kGainTransientMayDuck);
  }

  int GetAmbientCount() {
    return GetCountForType(mojom::AudioFocusType::kAmbient);
  }

  void AbandonAudioFocusNoReset(test::MockMediaSession* session) {
    session->audio_focus_request()->AbandonAudioFocus();
    session->FlushForTesting();
    audio_focus_ptr_.FlushForTesting();
  }

  AudioFocusManager::RequestId RequestAudioFocus(
      test::MockMediaSession* session,
      mojom::AudioFocusType audio_focus_type) {
    return session->RequestAudioFocusFromService(audio_focus_ptr_,
                                                 audio_focus_type);
  }

  AudioFocusManager::RequestId RequestGroupedAudioFocus(
      test::MockMediaSession* session,
      mojom::AudioFocusType audio_focus_type,
      const base::UnguessableToken& group_id) {
    return session->RequestGroupedAudioFocusFromService(
        audio_focus_ptr_, audio_focus_type, group_id);
  }

  mojom::MediaSessionDebugInfoPtr GetDebugInfo(
      AudioFocusManager::RequestId request_id) {
    mojom::MediaSessionDebugInfoPtr result;
    base::OnceCallback<void(mojom::MediaSessionDebugInfoPtr)> callback =
        base::BindOnce(
            [](mojom::MediaSessionDebugInfoPtr* out_result,
               mojom::MediaSessionDebugInfoPtr result) {
              *out_result = std::move(result);
            },
            &result);

    GetDebugService()->GetDebugInfoForRequest(request_id, std::move(callback));

    audio_focus_ptr_.FlushForTesting();
    audio_focus_debug_ptr_.FlushForTesting();

    return result;
  }

  mojom::MediaSessionInfo::SessionState GetState(
      test::MockMediaSession* session) {
    mojom::MediaSessionInfo::SessionState state = session->GetState();

    if (!IsEnforcementEnabled()) {
      // If audio focus enforcement is disabled then we should never see ducking
      // in the tests.
      EXPECT_NE(mojom::MediaSessionInfo::SessionState::kDucking, state);
    }

    return state;
  }

  std::unique_ptr<test::TestAudioFocusObserver> CreateObserver() {
    std::unique_ptr<test::TestAudioFocusObserver> observer =
        std::make_unique<test::TestAudioFocusObserver>();

    mojom::AudioFocusObserverPtr observer_ptr;
    observer->BindToMojoRequest(mojo::MakeRequest(&observer_ptr));
    GetService()->AddObserver(std::move(observer_ptr));

    audio_focus_ptr_.FlushForTesting();
    return observer;
  }

  mojom::MediaSessionInfo::SessionState GetStateFromParam(
      mojom::MediaSessionInfo::SessionState state) {
    // If enforcement is enabled then returns the provided state, otherwise
    // returns kActive because without enforcement we did not change state.
    if (IsEnforcementEnabled())
      return state;
    return mojom::MediaSessionInfo::SessionState::kActive;
  }

  void SetSourceName(const std::string& name) {
    GetService()->SetSourceName(name);
    audio_focus_ptr_.FlushForTesting();
  }

  mojom::AudioFocusManagerPtr CreateAudioFocusManagerPtr() {
    mojom::AudioFocusManagerPtr ptr;
    connector_factory_.GetDefaultConnector()->BindInterface(
        mojom::kServiceName, mojo::MakeRequest(&ptr));
    return ptr;
  }

  const std::string GetSourceNameForLastRequest() {
    std::vector<mojom::AudioFocusRequestStatePtr> requests = GetRequests();
    EXPECT_TRUE(requests.back());
    return requests.back()->source_name.value();
  }

  std::unique_ptr<base::HistogramSamples> GetHistogramSamplesSinceTestStart(
      const std::string& name) {
    return histogram_tester_.GetHistogramSamplesSinceCreation(name);
  }

  int GetAudioFocusHistogramCount() {
    return histogram_tester_
        .GetTotalCountsForPrefix("Media.Session.AudioFocus.")
        .size();
  }

  bool IsEnforcementEnabled() const {
#if defined(OS_CHROMEOS)
    // Enforcement is enabled by default on Chrome OS.
    if (GetParam() == mojom::EnforcementMode::kDefault)
      return true;
#endif

    return GetParam() == mojom::EnforcementMode::kSingleSession ||
           GetParam() == mojom::EnforcementMode::kSingleGroup;
  }

  bool IsGroupingEnabled() const {
    return GetParam() != mojom::EnforcementMode::kSingleSession;
  }

 private:
  int GetCountForType(mojom::AudioFocusType type) {
    const auto audio_focus_requests = GetRequests();
    return std::count_if(audio_focus_requests.begin(),
                         audio_focus_requests.end(),
                         [type](const auto& session) {
                           return session->audio_focus_type == type;
                         });
  }

  std::vector<mojom::AudioFocusRequestStatePtr> GetRequests() {
    std::vector<mojom::AudioFocusRequestStatePtr> result;

    GetService()->GetFocusRequests(base::BindOnce(
        [](std::vector<mojom::AudioFocusRequestStatePtr>* out,
           std::vector<mojom::AudioFocusRequestStatePtr> requests) {
          for (auto& request : requests)
            out->push_back(request.Clone());
        },
        &result));

    audio_focus_ptr_.FlushForTesting();
    return result;
  }

  mojom::AudioFocusManager* GetService() const {
    return audio_focus_ptr_.get();
  }

  mojom::AudioFocusManagerDebug* GetDebugService() const {
    return audio_focus_debug_ptr_.get();
  }

  void FlushForTestingIfEnabled() {
    if (!IsEnforcementEnabled())
      return;

    audio_focus_ptr_.FlushForTesting();
  }

  base::test::ScopedTaskEnvironment task_environment_;
  base::HistogramTester histogram_tester_;

  service_manager::TestConnectorFactory connector_factory_;
  std::unique_ptr<MediaSessionService> service_;

  mojom::AudioFocusManagerPtr audio_focus_ptr_;
  mojom::AudioFocusManagerDebugPtr audio_focus_debug_ptr_;

  DISALLOW_COPY_AND_ASSIGN(AudioFocusManagerTest);
};

INSTANTIATE_TEST_SUITE_P(
    ,
    AudioFocusManagerTest,
    testing::Values(mojom::EnforcementMode::kDefault,
                    mojom::EnforcementMode::kNone,
                    mojom::EnforcementMode::kSingleGroup,
                    mojom::EnforcementMode::kSingleSession));

TEST_P(AudioFocusManagerTest, RequestAudioFocusGain_ReplaceFocusedEntry) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;
  test::MockMediaSession media_session_3;

  EXPECT_EQ(base::UnguessableToken::Null(), GetAudioFocusedSession());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kInactive,
            GetState(&media_session_1));
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kInactive,
            GetState(&media_session_2));
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kInactive,
            GetState(&media_session_3));

  AudioFocusManager::RequestId request_id_1 =
      RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id_1, GetAudioFocusedSession());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  AudioFocusManager::RequestId request_id_2 =
      RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id_2, GetAudioFocusedSession());
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_1));

  AudioFocusManager::RequestId request_id_3 =
      RequestAudioFocus(&media_session_3, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id_3, GetAudioFocusedSession());
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_2));
}

TEST_P(AudioFocusManagerTest, RequestAudioFocusGain_Duplicate) {
  test::MockMediaSession media_session;

  EXPECT_EQ(base::UnguessableToken::Null(), GetAudioFocusedSession());

  AudioFocusManager::RequestId request_id =
      RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id, GetAudioFocusedSession());

  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id, GetAudioFocusedSession());
}

TEST_P(AudioFocusManagerTest, RequestAudioFocusGain_FromTransient) {
  test::MockMediaSession media_session;

  AudioFocusManager::RequestId request_id =
      RequestAudioFocus(&media_session, mojom::AudioFocusType::kGainTransient);
  EXPECT_EQ(base::UnguessableToken::Null(), GetAudioFocusedSession());
  EXPECT_EQ(1, GetTransientCount());

  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id, GetAudioFocusedSession());
  EXPECT_EQ(0, GetTransientCount());
}

TEST_P(AudioFocusManagerTest, RequestAudioFocusGain_FromTransientMayDuck) {
  test::MockMediaSession media_session;

  AudioFocusManager::RequestId request_id = RequestAudioFocus(
      &media_session, mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(base::UnguessableToken::Null(), GetAudioFocusedSession());
  EXPECT_EQ(1, GetTransientMaybeDuckCount());

  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id, GetAudioFocusedSession());
  EXPECT_EQ(0, GetTransientMaybeDuckCount());
}

TEST_P(AudioFocusManagerTest, RequestAudioFocusTransient_FromGain) {
  test::MockMediaSession media_session;

  AudioFocusManager::RequestId request_id =
      RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);

  EXPECT_EQ(request_id, GetAudioFocusedSession());
  EXPECT_EQ(0, GetTransientCount());

  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGainTransient);
  EXPECT_EQ(base::UnguessableToken::Null(), GetAudioFocusedSession());
  EXPECT_EQ(1, GetTransientCount());
  EXPECT_NE(mojom::MediaSessionInfo::SessionState::kSuspended,
            GetState(&media_session));
}

TEST_P(AudioFocusManagerTest, RequestAudioFocusTransientMayDuck_FromGain) {
  test::MockMediaSession media_session;

  AudioFocusManager::RequestId request_id =
      RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);

  EXPECT_EQ(request_id, GetAudioFocusedSession());
  EXPECT_EQ(0, GetTransientMaybeDuckCount());

  RequestAudioFocus(&media_session,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(base::UnguessableToken::Null(), GetAudioFocusedSession());
  EXPECT_EQ(1, GetTransientMaybeDuckCount());
  EXPECT_NE(mojom::MediaSessionInfo::SessionState::kDucking,
            GetState(&media_session));
}

TEST_P(AudioFocusManagerTest, RequestAudioFocusTransient_FromGainWhileDucking) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(0, GetTransientMaybeDuckCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(0, GetTransientCount());
  EXPECT_EQ(1, GetTransientMaybeDuckCount());
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGainTransient);
  EXPECT_EQ(1, GetTransientCount());
  EXPECT_EQ(1, GetTransientMaybeDuckCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest,
       RequestAudioFocusTransientMayDuck_FromGainWhileDucking) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(0, GetTransientMaybeDuckCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(1, GetTransientMaybeDuckCount());
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_1,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(2, GetTransientMaybeDuckCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest, AbandonAudioFocus_RemovesFocusedEntry) {
  test::MockMediaSession media_session;

  AudioFocusManager::RequestId request_id =
      RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id, GetAudioFocusedSession());

  media_session.AbandonAudioFocusFromClient();
  EXPECT_EQ(base::UnguessableToken::Null(), GetAudioFocusedSession());
}

TEST_P(AudioFocusManagerTest, AbandonAudioFocus_MultipleCalls) {
  test::MockMediaSession media_session;

  AudioFocusManager::RequestId request_id =
      RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id, GetAudioFocusedSession());

  AbandonAudioFocusNoReset(&media_session);

  std::unique_ptr<test::TestAudioFocusObserver> observer = CreateObserver();
  media_session.AbandonAudioFocusFromClient();

  EXPECT_EQ(base::UnguessableToken::Null(), GetAudioFocusedSession());
  EXPECT_TRUE(observer->focus_lost_session().is_null());
}

TEST_P(AudioFocusManagerTest, AbandonAudioFocus_RemovesTransientMayDuckEntry) {
  test::MockMediaSession media_session;

  RequestAudioFocus(&media_session,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(1, GetTransientMaybeDuckCount());

  {
    std::unique_ptr<test::TestAudioFocusObserver> observer = CreateObserver();
    media_session.AbandonAudioFocusFromClient();

    EXPECT_EQ(0, GetTransientMaybeDuckCount());
    EXPECT_TRUE(observer->focus_lost_session()->session_info.Equals(
        test::GetMediaSessionInfoSync(&media_session)));
  }
}

TEST_P(AudioFocusManagerTest, AbandonAudioFocus_RemovesTransientEntry) {
  test::MockMediaSession media_session;

  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGainTransient);
  EXPECT_EQ(1, GetTransientCount());

  {
    std::unique_ptr<test::TestAudioFocusObserver> observer = CreateObserver();
    media_session.AbandonAudioFocusFromClient();

    EXPECT_EQ(0, GetTransientCount());
    EXPECT_TRUE(observer->focus_lost_session()->session_info.Equals(
        test::GetMediaSessionInfoSync(&media_session)));
  }
}

TEST_P(AudioFocusManagerTest, AbandonAudioFocus_WhileDuckingThenResume) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(0, GetTransientMaybeDuckCount());
  EXPECT_NE(mojom::MediaSessionInfo::SessionState::kDucking,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(1, GetTransientMaybeDuckCount());
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));

  media_session_1.AbandonAudioFocusFromClient();
  EXPECT_EQ(1, GetTransientMaybeDuckCount());

  media_session_2.AbandonAudioFocusFromClient();
  EXPECT_EQ(0, GetTransientMaybeDuckCount());

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_NE(mojom::MediaSessionInfo::SessionState::kDucking,
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest, AbandonAudioFocus_StopsDucking) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(0, GetTransientMaybeDuckCount());
  EXPECT_NE(mojom::MediaSessionInfo::SessionState::kDucking,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(1, GetTransientMaybeDuckCount());
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));

  media_session_2.AbandonAudioFocusFromClient();
  EXPECT_EQ(0, GetTransientMaybeDuckCount());
  EXPECT_NE(mojom::MediaSessionInfo::SessionState::kDucking,
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest, AbandonAudioFocus_ResumesPlayback) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(0, GetTransientCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGainTransient);
  EXPECT_EQ(1, GetTransientCount());
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_1));

  media_session_2.AbandonAudioFocusFromClient();
  EXPECT_EQ(0, GetTransientCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest, DuckWhilePlaying) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_NE(mojom::MediaSessionInfo::SessionState::kDucking,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest, GainSuspendsTransient) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGainTransient);

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_2));
}

TEST_P(AudioFocusManagerTest, GainSuspendsTransientMayDuck) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  RequestAudioFocus(&media_session_2,
                    mojom::AudioFocusType::kGainTransientMayDuck);

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_2));
}

TEST_P(AudioFocusManagerTest, DuckWithMultipleTransientMayDucks) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;
  test::MockMediaSession media_session_3;
  test::MockMediaSession media_session_4;

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_NE(mojom::MediaSessionInfo::SessionState::kDucking,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGainTransient);
  EXPECT_NE(mojom::MediaSessionInfo::SessionState::kDucking,
            GetState(&media_session_2));

  RequestAudioFocus(&media_session_3,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_2));

  RequestAudioFocus(&media_session_4,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_2));

  media_session_3.AbandonAudioFocusFromClient();
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_2));

  media_session_4.AbandonAudioFocusFromClient();
  EXPECT_NE(mojom::MediaSessionInfo::SessionState::kDucking,
            GetState(&media_session_1));
  EXPECT_NE(mojom::MediaSessionInfo::SessionState::kDucking,
            GetState(&media_session_2));
}

TEST_P(AudioFocusManagerTest, MediaSessionDestroyed_ReleasesFocus) {
  {
    test::MockMediaSession media_session;

    AudioFocusManager::RequestId request_id =
        RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
    EXPECT_EQ(request_id, GetAudioFocusedSession());
  }

  // If the media session is destroyed without abandoning audio focus we do not
  // know until we next interact with the manager.
  test::MockMediaSession media_session;
  RequestAudioFocus(&media_session,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(base::UnguessableToken::Null(), GetAudioFocusedSession());
}

TEST_P(AudioFocusManagerTest, MediaSessionDestroyed_ReleasesTransient) {
  {
    test::MockMediaSession media_session;
    RequestAudioFocus(&media_session, mojom::AudioFocusType::kGainTransient);
    EXPECT_EQ(1, GetTransientCount());
  }

  // If the media session is destroyed without abandoning audio focus we do not
  // know until we next interact with the manager.
  test::MockMediaSession media_session;
  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
  EXPECT_EQ(0, GetTransientCount());
}

TEST_P(AudioFocusManagerTest, MediaSessionDestroyed_ReleasesTransientMayDucks) {
  {
    test::MockMediaSession media_session;
    RequestAudioFocus(&media_session,
                      mojom::AudioFocusType::kGainTransientMayDuck);
    EXPECT_EQ(1, GetTransientMaybeDuckCount());
  }

  // If the media session is destroyed without abandoning audio focus we do not
  // know until we next interact with the manager.
  test::MockMediaSession media_session;
  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
  EXPECT_EQ(0, GetTransientMaybeDuckCount());
}

TEST_P(AudioFocusManagerTest, GainDucksForceDuck) {
  test::MockMediaSession media_session_1(true /* force_duck */);
  test::MockMediaSession media_session_2;

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);

  AudioFocusManager::RequestId request_id_2 =
      RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGain);

  EXPECT_EQ(request_id_2, GetAudioFocusedSession());
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest, ForceDuckSessionShouldAlwaysBeDuckedFromGain) {
  test::MockMediaSession media_session_1(true /* force_duck */);
  test::MockMediaSession media_session_2;
  test::MockMediaSession media_session_3;

  AudioFocusManager::RequestId request_id_1 =
      RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  AudioFocusManager::RequestId request_id_2 =
      RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGain);

  AudioFocusManager::RequestId request_id_3 =
      RequestAudioFocus(&media_session_3, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id_3, GetAudioFocusedSession());

  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_2));
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));

  media_session_3.AbandonAudioFocusFromClient();
  EXPECT_EQ(request_id_2, GetAudioFocusedSession());
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));

  media_session_2.AbandonAudioFocusFromClient();
  EXPECT_EQ(request_id_1, GetAudioFocusedSession());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest,
       ForceDuckSessionShouldAlwaysBeDuckedFromTransient) {
  test::MockMediaSession media_session_1(true /* force_duck */);
  test::MockMediaSession media_session_2;

  AudioFocusManager::RequestId request_id_1 =
      RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGainTransient);

  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_2));
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));

  media_session_2.AbandonAudioFocusFromClient();
  EXPECT_EQ(request_id_1, GetAudioFocusedSession());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest, AudioFocusObserver_RequestNoop) {
  test::MockMediaSession media_session;
  AudioFocusManager::RequestId request_id;

  {
    std::unique_ptr<test::TestAudioFocusObserver> observer = CreateObserver();
    request_id =
        RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);

    EXPECT_EQ(request_id, GetAudioFocusedSession());
    EXPECT_EQ(mojom::AudioFocusType::kGain,
              observer->focus_gained_session()->audio_focus_type);
  }

  {
    std::unique_ptr<test::TestAudioFocusObserver> observer = CreateObserver();
    RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);

    EXPECT_EQ(request_id, GetAudioFocusedSession());
    EXPECT_TRUE(observer->focus_gained_session().is_null());
  }
}

TEST_P(AudioFocusManagerTest, AudioFocusObserver_TransientMayDuck) {
  test::MockMediaSession media_session;

  {
    std::unique_ptr<test::TestAudioFocusObserver> observer = CreateObserver();
    RequestAudioFocus(&media_session,
                      mojom::AudioFocusType::kGainTransientMayDuck);

    EXPECT_EQ(1, GetTransientMaybeDuckCount());
    EXPECT_EQ(mojom::AudioFocusType::kGainTransientMayDuck,
              observer->focus_gained_session()->audio_focus_type);
  }

  {
    std::unique_ptr<test::TestAudioFocusObserver> observer = CreateObserver();
    media_session.AbandonAudioFocusFromClient();

    EXPECT_EQ(0, GetTransientMaybeDuckCount());
    EXPECT_TRUE(observer->focus_lost_session()->session_info.Equals(
        test::GetMediaSessionInfoSync(&media_session)));
  }
}

TEST_P(AudioFocusManagerTest, GetDebugInfo) {
  test::MockMediaSession media_session;
  AudioFocusManager::RequestId request_id =
      RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);

  mojom::MediaSessionDebugInfoPtr debug_info = GetDebugInfo(request_id);
  EXPECT_FALSE(debug_info->name.empty());
  EXPECT_FALSE(debug_info->owner.empty());
  EXPECT_FALSE(debug_info->state.empty());
}

TEST_P(AudioFocusManagerTest, GetDebugInfo_BadRequestId) {
  mojom::MediaSessionDebugInfoPtr debug_info =
      GetDebugInfo(base::UnguessableToken::Create());
  EXPECT_TRUE(debug_info->name.empty());
}

TEST_P(AudioFocusManagerTest,
       RequestAudioFocusTransient_FromGainWhileSuspended) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(0, GetTransientCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGainTransient);
  EXPECT_EQ(1, GetTransientCount());
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_1));

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGainTransient);
  EXPECT_EQ(2, GetTransientCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest,
       RequestAudioFocusTransientMayDuck_FromGainWhileSuspended) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(0, GetTransientCount());
  EXPECT_EQ(0, GetTransientMaybeDuckCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGainTransient);
  EXPECT_EQ(1, GetTransientCount());
  EXPECT_EQ(0, GetTransientMaybeDuckCount());
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_1));

  RequestAudioFocus(&media_session_1,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(1, GetTransientCount());
  EXPECT_EQ(1, GetTransientMaybeDuckCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest, SourceName_AssociatedWithBinding) {
  SetSourceName(kExampleSourceName);

  mojom::AudioFocusManagerPtr new_ptr = CreateAudioFocusManagerPtr();
  new_ptr->SetSourceName(kExampleSourceName2);
  new_ptr.FlushForTesting();

  test::MockMediaSession media_session;
  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
  EXPECT_EQ(kExampleSourceName, GetSourceNameForLastRequest());
}

TEST_P(AudioFocusManagerTest, SourceName_Empty) {
  test::MockMediaSession media_session;
  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
  EXPECT_TRUE(GetSourceNameForLastRequest().empty());
}

TEST_P(AudioFocusManagerTest, SourceName_Updated) {
  SetSourceName(kExampleSourceName);

  test::MockMediaSession media_session;
  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
  EXPECT_EQ(kExampleSourceName, GetSourceNameForLastRequest());

  SetSourceName(kExampleSourceName2);
  EXPECT_EQ(kExampleSourceName, GetSourceNameForLastRequest());
}

TEST_P(AudioFocusManagerTest, RecordUmaMetrics) {
  EXPECT_EQ(0, GetAudioFocusHistogramCount());

  SetSourceName(kExampleSourceName);
  test::MockMediaSession media_session;
  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGainTransient);

  {
    std::unique_ptr<base::HistogramSamples> samples(
        GetHistogramSamplesSinceTestStart(
            "Media.Session.AudioFocus.Request.Test"));
    EXPECT_EQ(1, samples->TotalCount());
    EXPECT_EQ(1, samples->GetCount(static_cast<base::HistogramBase::Sample>(
                     AudioFocusManagerMetricsHelper::AudioFocusRequestSource::
                         kInitial)));
  }

  {
    std::unique_ptr<base::HistogramSamples> samples(
        GetHistogramSamplesSinceTestStart(
            "Media.Session.AudioFocus.Type.Test"));
    EXPECT_EQ(1, samples->TotalCount());
    EXPECT_EQ(
        1,
        samples->GetCount(static_cast<base::HistogramBase::Sample>(
            AudioFocusManagerMetricsHelper::AudioFocusType::kGainTransient)));
  }

  EXPECT_EQ(2, GetAudioFocusHistogramCount());

  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);

  {
    std::unique_ptr<base::HistogramSamples> samples(
        GetHistogramSamplesSinceTestStart(
            "Media.Session.AudioFocus.Request.Test"));
    EXPECT_EQ(2, samples->TotalCount());
    EXPECT_EQ(
        1,
        samples->GetCount(static_cast<base::HistogramBase::Sample>(
            AudioFocusManagerMetricsHelper::AudioFocusRequestSource::kUpdate)));
  }

  {
    std::unique_ptr<base::HistogramSamples> samples(
        GetHistogramSamplesSinceTestStart(
            "Media.Session.AudioFocus.Type.Test"));
    EXPECT_EQ(2, samples->TotalCount());
    EXPECT_EQ(1, samples->GetCount(static_cast<base::HistogramBase::Sample>(
                     AudioFocusManagerMetricsHelper::AudioFocusType::kGain)));
  }

  EXPECT_EQ(2, GetAudioFocusHistogramCount());

  media_session.AbandonAudioFocusFromClient();

  {
    std::unique_ptr<base::HistogramSamples> samples(
        GetHistogramSamplesSinceTestStart(
            "Media.Session.AudioFocus.Abandon.Test"));
    EXPECT_EQ(1, samples->TotalCount());
    EXPECT_EQ(
        1, samples->GetCount(static_cast<base::HistogramBase::Sample>(
               AudioFocusManagerMetricsHelper::AudioFocusAbandonSource::kAPI)));
  }

  EXPECT_EQ(3, GetAudioFocusHistogramCount());
}

TEST_P(AudioFocusManagerTest, RecordUmaMetrics_ConnectionError) {
  SetSourceName(kExampleSourceName);

  {
    test::MockMediaSession media_session;
    RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);
  }

  test::MockMediaSession media_session;
  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);

  {
    std::unique_ptr<base::HistogramSamples> samples(
        GetHistogramSamplesSinceTestStart(
            "Media.Session.AudioFocus.Abandon.Test"));
    EXPECT_EQ(1, samples->TotalCount());
    EXPECT_EQ(1, samples->GetCount(static_cast<base::HistogramBase::Sample>(
                     AudioFocusManagerMetricsHelper::AudioFocusAbandonSource::
                         kConnectionError)));
  }
}

TEST_P(AudioFocusManagerTest, RecordUmaMetrics_NoSourceName) {
  test::MockMediaSession media_session;
  RequestAudioFocus(&media_session, mojom::AudioFocusType::kGain);

  EXPECT_EQ(0, GetAudioFocusHistogramCount());
}

TEST_P(AudioFocusManagerTest,
       AbandonAudioFocus_ObserverFocusGain_NoTopSession) {
  test::MockMediaSession media_session;

  RequestAudioFocus(&media_session,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(1, GetTransientMaybeDuckCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session));

  {
    std::unique_ptr<test::TestAudioFocusObserver> observer = CreateObserver();
    media_session.AbandonAudioFocusFromClient();

    EXPECT_EQ(0, GetTransientMaybeDuckCount());
    EXPECT_TRUE(observer->focus_lost_session()->session_info.Equals(
        test::GetMediaSessionInfoSync(&media_session)));
    EXPECT_TRUE(observer->focus_gained_session().is_null());

    auto notifications = observer->notifications();
    EXPECT_EQ(1u, notifications.size());
    EXPECT_EQ(test::TestAudioFocusObserver::NotificationType::kFocusLost,
              notifications[0]);
  }
}

TEST_P(AudioFocusManagerTest,
       AbandonAudioFocus_ObserverFocusGain_NewTopSession) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(0, GetTransientMaybeDuckCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(1, GetTransientMaybeDuckCount());
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));

  mojom::MediaSessionInfoPtr media_session_1_info =
      test::GetMediaSessionInfoSync(&media_session_1);

  {
    std::unique_ptr<test::TestAudioFocusObserver> observer = CreateObserver();
    media_session_2.AbandonAudioFocusFromClient();

    EXPECT_EQ(0, GetTransientMaybeDuckCount());
    EXPECT_TRUE(observer->focus_lost_session()->session_info.Equals(
        test::GetMediaSessionInfoSync(&media_session_2)));
    EXPECT_TRUE(observer->focus_gained_session()->session_info.Equals(
        media_session_1_info));

    // FocusLost should always come before FocusGained so observers always know
    // the current session that has focus.
    auto notifications = observer->notifications();
    EXPECT_EQ(2u, notifications.size());
    EXPECT_EQ(test::TestAudioFocusObserver::NotificationType::kFocusLost,
              notifications[0]);
    EXPECT_EQ(test::TestAudioFocusObserver::NotificationType::kFocusGained,
              notifications[1]);
  }
}

TEST_P(AudioFocusManagerTest, AudioFocusGrouping_LayeredFocus) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;
  test::MockMediaSession media_session_3;

  base::UnguessableToken group_id = base::UnguessableToken::Create();

  RequestGroupedAudioFocus(&media_session_1, mojom::AudioFocusType::kGain,
                           group_id);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));

  // When we request audio focus for media_session_3 the group will take audio
  // focus and we suspend the ducking session.
  RequestGroupedAudioFocus(&media_session_3,
                           mojom::AudioFocusType::kGainTransient, group_id);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_3));

  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_2));
  EXPECT_EQ(GetStateFromParam(
                IsGroupingEnabled()
                    ? mojom::MediaSessionInfo::SessionState::kActive
                    : mojom::MediaSessionInfo::SessionState::kSuspended),
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest, AudioFocusGrouping_TransientResume) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;
  test::MockMediaSession media_session_3;
  test::MockMediaSession media_session_4;

  base::UnguessableToken group_id = base::UnguessableToken::Create();

  RequestGroupedAudioFocus(&media_session_1, mojom::AudioFocusType::kGain,
                           group_id);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGain);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_2));

  RequestGroupedAudioFocus(&media_session_3, mojom::AudioFocusType::kGain,
                           group_id);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_3));

  RequestAudioFocus(&media_session_4, mojom::AudioFocusType::kGainTransient);
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_1));
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_2));
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_3));

  media_session_4.AbandonAudioFocusFromClient();

  // TODO(https://crbug.com/916177): This should wait on a more precise
  // condition than RunLoop idling, but it's not clear exactly what that
  // should be.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(IsGroupingEnabled()
                ? mojom::MediaSessionInfo::SessionState::kActive
                : mojom::MediaSessionInfo::SessionState::kSuspended,
            GetState(&media_session_1));
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_2));
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_3));
}

TEST_P(AudioFocusManagerTest, AudioFocusGrouping_DoNotSuspendSameGroup) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  base::UnguessableToken group_id = base::UnguessableToken::Create();

  RequestGroupedAudioFocus(&media_session_1, mojom::AudioFocusType::kGain,
                           group_id);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestGroupedAudioFocus(&media_session_2, mojom::AudioFocusType::kGain,
                           group_id);
  EXPECT_EQ(IsGroupingEnabled()
                ? mojom::MediaSessionInfo::SessionState::kActive
                : mojom::MediaSessionInfo::SessionState::kSuspended,
            GetState(&media_session_1));
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_2));
}

TEST_P(AudioFocusManagerTest, AudioFocusGrouping_DuckSameGroup) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  base::UnguessableToken group_id = base::UnguessableToken::Create();

  RequestGroupedAudioFocus(&media_session_1, mojom::AudioFocusType::kGain,
                           group_id);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestGroupedAudioFocus(
      &media_session_2, mojom::AudioFocusType::kGainTransientMayDuck, group_id);
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest, AudioFocusGrouping_TransientSameGroup) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  base::UnguessableToken group_id = base::UnguessableToken::Create();

  RequestGroupedAudioFocus(&media_session_1, mojom::AudioFocusType::kGain,
                           group_id);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestGroupedAudioFocus(&media_session_2,
                           mojom::AudioFocusType::kGainTransient, group_id);
  EXPECT_EQ(IsGroupingEnabled()
                ? mojom::MediaSessionInfo::SessionState::kActive
                : mojom::MediaSessionInfo::SessionState::kSuspended,
            GetState(&media_session_1));
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_2));
}

TEST_P(AudioFocusManagerTest, RequestAudioFocus_PreferStop_LossToGain) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  media_session_1.SetPreferStop(true);

  AudioFocusManager::RequestId request_id_1 =
      RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id_1, GetAudioFocusedSession());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  AudioFocusManager::RequestId request_id_2 =
      RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id_2, GetAudioFocusedSession());
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kInactive),
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest,
       RequestAudioFocus_PreferStop_LossToGainTransient) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  media_session_1.SetPreferStop(true);

  AudioFocusManager::RequestId request_id_1 =
      RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id_1, GetAudioFocusedSession());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGainTransient);
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest, GainFocusTypeHasEffectEvenIfSuspended) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;
  test::MockMediaSession media_session_3;

  AudioFocusManager::RequestId request_id_1 =
      RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id_1, GetAudioFocusedSession());

  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGain);
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_1));

  // When the second session becomes suspended and that event originated from
  // the session itself then we should keep the other session suspended.
  media_session_2.Suspend(mojom::MediaSession::SuspendType::kUI);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kSuspended,
            GetState(&media_session_2));

  {
    test::MockMediaSessionMojoObserver observer(media_session_1);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }

  // When the second session is resumed then we should still keep the other
  // session suspended.
  media_session_2.Resume(mojom::MediaSession::SuspendType::kUI);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_2));

  {
    test::MockMediaSessionMojoObserver observer(media_session_1);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }

  // If a new session takes focus then this should suspend all sessions.
  RequestAudioFocus(&media_session_3, mojom::AudioFocusType::kGainTransient);

  {
    test::MockMediaSessionMojoObserver observer(media_session_2);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }

  {
    test::MockMediaSessionMojoObserver observer(media_session_1);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }

  // If the second session regains focus then it should suspend all sessions.
  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGain);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_2));

  {
    test::MockMediaSessionMojoObserver observer(media_session_1);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }

  {
    test::MockMediaSessionMojoObserver observer(media_session_3);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }
}

TEST_P(AudioFocusManagerTest, TransientFocusTypeHasNoEffectIfSuspended) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;
  test::MockMediaSession media_session_3;

  AudioFocusManager::RequestId request_id_1 =
      RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id_1, GetAudioFocusedSession());

  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGainTransient);
  EXPECT_EQ(
      GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended),
      GetState(&media_session_1));

  // When the transient session becomes suspended and that event originates from
  // the session itself then we should stop pausing the other session.
  media_session_2.Suspend(mojom::MediaSession::SuspendType::kUI);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kSuspended,
            GetState(&media_session_2));

  {
    test::MockMediaSessionMojoObserver observer(media_session_1);
    observer.WaitForState(mojom::MediaSessionInfo::SessionState::kActive);
  }

  // When the transient session is resumed then we should pause the other
  // session again.
  media_session_2.Resume(mojom::MediaSession::SuspendType::kUI);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_2));

  {
    test::MockMediaSessionMojoObserver observer(media_session_1);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }

  // If we have a new session take focus then this should suspend all the other
  // sessions and the transient session should have no effect.
  RequestAudioFocus(&media_session_3, mojom::AudioFocusType::kGainTransient);

  {
    test::MockMediaSessionMojoObserver observer(media_session_2);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }

  {
    test::MockMediaSessionMojoObserver observer(media_session_1);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }

  // If the second session regains focus then it should start pausing again.
  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kGainTransient);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_2));

  {
    test::MockMediaSessionMojoObserver observer(media_session_1);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }

  {
    test::MockMediaSessionMojoObserver observer(media_session_3);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }
}

TEST_P(AudioFocusManagerTest, TransientDuckFocusTypeHasNoEffectIfSuspended) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;
  test::MockMediaSession media_session_3;

  AudioFocusManager::RequestId request_id_1 =
      RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id_1, GetAudioFocusedSession());

  RequestAudioFocus(&media_session_2,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));

  // When the ducking session becomes suspended and that event originates from
  // the session itself then we should stop ducking.
  media_session_2.Suspend(mojom::MediaSession::SuspendType::kUI);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kSuspended,
            GetState(&media_session_2));

  {
    test::MockMediaSessionMojoObserver observer(media_session_1);
    observer.WaitForState(mojom::MediaSessionInfo::SessionState::kActive);
  }

  // When the ducking session is resumed then we should resume ducking.
  media_session_2.Resume(mojom::MediaSession::SuspendType::kUI);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_2));

  {
    test::MockMediaSessionMojoObserver observer(media_session_1);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking));
  }

  // If we have a new session take focus then this should suspend all the other
  // sessions and we should not have any ducking from the ducking session (since
  // it is suspended).
  RequestAudioFocus(&media_session_3, mojom::AudioFocusType::kGainTransient);

  {
    test::MockMediaSessionMojoObserver observer(media_session_2);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }

  {
    test::MockMediaSessionMojoObserver observer(media_session_1);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kSuspended));
  }

  // If the ducking session regains focus then it should start ducking again.
  RequestAudioFocus(&media_session_2,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_2));

  {
    test::MockMediaSessionMojoObserver observer(media_session_1);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking));
  }

  {
    test::MockMediaSessionMojoObserver observer(media_session_3);
    observer.WaitForState(
        GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking));
  }
}

TEST_P(AudioFocusManagerTest, AmbientFocusHasNoEffect) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  AudioFocusManager::RequestId request_id_1 =
      RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id_1, GetAudioFocusedSession());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  EXPECT_EQ(0, GetAmbientCount());
  RequestAudioFocus(&media_session_2, mojom::AudioFocusType::kAmbient);

  EXPECT_EQ(1, GetAmbientCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_2));

  media_session_2.AbandonAudioFocusFromClient();

  EXPECT_EQ(0, GetAmbientCount());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));
}

TEST_P(AudioFocusManagerTest, AudioFocusObserver_NotTopMost) {
  test::MockMediaSession media_session_1;
  test::MockMediaSession media_session_2;

  AudioFocusManager::RequestId request_id_1 =
      RequestAudioFocus(&media_session_1, mojom::AudioFocusType::kGain);
  EXPECT_EQ(request_id_1, GetAudioFocusedSession());
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_1));

  RequestAudioFocus(&media_session_2,
                    mojom::AudioFocusType::kGainTransientMayDuck);
  EXPECT_EQ(GetStateFromParam(mojom::MediaSessionInfo::SessionState::kDucking),
            GetState(&media_session_1));
  EXPECT_EQ(mojom::MediaSessionInfo::SessionState::kActive,
            GetState(&media_session_2));

  {
    std::unique_ptr<test::TestAudioFocusObserver> observer = CreateObserver();
    media_session_1.AbandonAudioFocusFromClient();

    EXPECT_TRUE(observer->focus_lost_session()->session_info.Equals(
        test::GetMediaSessionInfoSync(&media_session_1)));
  }
}

}  // namespace media_session
