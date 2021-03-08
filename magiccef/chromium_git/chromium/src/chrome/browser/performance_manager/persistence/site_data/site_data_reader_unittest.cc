// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/performance_manager/persistence/site_data/site_data_reader.h"

#include <memory>
#include <utility>

#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/test/bind_test_util.h"
#include "base/test/simple_test_tick_clock.h"
#include "chrome/browser/performance_manager/performance_manager_clock.h"
#include "chrome/browser/performance_manager/persistence/site_data/site_data_impl.h"
#include "chrome/browser/performance_manager/persistence/site_data/unittest_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace performance_manager {

namespace {

class MockSiteDataStore : public testing::NoopSiteDataStore {
 public:
  MockSiteDataStore() = default;
  ~MockSiteDataStore() = default;

  // Note: As move-only parameters (e.g. OnceCallback) aren't supported by mock
  // methods, add On... methods to pass a non-const reference to OnceCallback.
  void ReadSiteDataFromStore(
      const url::Origin& origin,
      SiteDataStore::ReadSiteDataFromStoreCallback callback) override {
    OnReadSiteDataFromStore(std::move(origin), callback);
  }
  MOCK_METHOD2(OnReadSiteDataFromStore,
               void(const url::Origin&,
                    SiteDataStore::ReadSiteDataFromStoreCallback&));

  MOCK_METHOD2(WriteSiteDataIntoStore,
               void(const url::Origin&, const SiteDataProto&));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockSiteDataStore);
};

void InitializeSiteDataProto(SiteDataProto* site_data) {
  DCHECK(site_data);
  site_data->set_last_loaded(42);

  SiteDataFeatureProto used_feature_proto;
  used_feature_proto.set_observation_duration(0U);
  used_feature_proto.set_use_timestamp(1U);

  site_data->mutable_updates_favicon_in_background()->CopyFrom(
      used_feature_proto);
  site_data->mutable_updates_title_in_background()->CopyFrom(
      used_feature_proto);
  site_data->mutable_uses_audio_in_background()->CopyFrom(used_feature_proto);
  site_data->mutable_uses_notifications_in_background()->CopyFrom(
      used_feature_proto);

  DCHECK(site_data->IsInitialized());
}

}  // namespace

class SiteDataReaderTest : public ::testing::Test {
 protected:
  // The constructors needs to call 'new' directly rather than using the
  // base::MakeRefCounted helper function because the constructor of
  // SiteDataImpl is protected and not visible to
  // base::MakeRefCounted.
  SiteDataReaderTest() {
    PerformanceManagerClock::SetClockForTesting(&test_clock_);

    test_clock_.Advance(base::TimeDelta::FromSeconds(1));
    test_impl_ = base::WrapRefCounted(new internal::SiteDataImpl(
        url::Origin::Create(GURL("foo.com")), &delegate_, &data_store_));
    test_impl_->NotifySiteLoaded();
    test_impl_->NotifyLoadedSiteBackgrounded();
    SiteDataReader* reader = new SiteDataReader(test_impl_.get());
    reader_ = base::WrapUnique(reader);
  }

  ~SiteDataReaderTest() override {
    test_impl_->NotifySiteUnloaded(
        performance_manager::TabVisibility::kBackground);
    PerformanceManagerClock::ResetClockForTesting();
  }

  base::SimpleTestTickClock test_clock_;

  // The mock delegate used by the SiteDataImpl objects
  // created by this class, NiceMock is used to avoid having to set expectations
  // in test cases that don't care about this.
  ::testing::NiceMock<testing::MockSiteDataImplOnDestroyDelegate> delegate_;

  // The SiteDataImpl object used in these tests.
  scoped_refptr<internal::SiteDataImpl> test_impl_;

  // A SiteDataReader object associated with the origin used
  // to create this object.
  std::unique_ptr<SiteDataReader> reader_;

  testing::NoopSiteDataStore data_store_;

  DISALLOW_COPY_AND_ASSIGN(SiteDataReaderTest);
};

TEST_F(SiteDataReaderTest, TestAccessors) {
  // Initially we have no information about any of the features.
  EXPECT_EQ(performance_manager::SiteFeatureUsage::kSiteFeatureUsageUnknown,
            reader_->UpdatesFaviconInBackground());
  EXPECT_EQ(performance_manager::SiteFeatureUsage::kSiteFeatureUsageUnknown,
            reader_->UpdatesTitleInBackground());
  EXPECT_EQ(performance_manager::SiteFeatureUsage::kSiteFeatureUsageUnknown,
            reader_->UsesAudioInBackground());
  EXPECT_EQ(performance_manager::SiteFeatureUsage::kSiteFeatureUsageUnknown,
            reader_->UsesNotificationsInBackground());

  // Simulates a title update event, make sure it gets reported directly.
  test_impl_->NotifyUpdatesTitleInBackground();

  EXPECT_EQ(performance_manager::SiteFeatureUsage::kSiteFeatureInUse,
            reader_->UpdatesTitleInBackground());

  // Advance the clock by a large amount of time, enough for the unused features
  // observation windows to expire.
  test_clock_.Advance(base::TimeDelta::FromDays(31));

  EXPECT_EQ(performance_manager::SiteFeatureUsage::kSiteFeatureNotInUse,
            reader_->UpdatesFaviconInBackground());
  EXPECT_EQ(performance_manager::SiteFeatureUsage::kSiteFeatureInUse,
            reader_->UpdatesTitleInBackground());
  EXPECT_EQ(performance_manager::SiteFeatureUsage::kSiteFeatureNotInUse,
            reader_->UsesAudioInBackground());
  EXPECT_EQ(performance_manager::SiteFeatureUsage::kSiteFeatureNotInUse,
            reader_->UsesNotificationsInBackground());
}

TEST_F(SiteDataReaderTest, FreeingReaderDoesntCauseWriteOperation) {
  const url::Origin kOrigin = url::Origin::Create(GURL("foo.com"));
  ::testing::StrictMock<MockSiteDataStore> data_store;

  // Override the read callback to simulate a successful read from the
  // data store.
  SiteDataProto proto = {};
  InitializeSiteDataProto(&proto);
  auto read_from_store_mock_impl =
      [&](const url::Origin& origin,
          SiteDataStore::ReadSiteDataFromStoreCallback& callback) {
        std::move(callback).Run(base::Optional<SiteDataProto>(proto));
      };

  EXPECT_CALL(data_store, OnReadSiteDataFromStore(
                              ::testing::Property(&url::Origin::Serialize,
                                                  kOrigin.Serialize()),
                              ::testing::_))
      .WillOnce(::testing::Invoke(read_from_store_mock_impl));

  std::unique_ptr<SiteDataReader> reader =
      base::WrapUnique(new SiteDataReader(base::WrapRefCounted(
          new internal::SiteDataImpl(kOrigin, &delegate_, &data_store))));
  ::testing::Mock::VerifyAndClear(&data_store);

  EXPECT_TRUE(reader->impl_for_testing()->fully_initialized_for_testing());

  // Resetting the reader shouldn't cause any write operation to the data store.
  EXPECT_CALL(data_store, WriteSiteDataIntoStore(::testing::_, ::testing::_))
      .Times(0);
  reader.reset();
  ::testing::Mock::VerifyAndClear(&data_store);
}

TEST_F(SiteDataReaderTest, OnDataLoadedCallbackInvoked) {
  const url::Origin kOrigin = url::Origin::Create(GURL("foo.com"));
  ::testing::StrictMock<MockSiteDataStore> data_store;

  // Create the impl.
  EXPECT_CALL(data_store, OnReadSiteDataFromStore(
                              ::testing::Property(&url::Origin::Serialize,
                                                  kOrigin.Serialize()),
                              ::testing::_));
  scoped_refptr<internal::SiteDataImpl> impl = base::WrapRefCounted(
      new internal::SiteDataImpl(kOrigin, &delegate_, &data_store));

  // Create the reader.
  std::unique_ptr<SiteDataReader> reader =
      base::WrapUnique(new SiteDataReader(impl));
  EXPECT_FALSE(reader->DataLoaded());

  // Register a data ready closure.
  bool on_data_loaded = false;
  reader->RegisterDataLoadedCallback(base::BindLambdaForTesting(
      [&on_data_loaded]() { on_data_loaded = true; }));

  // Transition the impl to fully initialized, which should cause the callbacks
  // to fire.
  EXPECT_FALSE(impl->DataLoaded());
  EXPECT_FALSE(on_data_loaded);
  impl->TransitionToFullyInitialized();
  EXPECT_TRUE(impl->DataLoaded());
  EXPECT_TRUE(on_data_loaded);
}

TEST_F(SiteDataReaderTest, DestroyingReaderCancelsPendingCallbacks) {
  const url::Origin kOrigin = url::Origin::Create(GURL("foo.com"));
  ::testing::StrictMock<MockSiteDataStore> data_store;

  // Create the impl.
  EXPECT_CALL(data_store, OnReadSiteDataFromStore(
                              ::testing::Property(&url::Origin::Serialize,
                                                  kOrigin.Serialize()),
                              ::testing::_));
  scoped_refptr<internal::SiteDataImpl> impl = base::WrapRefCounted(
      new internal::SiteDataImpl(kOrigin, &delegate_, &data_store));

  // Create the reader.
  std::unique_ptr<SiteDataReader> reader =
      base::WrapUnique(new SiteDataReader(impl));
  EXPECT_FALSE(reader->DataLoaded());

  // Register a data ready closure.
  reader->RegisterDataLoadedCallback(
      base::MakeExpectedNotRunClosure(FROM_HERE));

  // Reset the reader.
  reader.reset();

  // Transition the impl to fully initialized, which should cause the callbacks
  // to fire. The reader's callback should *not* be invoked.
  EXPECT_FALSE(impl->DataLoaded());
  impl->TransitionToFullyInitialized();
  EXPECT_TRUE(impl->DataLoaded());
}

}  // namespace performance_manager
