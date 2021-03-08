// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/run_loop.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/service_manager_connection.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "media/base/media_switches.h"
#include "services/service_manager/public/cpp/connector.h"
#include "services/video_capture/public/cpp/mock_receiver.h"
#include "services/video_capture/public/mojom/constants.mojom.h"
#include "services/video_capture/public/mojom/device.mojom.h"
#include "services/video_capture/public/mojom/device_factory.mojom.h"
#include "services/video_capture/public/mojom/device_factory_provider.mojom.h"
#include "services/video_capture/public/mojom/video_source.mojom.h"
#include "services/video_capture/public/mojom/video_source_provider.mojom.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;

namespace content {

namespace {

enum class ServiceApi { kSingleClient, kMultiClient };

struct TestParams {
  ServiceApi api_to_use;
  media::VideoCaptureBufferType buffer_type_to_request;

  media::mojom::VideoBufferHandle::Tag GetExpectedBufferHandleTag() const {
    switch (buffer_type_to_request) {
      case media::VideoCaptureBufferType::kSharedMemory:
        return media::mojom::VideoBufferHandle::Tag::SHARED_BUFFER_HANDLE;
      case media::VideoCaptureBufferType::kSharedMemoryViaRawFileDescriptor:
        return media::mojom::VideoBufferHandle::Tag::
            SHARED_MEMORY_VIA_RAW_FILE_DESCRIPTOR;
      case media::VideoCaptureBufferType::kMailboxHolder:
        NOTREACHED();
        return media::mojom::VideoBufferHandle::Tag::SHARED_BUFFER_HANDLE;
    }
  }
};

static const char kVideoCaptureHtmlFile[] = "/media/video_capture_test.html";
static const char kStartVideoCaptureAndVerify[] =
    "startVideoCaptureAndVerifySize(%d, %d)";
static const gfx::Size kVideoSize(320, 200);

}  // namespace

// Integration test sets up a single fake device and obtains a connection to the
// video capture service via the Browser process' service manager. It then
// opens the device from clients. One client is the test calling into the
// video capture service directly. The second client is the Browser, which the
// test exercises through JavaScript.
class WebRtcVideoCaptureSharedDeviceBrowserTest
    : public ContentBrowserTest,
      public testing::WithParamInterface<TestParams> {
 public:
  WebRtcVideoCaptureSharedDeviceBrowserTest() : weak_factory_(this) {
    scoped_feature_list_.InitAndEnableFeature(features::kMojoVideoCapture);
  }

  ~WebRtcVideoCaptureSharedDeviceBrowserTest() override {}

  void OpenDeviceViaService() {
    connector_->BindInterface(video_capture::mojom::kServiceName,
                              &device_factory_provider_);
    switch (GetParam().api_to_use) {
      case ServiceApi::kSingleClient:
        device_factory_provider_->ConnectToDeviceFactory(
            mojo::MakeRequest(&device_factory_));
        device_factory_->GetDeviceInfos(base::BindOnce(
            &WebRtcVideoCaptureSharedDeviceBrowserTest::OnDeviceInfosReceived,
            weak_factory_.GetWeakPtr(), GetParam().buffer_type_to_request));
        break;
      case ServiceApi::kMultiClient:
        device_factory_provider_->ConnectToVideoSourceProvider(
            mojo::MakeRequest(&video_source_provider_));
        video_source_provider_->GetSourceInfos(base::BindOnce(
            &WebRtcVideoCaptureSharedDeviceBrowserTest::OnSourceInfosReceived,
            weak_factory_.GetWeakPtr(), GetParam().buffer_type_to_request));
        break;
    }
  }

  void OpenDeviceInRendererAndWaitForPlaying() {
    DCHECK(main_task_runner_->RunsTasksInCurrentSequence());
    embedded_test_server()->StartAcceptingConnections();
    GURL url(embedded_test_server()->GetURL(kVideoCaptureHtmlFile));
    NavigateToURL(shell(), url);

    const std::string javascript_to_execute = base::StringPrintf(
        kStartVideoCaptureAndVerify, kVideoSize.width(), kVideoSize.height());
    std::string result;
    // Start video capture and wait until it started rendering
    ASSERT_TRUE(
        ExecuteScriptAndExtractString(shell(), javascript_to_execute, &result));
    ASSERT_EQ("OK", result);
  }

 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch(switches::kUseFakeDeviceForMediaStream);
    command_line->AppendSwitch(switches::kUseFakeUIForMediaStream);
  }

  void SetUp() override {
    ASSERT_TRUE(embedded_test_server()->InitializeAndListen());
    EnablePixelOutput();
    ContentBrowserTest::SetUp();
  }

  void Initialize() {
    DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
    main_task_runner_ = base::ThreadTaskRunnerHandle::Get();

    auto* connection = content::ServiceManagerConnection::GetForProcess();
    ASSERT_TRUE(connection);
    auto* connector = connection->GetConnector();
    ASSERT_TRUE(connector);
    // We need to clone it so that we can use the clone on a different thread.
    connector_ = connector->Clone();

    mock_receiver_ = std::make_unique<video_capture::MockReceiver>(
        mojo::MakeRequest(&receiver_proxy_));
  }

  scoped_refptr<base::TaskRunner> main_task_runner_;
  std::unique_ptr<service_manager::Connector> connector_;
  std::unique_ptr<video_capture::MockReceiver> mock_receiver_;

 private:
  void OnDeviceInfosReceived(
      media::VideoCaptureBufferType buffer_type_to_request,
      const std::vector<media::VideoCaptureDeviceInfo>& infos) {
    ASSERT_FALSE(infos.empty());
    device_factory_->CreateDevice(
        infos[0].descriptor.device_id, mojo::MakeRequest(&device_),
        base::BindOnce(
            &WebRtcVideoCaptureSharedDeviceBrowserTest::OnCreateDeviceCallback,
            weak_factory_.GetWeakPtr(), infos, buffer_type_to_request));
  }

  void OnCreateDeviceCallback(
      const std::vector<media::VideoCaptureDeviceInfo>& infos,
      media::VideoCaptureBufferType buffer_type_to_request,
      video_capture::mojom::DeviceAccessResultCode result_code) {
    ASSERT_EQ(video_capture::mojom::DeviceAccessResultCode::SUCCESS,
              result_code);

    media::VideoCaptureParams requestable_settings;
    ASSERT_FALSE(infos[0].supported_formats.empty());
    requestable_settings.requested_format = infos[0].supported_formats[0];
    requestable_settings.requested_format.frame_size = kVideoSize;
    requestable_settings.buffer_type = buffer_type_to_request;

    device_->Start(requestable_settings, std::move(receiver_proxy_));
  }

  void OnSourceInfosReceived(
      media::VideoCaptureBufferType buffer_type_to_request,
      const std::vector<media::VideoCaptureDeviceInfo>& infos) {
    ASSERT_FALSE(infos.empty());
    video_source_provider_->GetVideoSource(infos[0].descriptor.device_id,
                                           mojo::MakeRequest(&video_source_));

    media::VideoCaptureParams requestable_settings;
    ASSERT_FALSE(infos[0].supported_formats.empty());
    requestable_settings.requested_format = infos[0].supported_formats[0];
    requestable_settings.requested_format.frame_size = kVideoSize;
    requestable_settings.buffer_type = buffer_type_to_request;

    video_capture::mojom::PushVideoStreamSubscriptionPtr subscription;
    video_source_->CreatePushSubscription(
        std::move(receiver_proxy_), requestable_settings,
        false /*force_reopen_with_new_settings*/,
        mojo::MakeRequest(&subscription_),
        base::BindOnce(&WebRtcVideoCaptureSharedDeviceBrowserTest::
                           OnCreatePushSubscriptionCallback,
                       weak_factory_.GetWeakPtr()));
  }

  void OnCreatePushSubscriptionCallback(
      video_capture::mojom::CreatePushSubscriptionResultCode result_code,
      const media::VideoCaptureParams& params) {
    ASSERT_NE(video_capture::mojom::CreatePushSubscriptionResultCode::kFailed,
              result_code);
    subscription_->Activate();
  }

  base::test::ScopedFeatureList scoped_feature_list_;
  video_capture::mojom::DeviceFactoryProviderPtr device_factory_provider_;

  // For single-client API case only
  video_capture::mojom::DeviceFactoryPtr device_factory_;
  video_capture::mojom::DevicePtr device_;

  // For multi-client API case only
  video_capture::mojom::VideoSourceProviderPtr video_source_provider_;
  video_capture::mojom::VideoSourcePtr video_source_;
  video_capture::mojom::PushVideoStreamSubscriptionPtr subscription_;

  video_capture::mojom::ReceiverPtr receiver_proxy_;
  base::WeakPtrFactory<WebRtcVideoCaptureSharedDeviceBrowserTest> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(WebRtcVideoCaptureSharedDeviceBrowserTest);
};

// Tests that a single fake video capture device can be opened via JavaScript
// by the Renderer while it is already in use by a direct client of the
// video capture service.
IN_PROC_BROWSER_TEST_P(
    WebRtcVideoCaptureSharedDeviceBrowserTest,
    ReceiveFrameInRendererWhileDeviceAlreadyInUseViaDirectServiceClient) {
  Initialize();

  base::RunLoop receive_frame_from_service_wait_loop;
  auto expected_buffer_handle_tag = GetParam().GetExpectedBufferHandleTag();
  ON_CALL(*mock_receiver_, DoOnNewBuffer(_, _))
      .WillByDefault(Invoke(
          [expected_buffer_handle_tag](
              int32_t, media::mojom::VideoBufferHandlePtr* buffer_handle) {
            ASSERT_EQ(expected_buffer_handle_tag, (*buffer_handle)->which());
          }));
  EXPECT_CALL(*mock_receiver_, DoOnFrameReadyInBuffer(_, _, _, _))
      .WillOnce(InvokeWithoutArgs([&receive_frame_from_service_wait_loop]() {
        receive_frame_from_service_wait_loop.Quit();
      }))
      .WillRepeatedly(Return());

  OpenDeviceViaService();
  // Note, if we do not wait for the first frame to arrive before opening the
  // device in the Renderer, it could happen that the Renderer takes ove access
  // to the device before a first frame is received by mock_receiver_.
  receive_frame_from_service_wait_loop.Run();

  OpenDeviceInRendererAndWaitForPlaying();
}

// Tests that a single fake video capture device can be opened by a direct
// client of the video capture service while it is already in use via JavaScript
// by the Renderer.
IN_PROC_BROWSER_TEST_P(
    WebRtcVideoCaptureSharedDeviceBrowserTest,
    ReceiveFrameViaDirectServiceClientWhileDeviceAlreadyInUseViaRenderer) {
  Initialize();

  base::RunLoop receive_frame_from_service_wait_loop;
  auto expected_buffer_handle_tag = GetParam().GetExpectedBufferHandleTag();
  ON_CALL(*mock_receiver_, DoOnNewBuffer(_, _))
      .WillByDefault(Invoke(
          [expected_buffer_handle_tag](
              int32_t, media::mojom::VideoBufferHandlePtr* buffer_handle) {
            ASSERT_EQ(expected_buffer_handle_tag, (*buffer_handle)->which());
          }));
  EXPECT_CALL(*mock_receiver_, DoOnFrameReadyInBuffer(_, _, _, _))
      .WillOnce(InvokeWithoutArgs([&receive_frame_from_service_wait_loop]() {
        receive_frame_from_service_wait_loop.Quit();
      }))
      .WillRepeatedly(Return());

  OpenDeviceInRendererAndWaitForPlaying();

  OpenDeviceViaService();
  receive_frame_from_service_wait_loop.Run();
}

INSTANTIATE_TEST_SUITE_P(
    ,
    WebRtcVideoCaptureSharedDeviceBrowserTest,
    ::testing::Values(
        TestParams{ServiceApi::kSingleClient,
                   media::VideoCaptureBufferType::kSharedMemory},
        TestParams {
          ServiceApi::kMultiClient, media::VideoCaptureBufferType::kSharedMemory
        }
#if defined(OS_LINUX)
        ,
        TestParams{
            ServiceApi::kSingleClient,
            media::VideoCaptureBufferType::kSharedMemoryViaRawFileDescriptor},
        TestParams {
          ServiceApi::kMultiClient,
              media::VideoCaptureBufferType::kSharedMemoryViaRawFileDescriptor
        }
#endif  // defined(OS_LINUX)
        ));

}  // namespace content