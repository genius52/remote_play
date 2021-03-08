// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_INPUT_SYNTHETIC_GESTURE_TARGET_BASE_H_
#define CONTENT_BROWSER_RENDERER_HOST_INPUT_SYNTHETIC_GESTURE_TARGET_BASE_H_

#include "base/macros.h"
#include "base/time/time.h"
#include "content/browser/renderer_host/input/synthetic_gesture_target.h"
#include "content/common/content_export.h"

namespace ui {
class LatencyInfo;
}

namespace blink {
class WebTouchEvent;
class WebMouseEvent;
class WebMouseWheelEvent;
class WebGestureEvent;
}

namespace content {

class RenderWidgetHostImpl;

class CONTENT_EXPORT SyntheticGestureTargetBase :
    public SyntheticGestureTarget {
 public:
  explicit SyntheticGestureTargetBase(RenderWidgetHostImpl* host);
  ~SyntheticGestureTargetBase() override;

  virtual void DispatchWebTouchEventToPlatform(
      const blink::WebTouchEvent& web_touch,
      const ui::LatencyInfo& latency_info) = 0;

  virtual void DispatchWebMouseWheelEventToPlatform(
      const blink::WebMouseWheelEvent& web_wheel,
      const ui::LatencyInfo& latency_info) = 0;

  virtual void DispatchWebGestureEventToPlatform(
      const blink::WebGestureEvent& web_gesture,
      const ui::LatencyInfo& latency_info) = 0;

  virtual void DispatchWebMouseEventToPlatform(
      const blink::WebMouseEvent& web_mouse,
      const ui::LatencyInfo& latency_info) = 0;

  // SyntheticGestureTarget:
  void DispatchInputEventToPlatform(const blink::WebInputEvent& event) override;

  base::TimeDelta PointerAssumedStoppedTime() const override;

  float GetSpanSlopInDips() const override;

  int GetMouseWheelMinimumGranularity() const override;

  void WaitForTargetAck(SyntheticGestureParams::GestureType type,
                        SyntheticGestureParams::GestureSourceType source,
                        base::OnceClosure callback) const override;

 protected:
  RenderWidgetHostImpl* render_widget_host() const { return host_; }

 private:
  bool PointIsWithinContents(int x, int y) const;

  RenderWidgetHostImpl* host_;

  DISALLOW_COPY_AND_ASSIGN(SyntheticGestureTargetBase);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_INPUT_SYNTHETIC_GESTURE_TARGET_BASE_H_
