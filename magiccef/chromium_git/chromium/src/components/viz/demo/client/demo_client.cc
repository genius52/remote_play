// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/demo/client/demo_client.h"

#include <memory>
#include <utility>
#include <vector>

#include "components/viz/common/quads/compositor_frame.h"
#include "components/viz/common/quads/solid_color_draw_quad.h"
#include "components/viz/common/quads/surface_draw_quad.h"

namespace demo {

DemoClient::DemoClient(const viz::FrameSinkId& frame_sink_id,
                       const viz::LocalSurfaceIdAllocation& local_surface_id,
                       const gfx::Rect& bounds)
    : thread_(frame_sink_id.ToString()),
      frame_sink_id_(frame_sink_id),
      local_surface_id_(local_surface_id),
      bounds_(bounds),
      binding_(this) {
  CHECK(thread_.Start());
}

DemoClient::~DemoClient() = default;

void DemoClient::Initialize(
    viz::mojom::CompositorFrameSinkClientRequest request,
    viz::mojom::CompositorFrameSinkAssociatedPtrInfo sink_info) {
  thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&DemoClient::InitializeOnThread, base::Unretained(this),
                     std::move(request), std::move(sink_info), nullptr));
}

void DemoClient::Initialize(
    viz::mojom::CompositorFrameSinkClientRequest request,
    viz::mojom::CompositorFrameSinkPtrInfo sink_info) {
  thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&DemoClient::InitializeOnThread, base::Unretained(this),
                     std::move(request), nullptr, std::move(sink_info)));
}

viz::LocalSurfaceIdAllocation DemoClient::Embed(
    const viz::FrameSinkId& frame_sink_id,
    const gfx::Rect& bounds) {
  // |embeds_| is used on the client-thread in CreateFrame(). So this needs to
  // be mutated under a lock.
  base::AutoLock lock(lock_);
  allocator_.GenerateId();
  embeds_[frame_sink_id] = {allocator_.GetCurrentLocalSurfaceIdAllocation(),
                            bounds};
  return embeds_[frame_sink_id].lsid;
}

void DemoClient::Resize(const gfx::Size& size,
                        const viz::LocalSurfaceIdAllocation& local_surface_id) {
  // |bounds_| and |local_surface_id_| are used on the client-thread in
  // CreateFrame(). So these need to be mutated under a lock.
  base::AutoLock lock(lock_);
  bounds_.set_size(size);
  local_surface_id_ = local_surface_id;
}

viz::CompositorFrame DemoClient::CreateFrame(const viz::BeginFrameArgs& args) {
  constexpr SkColor colors[] = {SK_ColorRED, SK_ColorGREEN, SK_ColorYELLOW};
  viz::CompositorFrame frame;

  frame.metadata.begin_frame_ack = viz::BeginFrameAck(args, true);
  frame.metadata.device_scale_factor = 1.f;
  frame.metadata.local_surface_id_allocation_time =
      local_surface_id_.allocation_time();
  frame.metadata.frame_token = ++next_frame_token_;

  const int kRenderPassId = 1;
  const gfx::Rect& output_rect = bounds_;
  const gfx::Rect& damage_rect = output_rect;
  std::unique_ptr<viz::RenderPass> render_pass = viz::RenderPass::Create();
  render_pass->SetNew(kRenderPassId, output_rect, damage_rect,
                      gfx::Transform());

  // The content of the client is one big solid-color rectangle, which includes
  // the other clients above it (in z-order). The embedded clients are first
  // added to the CompositorFrame using their SurfaceId (i.e. the FrameSinkId
  // and LocalSurfaceId), and then the big rectangle is added afterwards.
  for (auto& iter : embeds_) {
    const gfx::Rect& child_bounds = iter.second.bounds;
    const gfx::Vector2dF center(child_bounds.width() / 2,
                                child_bounds.height() / 2);

    // Apply a rotation so there's visual-update every frame in the demo.
    gfx::Transform transform;
    transform.Translate(center + child_bounds.OffsetFromOrigin());
    transform.Rotate(iter.second.degrees);
    iter.second.degrees += 0.3;
    transform.Translate(-center);

    viz::SharedQuadState* quad_state =
        render_pass->CreateAndAppendSharedQuadState();
    quad_state->SetAll(
        transform,
        /*quad_layer_rect=*/child_bounds,
        /*visible_quad_layer_rect=*/child_bounds,
        /*rounded_corner_bounds=*/gfx::RRectF(),
        /*clip_rect=*/gfx::Rect(),
        /*is_clipped=*/false, /*are_contents_opaque=*/false, /*opacity=*/1.f,
        /*blend_mode=*/SkBlendMode::kSrcOver, /*sorting_context_id=*/0);

    viz::SurfaceDrawQuad* embed =
        render_pass->CreateAndAppendDrawQuad<viz::SurfaceDrawQuad>();
    viz::SurfaceId surface_id(iter.first, iter.second.lsid.local_surface_id());
    // |rect| and |visible_rect| needs to be in the quad's coord-space, so to
    // draw the whole quad, it needs to use origin (0, 0).
    embed->SetNew(quad_state,
                  /*rect=*/gfx::Rect(child_bounds.size()),
                  /*visible_rect=*/gfx::Rect(child_bounds.size()),
                  viz::SurfaceRange(surface_id), SK_ColorGRAY,
                  /*stretch_content_to_fill_bounds=*/false,
                  /*ignores_input_event=*/false);
  }

  // Add a solid-color draw-quad for the big rectangle covering the entire
  // content-area of the client.
  viz::SharedQuadState* quad_state =
      render_pass->CreateAndAppendSharedQuadState();
  quad_state->SetAll(
      gfx::Transform(),
      /*quad_layer_rect=*/output_rect,
      /*visible_quad_layer_rect=*/output_rect,
      /*rounded_corner_bounds=*/gfx::RRectF(),
      /*clip_rect=*/gfx::Rect(),
      /*is_clipped=*/false, /*are_contents_opaque=*/false, /*opacity=*/1.f,
      /*blend_mode=*/SkBlendMode::kSrcOver, /*sorting_context_id=*/0);

  viz::SolidColorDrawQuad* color_quad =
      render_pass->CreateAndAppendDrawQuad<viz::SolidColorDrawQuad>();
  color_quad->SetNew(quad_state, output_rect, output_rect,
                     colors[(++frame_count_ / 60) % base::size(colors)], false);

  frame.render_pass_list.push_back(std::move(render_pass));

  return frame;
}

viz::mojom::CompositorFrameSink* DemoClient::GetPtr() {
  if (associated_sink_.is_bound())
    return associated_sink_.get();
  return sink_.get();
}

void DemoClient::InitializeOnThread(
    viz::mojom::CompositorFrameSinkClientRequest request,
    viz::mojom::CompositorFrameSinkAssociatedPtrInfo associated_sink_info,
    viz::mojom::CompositorFrameSinkPtrInfo sink_info) {
  binding_.Bind(std::move(request));
  if (associated_sink_info)
    associated_sink_.Bind(std::move(associated_sink_info));
  else
    sink_.Bind(std::move(sink_info));
  // Request for begin-frames.
  GetPtr()->SetNeedsBeginFrame(true);
}

void DemoClient::DidReceiveCompositorFrameAck(
    const std::vector<viz::ReturnedResource>& resources) {
  // See documentation in mojom for how this can be used.
}

void DemoClient::OnBeginFrame(const viz::BeginFrameArgs& args,
                              const viz::PresentationFeedbackMap& feedbacks) {
  // Generate a new compositor-frame for each begin-frame. This demo client
  // generates and submits the compositor-frame immediately. But it is possible
  // for the client to delay sending the compositor-frame. |args| includes the
  // deadline for the client before it needs to submit the compositor-frame.
  base::AutoLock lock(lock_);
  GetPtr()->SubmitCompositorFrame(local_surface_id_.local_surface_id(),
                                  CreateFrame(args),
                                  base::Optional<viz::HitTestRegionList>(),
                                  /*trace_time=*/0);
}
void DemoClient::OnBeginFramePausedChanged(bool paused) {}
void DemoClient::ReclaimResources(
    const std::vector<viz::ReturnedResource>& resources) {}

}  // namespace demo
