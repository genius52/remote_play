// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/video_capture/local_video_capturer_source.h"

#include <utility>

#include "base/bind.h"
#include "content/renderer/media/video_capture/video_capture_impl_manager.h"
#include "content/renderer/render_thread_impl.h"
#include "media/base/bind_to_current_loop.h"

namespace content {

LocalVideoCapturerSource::LocalVideoCapturerSource(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    int session_id)
    : session_id_(session_id),
      manager_(RenderThreadImpl::current()->video_capture_impl_manager()),
      release_device_cb_(manager_->UseDevice(session_id_)),
      task_runner_(std::move(task_runner)),
      weak_factory_(this) {
  DCHECK(RenderThreadImpl::current());
}

LocalVideoCapturerSource::~LocalVideoCapturerSource() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  std::move(release_device_cb_).Run();
}

media::VideoCaptureFormats LocalVideoCapturerSource::GetPreferredFormats() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return media::VideoCaptureFormats();
}

void LocalVideoCapturerSource::StartCapture(
    const media::VideoCaptureParams& params,
    const blink::VideoCaptureDeliverFrameCB& new_frame_callback,
    const RunningCallback& running_callback) {
  DCHECK(params.requested_format.IsValid());
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  running_callback_ = running_callback;

  stop_capture_cb_ = manager_->StartCapture(
      session_id_, params,
      media::BindToLoop(
          task_runner_,
          base::BindRepeating(&LocalVideoCapturerSource::OnStateUpdate,
                              weak_factory_.GetWeakPtr())),
      new_frame_callback);
}

void LocalVideoCapturerSource::RequestRefreshFrame() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!stop_capture_cb_)
    return;  // Do not request frames if the source is stopped.
  manager_->RequestRefreshFrame(session_id_);
}

void LocalVideoCapturerSource::MaybeSuspend() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  manager_->Suspend(session_id_);
}

void LocalVideoCapturerSource::Resume() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  manager_->Resume(session_id_);
}

void LocalVideoCapturerSource::StopCapture() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // Immediately make sure we don't provide more frames.
  if (stop_capture_cb_)
    std::move(stop_capture_cb_).Run();
}

void LocalVideoCapturerSource::OnFrameDropped(
    media::VideoCaptureFrameDropReason reason) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  manager_->OnFrameDropped(session_id_, reason);
}

void LocalVideoCapturerSource::OnLog(const std::string& message) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  manager_->OnLog(session_id_, message);
}

void LocalVideoCapturerSource::OnStateUpdate(blink::VideoCaptureState state) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (running_callback_.is_null()) {
    OnLog("LocalVideoCapturerSource::OnStateUpdate discarding state update.");
    return;
  }
  switch (state) {
    case blink::VIDEO_CAPTURE_STATE_STARTED:
      OnLog(
          "LocalVideoCapturerSource::OnStateUpdate signaling to "
          "consumer that source is now running.");
      running_callback_.Run(true);
      break;

    case blink::VIDEO_CAPTURE_STATE_STOPPING:
    case blink::VIDEO_CAPTURE_STATE_STOPPED:
    case blink::VIDEO_CAPTURE_STATE_ERROR:
    case blink::VIDEO_CAPTURE_STATE_ENDED:
      std::move(release_device_cb_).Run();
      release_device_cb_ = manager_->UseDevice(session_id_);
      OnLog(
          "LocalVideoCapturerSource::OnStateUpdate signaling to "
          "consumer that source is no longer running.");
      running_callback_.Run(false);
      break;

    case blink::VIDEO_CAPTURE_STATE_STARTING:
    case blink::VIDEO_CAPTURE_STATE_PAUSED:
    case blink::VIDEO_CAPTURE_STATE_RESUMED:
      // Not applicable to reporting on device starts or errors.
      break;
  }
}

// static
std::unique_ptr<media::VideoCapturerSource> LocalVideoCapturerSource::Create(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    int session_id) {
  return std::make_unique<LocalVideoCapturerSource>(std::move(task_runner),
                                                    session_id);
}

}  // namespace content
