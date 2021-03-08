// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise_reporting/report_uploader.h"

#include <utility>

#include "base/test/scoped_task_environment.h"
#include "components/policy/core/common/cloud/mock_cloud_policy_client.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Property;
using ::testing::WithArgs;

namespace enterprise_reporting {
namespace {
constexpr const char* kOsUserNames[] = {"name1", "name2"};
}  // namespace

class ReportUploaderTest : public ::testing::Test {
 public:
  ReportUploaderTest()
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::MOCK_TIME) {
    CreateUploader(0);
  }
  ~ReportUploaderTest() override {}

  void UploadReportAndSetExpectation(
      int number_of_request,
      ReportUploader::ReportStatus expected_status) {
    DCHECK_LE(number_of_request, 2) << "Please update kOsUserNames above.";
    std::queue<std::unique_ptr<em::ChromeDesktopReportRequest>> requests;
    for (int i = 0; i < number_of_request; i++) {
      auto request = std::make_unique<em::ChromeDesktopReportRequest>();
      request->set_os_user_name(kOsUserNames[i]);
      requests.push(std::move(request));
    }
    has_responded_ = false;
    uploader_->SetRequestAndUpload(
        std::move(requests),
        base::BindOnce(&ReportUploaderTest::OnReportUploaded,
                       base::Unretained(this), expected_status));
  }

  void OnReportUploaded(ReportUploader::ReportStatus expected_status,
                        ReportUploader::ReportStatus actuall_status) {
    EXPECT_EQ(expected_status, actuall_status);
    has_responded_ = true;
  }

  void CreateUploader(int retry_count) {
    uploader_ = std::make_unique<ReportUploader>(&client_, retry_count);
  }

  // Forwards to send next request and get response.
  void RunNextTask() {
    scoped_task_environment_.FastForwardBy(
        scoped_task_environment_.NextMainThreadPendingTaskDelay());
  }

  // Verifies the retried is delayed properly.
  void VerifyRequestDelay(int delay_seconds) {
    if (delay_seconds == 0) {
      EXPECT_EQ(base::TimeDelta(),
                scoped_task_environment_.NextMainThreadPendingTaskDelay());
      return;
    }
    EXPECT_GE(base::TimeDelta::FromSeconds(delay_seconds),
              scoped_task_environment_.NextMainThreadPendingTaskDelay());
    EXPECT_LE(
        base::TimeDelta::FromSeconds(static_cast<int>(delay_seconds * 0.9)),
        scoped_task_environment_.NextMainThreadPendingTaskDelay());
  }

  base::test::ScopedTaskEnvironment scoped_task_environment_;

  std::unique_ptr<ReportUploader> uploader_;
  policy::MockCloudPolicyClient client_;
  bool has_responded_ = false;

 private:
  DISALLOW_COPY_AND_ASSIGN(ReportUploaderTest);
};

class ReportUploaderTestWithTransientError
    : public ReportUploaderTest,
      public ::testing::WithParamInterface<policy::DeviceManagementStatus> {};

TEST_F(ReportUploaderTest, Success) {
  EXPECT_CALL(client_, UploadChromeDesktopReportProxy(_, _))
      .WillOnce(WithArgs<1>(policy::ScheduleStatusCallback(true)));
  UploadReportAndSetExpectation(/*number_of_request=*/1,
                                ReportUploader::kSuccess);
  RunNextTask();
  EXPECT_TRUE(has_responded_);
  ::testing::Mock::VerifyAndClearExpectations(&client_);
}

TEST_F(ReportUploaderTest, PersistentError) {
  CreateUploader(/* retry_count = */ 1);
  EXPECT_CALL(client_, UploadChromeDesktopReportProxy(_, _))
      .WillOnce(WithArgs<1>(policy::ScheduleStatusCallback(false)));
  client_.SetStatus(policy::DM_STATUS_SERVICE_DEVICE_NOT_FOUND);
  UploadReportAndSetExpectation(/*number_of_request=*/2,
                                ReportUploader::kPersistentError);
  RunNextTask();
  EXPECT_TRUE(has_responded_);
  ::testing::Mock::VerifyAndClearExpectations(&client_);
}

TEST_F(ReportUploaderTest, RetryAndSuccess) {
  EXPECT_CALL(client_, UploadChromeDesktopReportProxy(_, _))
      .Times(2)
      .WillOnce(WithArgs<1>(policy::ScheduleStatusCallback(false)))
      .WillOnce(WithArgs<1>(policy::ScheduleStatusCallback(true)));
  CreateUploader(/* retry_count = */ 1);
  client_.SetStatus(policy::DM_STATUS_TEMPORARY_UNAVAILABLE);
  UploadReportAndSetExpectation(/*number_of_request=*/1,
                                ReportUploader::kSuccess);
  RunNextTask();

  // No response, request is retried.
  EXPECT_FALSE(has_responded_);
  RunNextTask();
  EXPECT_TRUE(has_responded_);
  ::testing::Mock::VerifyAndClearExpectations(&client_);
}

TEST_F(ReportUploaderTest, RetryAndFailedWithPersistentError) {
  EXPECT_CALL(client_, UploadChromeDesktopReportProxy(_, _))
      .Times(2)
      .WillRepeatedly(WithArgs<1>(policy::ScheduleStatusCallback(false)));
  CreateUploader(/* retry_count = */ 1);
  client_.SetStatus(policy::DM_STATUS_TEMPORARY_UNAVAILABLE);
  UploadReportAndSetExpectation(/*number_of_request=*/1,
                                ReportUploader::kPersistentError);
  RunNextTask();

  // No response, request is retried.
  EXPECT_FALSE(has_responded_);
  // Error is changed.
  client_.SetStatus(policy::DM_STATUS_SERVICE_DEVICE_NOT_FOUND);
  RunNextTask();
  EXPECT_TRUE(has_responded_);
  ::testing::Mock::VerifyAndClearExpectations(&client_);
}

TEST_F(ReportUploaderTest, RetryAndFailedWithTransientError) {
  EXPECT_CALL(client_, UploadChromeDesktopReportProxy(_, _))
      .Times(2)
      .WillRepeatedly(WithArgs<1>(policy::ScheduleStatusCallback(false)));
  CreateUploader(/* retry_count = */ 1);
  client_.SetStatus(policy::DM_STATUS_TEMPORARY_UNAVAILABLE);
  UploadReportAndSetExpectation(/*number_of_request=*/1,
                                ReportUploader::kTransientError);
  RunNextTask();

  // No response, request is retried.
  EXPECT_FALSE(has_responded_);
  RunNextTask();
  EXPECT_TRUE(has_responded_);
  ::testing::Mock::VerifyAndClearExpectations(&client_);
}

TEST_F(ReportUploaderTest, MultipleReports) {
  {
    InSequence s;
    // First report
    EXPECT_CALL(client_,
                UploadChromeDesktopReportProxy(
                    Property(&em::ChromeDesktopReportRequest::os_user_name,
                             Eq(kOsUserNames[0])),
                    _))
        .Times(3)
        .WillOnce(WithArgs<1>(policy::ScheduleStatusCallback(false)))
        .WillOnce(WithArgs<1>(policy::ScheduleStatusCallback(false)))
        .WillOnce(WithArgs<1>(policy::ScheduleStatusCallback(true)));
    // Second report
    EXPECT_CALL(client_,
                UploadChromeDesktopReportProxy(
                    Property(&em::ChromeDesktopReportRequest::os_user_name,
                             Eq(kOsUserNames[1])),
                    _))
        .Times(2)
        .WillOnce(WithArgs<1>(policy::ScheduleStatusCallback(false)))
        .WillOnce(WithArgs<1>(policy::ScheduleStatusCallback(false)));
  }
  CreateUploader(/* retry_count = */ 2);
  client_.SetStatus(policy::DM_STATUS_TEMPORARY_UNAVAILABLE);
  UploadReportAndSetExpectation(/*number_of_request=*/2,
                                ReportUploader::kTransientError);

  // The first request has no delay.
  VerifyRequestDelay(0);
  RunNextTask();

  // The first retry is delayed between 54 to 60 seconds.
  VerifyRequestDelay(60);
  RunNextTask();

  // The second retry is delayed between 108 to 120 seconds.
  VerifyRequestDelay(120);
  RunNextTask();

  // Request is succeeded, send the next request And its first retry is delayed
  // between 108 to 120 seconds because there were 2 failures.
  VerifyRequestDelay(120);
  RunNextTask();

  // And we failed again, reach maximum retries count.
  EXPECT_TRUE(has_responded_);

  ::testing::Mock::VerifyAndClearExpectations(&client_);
}

// Verified three DM server error that is transient.
TEST_P(ReportUploaderTestWithTransientError, WithoutRetry) {
  EXPECT_CALL(client_, UploadChromeDesktopReportProxy(_, _))
      .WillOnce(WithArgs<1>(policy::ScheduleStatusCallback(false)));
  client_.SetStatus(GetParam());
  UploadReportAndSetExpectation(/*number_of_request=*/2,
                                ReportUploader::kTransientError);
  scoped_task_environment_.FastForwardBy(base::TimeDelta());
  EXPECT_TRUE(has_responded_);
  ::testing::Mock::VerifyAndClearExpectations(&client_);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    ReportUploaderTestWithTransientError,
    ::testing::Values(policy::DM_STATUS_REQUEST_FAILED,
                      policy::DM_STATUS_TEMPORARY_UNAVAILABLE,
                      policy::DM_STATUS_SERVICE_DEVICE_ID_CONFLICT));

}  // namespace enterprise_reporting
