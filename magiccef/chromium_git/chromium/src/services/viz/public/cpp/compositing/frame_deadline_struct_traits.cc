// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/viz/public/cpp/compositing/frame_deadline_struct_traits.h"

namespace mojo {

// static
bool StructTraits<viz::mojom::FrameDeadlineDataView, viz::FrameDeadline>::Read(
    viz::mojom::FrameDeadlineDataView data,
    viz::FrameDeadline* out) {
  base::TimeTicks frame_start_time;
  base::TimeDelta frame_interval;
  if (!data.ReadFrameStartTime(&frame_start_time) ||
      !data.ReadFrameInterval(&frame_interval)) {
    return false;
  }
  *out = viz::FrameDeadline(frame_start_time, data.deadline_in_frames(),
                            frame_interval,
                            data.use_default_lower_bound_deadline());
  return true;
}

}  // namespace mojo
