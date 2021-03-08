// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_ARCORE_DEVICE_ARCORE_GL_H_
#define CHROME_BROWSER_ANDROID_VR_ARCORE_DEVICE_ARCORE_GL_H_

#include <memory>
#include <utility>
#include <vector>
#include "base/cancelable_callback.h"
#include "base/containers/queue.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/single_thread_task_runner.h"
#include "device/vr/public/mojom/isolated_xr_service.mojom.h"
#include "device/vr/public/mojom/vr_service.mojom.h"
#include "device/vr/util/fps_meter.h"
#include "mojo/public/cpp/bindings/associated_binding.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "ui/display/display.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/quaternion.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size_f.h"
#include "ui/gfx/native_widget_types.h"

namespace gfx {
class GpuFence;
}  // namespace gfx

namespace gl {
class GLContext;
class GLSurface;
}  // namespace gl

namespace vr {
class ArCoreInstallUtils;
class WebXrPresentationState;
}  // namespace vr

namespace device {

class ArCore;
class ArCoreFactory;
struct ArCoreHitTestRequest;
class ArImageTransport;

using ArCoreGlCreateSessionCallback = base::OnceCallback<void(
    mojom::XRFrameDataProviderPtrInfo frame_data_provider_info,
    mojom::VRDisplayInfoPtr display_info,
    mojom::XRSessionControllerPtrInfo session_controller_info,
    mojom::XRPresentationConnectionPtr presentation_connection)>;

// All of this class's methods must be called on the same valid GL thread with
// the exception of GetGlThreadTaskRunner() and GetWeakPtr().
class ArCoreGl : public mojom::XRFrameDataProvider,
                 public mojom::XRPresentationProvider,
                 public mojom::XREnvironmentIntegrationProvider,
                 public mojom::XRSessionController {
 public:
  explicit ArCoreGl(std::unique_ptr<ArImageTransport> ar_image_transport);
  ~ArCoreGl() override;

  void Initialize(vr::ArCoreInstallUtils* install_utils,
                  ArCoreFactory* arcore_factory,
                  gfx::AcceleratedWidget drawing_widget,
                  const gfx::Size& frame_size,
                  display::Display::Rotation display_rotation,
                  base::OnceCallback<void(bool)> callback);

  void CreateSession(mojom::VRDisplayInfoPtr display_info,
                     ArCoreGlCreateSessionCallback create_callback,
                     base::OnceClosure shutdown_callback);

  const scoped_refptr<base::SingleThreadTaskRunner>& GetGlThreadTaskRunner() {
    return gl_thread_task_runner_;
  }

  // mojom::XRFrameDataProvider
  void GetFrameData(mojom::XRFrameDataRequestOptionsPtr options,
                    GetFrameDataCallback callback) override;

  void GetEnvironmentIntegrationProvider(
      mojom::XREnvironmentIntegrationProviderAssociatedRequest
          environment_provider) override;

  // XRPresentationProvider
  void SubmitFrameMissing(int16_t frame_index, const gpu::SyncToken&) override;
  void SubmitFrame(int16_t frame_index,
                   const gpu::MailboxHolder& mailbox,
                   base::TimeDelta time_waited) override;
  void SubmitFrameWithTextureHandle(int16_t frame_index,
                                    mojo::ScopedHandle texture_handle) override;
  void SubmitFrameDrawnIntoTexture(int16_t frame_index,
                                   const gpu::SyncToken&,
                                   base::TimeDelta time_waited) override;
  void UpdateLayerBounds(int16_t frame_index,
                         const gfx::RectF& left_bounds,
                         const gfx::RectF& right_bounds,
                         const gfx::Size& source_size) override;

  void RequestHitTest(
      mojom::XRRayPtr,
      mojom::XREnvironmentIntegrationProvider::RequestHitTestCallback) override;

  // mojom::XRSessionController
  void SetFrameDataRestricted(bool restricted) override;

  void OnWebXrTokenSignaled(int16_t frame_index,
                            std::unique_ptr<gfx::GpuFence> gpu_fence);

  void OnScreenTouch(bool touching, const gfx::PointF& touch_point);
  mojom::XRInputSourceStatePtr GetInputSourceState();

  base::WeakPtr<ArCoreGl> GetWeakPtr();

 private:
  void Pause();
  void Resume();

  bool IsSubmitFrameExpected(int16_t frame_index);
  void ProcessFrame(mojom::XRFrameDataPtr frame_data,
                    mojom::XRFrameDataProvider::GetFrameDataCallback callback);

  bool InitializeGl(gfx::AcceleratedWidget drawing_widget);
  bool IsOnGlThread() const;

  base::OnceClosure session_shutdown_callback_;

  scoped_refptr<gl::GLSurface> surface_;
  scoped_refptr<gl::GLContext> context_;
  scoped_refptr<base::SingleThreadTaskRunner> gl_thread_task_runner_;

  // Created on GL thread and should only be accessed on that thread.
  std::unique_ptr<ArCore> arcore_;
  std::unique_ptr<ArImageTransport> ar_image_transport_;

  // This class uses the same overall presentation state logic
  // as GvrGraphicsDelegate, with some difference due to drawing
  // camera images even on frames with no pose and therefore
  // no blink-generated rendered image.
  //
  // Rough sequence is:
  //
  // SubmitFrame N                 N animating->processing
  //   draw camera N
  //   waitForToken
  // GetFrameData N+1              N+1 start animating
  //   update ARCore N to N+1
  // OnToken N                     N processing->rendering
  //   draw rendered N
  //   swap                        N rendering done
  // SubmitFrame N+1               N+1 animating->processing
  //   draw camera N+1
  //   waitForToken
  std::unique_ptr<vr::WebXrPresentationState> webxr_;

  // Default dummy values to ensure consistent behaviour.
  gfx::Size transfer_size_ = gfx::Size(0, 0);
  display::Display::Rotation display_rotation_ = display::Display::ROTATE_0;
  bool should_update_display_geometry_ = true;

  gfx::Transform uv_transform_;
  gfx::Transform webxr_transform_;
  gfx::Transform projection_;
  gfx::Transform inverse_projection_;
  // The first run of ProduceFrame should set uv_transform_ and projection_
  // using the default settings in ArCore.
  bool should_recalculate_uvs_ = true;
  bool have_camera_image_ = false;

  bool is_initialized_ = false;
  bool is_paused_ = true;

  bool restrict_frame_data_ = false;

  FPSMeter fps_meter_;

  std::vector<std::unique_ptr<ArCoreHitTestRequest>> hit_test_requests_;

  mojo::Binding<mojom::XRFrameDataProvider> frame_data_binding_;
  mojo::Binding<mojom::XRSessionController> session_controller_binding_;
  mojo::AssociatedBinding<mojom::XREnvironmentIntegrationProvider>
      environment_binding_;

  void OnBindingDisconnect();
  void CloseBindingsIfOpen();

  mojo::Binding<device::mojom::XRPresentationProvider> presentation_binding_;
  device::mojom::XRPresentationClientPtr submit_client_;

  base::OnceClosure pending_getframedata_;

  mojom::VRDisplayInfoPtr display_info_;
  bool display_info_changed_ = false;

  std::vector<device::mojom::XRInputSourceStatePtr> input_states_;
  gfx::PointF screen_last_touch_;

  // Screen touch start/end events get reported asynchronously. We want to
  // report at least one "clicked" event even if start and end happen within a
  // single frame. The "active" state corresponds to the current state and is
  // updated asynchronously. The "pending" state is set to true whenever the
  // screen is touched, but only gets cleared by the input source handler.
  //
  //    active   pending    event
  //         0         0
  //         1         1
  //         1         1    pressed=true (selectstart)
  //         1         1    pressed=true
  //         0         1->0 pressed=false clicked=true (selectend, click)
  //
  //         0         0
  //         1         1
  //         0         1
  //         0         1->0 pressed=false clicked=true (selectend, click)
  float screen_touch_pending_ = false;
  float screen_touch_active_ = false;

  // Must be last.
  base::WeakPtrFactory<ArCoreGl> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(ArCoreGl);
};

}  // namespace device

#endif  // CHROME_BROWSER_ANDROID_VR_ARCORE_DEVICE_ARCORE_GL_H_
