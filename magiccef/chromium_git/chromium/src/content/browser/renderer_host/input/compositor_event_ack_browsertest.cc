// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "content/browser/renderer_host/input/synthetic_smooth_scroll_gesture.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_input_event_router.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/common/input/synthetic_web_input_event_builders.h"
#include "content/common/input_messages.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/hit_test_region_observer.h"
#include "content/public/test/test_utils.h"
#include "content/shell/browser/shell.h"
#include "third_party/blink/public/platform/web_input_event.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event_switches.h"
#include "ui/latency/latency_info.h"

using blink::WebInputEvent;

namespace {

// This value has to be larger than the height of the device this test is
// executed on, otherwise the device will be unable to scroll thus failing
// tests.
const int kWebsiteHeight = 10000;

// The event listeners will block the renderer's main thread for both wheel and
// touchstart. This will lead to the compositor impl thread to perform
// scrolling.
const char kCompositorEventAckDataURL[] =
    "data:text/html;charset=utf-8,"
    "<!DOCTYPE html>"
    "<meta name='viewport' content='width=device-width'/>"
    "<style>"
    "html, body {"
    "  margin: 0;"
    "}"
    ".spacer { height: 10000px; }"
    "</style>"
    "<div class=spacer></div>"
    "<script>"
    "  document.addEventListener('wheel', function(e) { while(true) {} }, "
    "{'passive': true});"
    "  document.addEventListener('touchstart', function(e) { while(true) {} }, "
    "{'passive': true});"
    "  document.title='ready';"
    "</script>";

// The event listeners will block the renderer's main thread for both
// touchstart and touchend.
const char kPassiveTouchStartBlockingTouchEndDataURL[] =
    "data:text/html;charset=utf-8,"
    "<!DOCTYPE html>"
    "<meta name='viewport' content='width=device-width'/>"
    "<style>"
    "html, body {"
    "  margin: 0;"
    "}"
    ".spacer { height: 10000px; }"
    "</style>"
    "<div class=spacer></div>"
    "<script>"
    "  document.addEventListener('touchstart', function(e) { while(true) {} }, "
    "{'passive': true});"
    "  document.addEventListener('touchend', function(e) { while(true) {} });"
    "  document.title='ready';"
    "</script>";

// The event listeners will block the renderer's main thread for touchstart.
const char kBlockingTouchStartDataURL[] =
    "data:text/html;charset=utf-8,"
    "<!DOCTYPE html>"
    "<meta name='viewport' content='width=device-width'/>"
    "<style>"
    "html, body {"
    "  margin: 0;"
    "}"
    ".spacer { height: 10000px; }"
    "</style>"
    "<div class=spacer></div>"
    "<script>"
    "  document.addEventListener('touchstart', function(e) { while(true) {} }, "
    "{'passive': false});"
    "  document.title='ready';"
    "</script>";

}  // namespace

namespace content {

// This test will used event listeners which block the renderer's main thread.
// This test verifies that the compositor sends back an event ack that is not
// blocked by the main thread. Then that subsequently the compositor will
// perform scrolling from the impl thread.
class CompositorEventAckBrowserTest : public ContentBrowserTest {
 public:
  CompositorEventAckBrowserTest() {}
  ~CompositorEventAckBrowserTest() override {}

  RenderWidgetHostImpl* GetWidgetHost() {
    return RenderWidgetHostImpl::From(
        shell()->web_contents()->GetRenderViewHost()->GetWidget());
  }

  void OnSyntheticGestureCompleted(SyntheticGesture::Result result) {
    EXPECT_EQ(SyntheticGesture::GESTURE_FINISHED, result);
  }

 protected:
  void LoadURL(const char* page_data) {
    const GURL data_url(page_data);
    NavigateToURL(shell(), data_url);

    RenderWidgetHostImpl* host = GetWidgetHost();
    host->GetView()->SetSize(gfx::Size(400, 400));

    base::string16 ready_title(base::ASCIIToUTF16("ready"));
    TitleWatcher watcher(shell()->web_contents(), ready_title);
    ignore_result(watcher.WaitAndGetTitle());

    HitTestRegionObserver observer(host->GetFrameSinkId());
    observer.WaitForHitTestData();
  }

  int ExecuteScriptAndExtractInt(const std::string& script) {
    int value = 0;
    EXPECT_TRUE(content::ExecuteScriptAndExtractInt(
        shell(), "domAutomationController.send(" + script + ")", &value));
    return value;
  }

  int GetScrollTop() {
    return ExecuteScriptAndExtractInt("document.scrollingElement.scrollTop");
  }

  void DoWheelScroll() {
    EXPECT_EQ(0, GetScrollTop());

    int scrollHeight =
        ExecuteScriptAndExtractInt("document.documentElement.scrollHeight");
    EXPECT_EQ(kWebsiteHeight, scrollHeight);

    RenderFrameSubmissionObserver observer(
        GetWidgetHost()->render_frame_metadata_provider());
    auto input_msg_watcher = std::make_unique<InputMsgWatcher>(
        GetWidgetHost(), blink::WebInputEvent::kMouseWheel);

    // This event never completes its processing. As kCompositorEventAckDataURL
    // will block the renderer's main thread once it is received.
    blink::WebMouseWheelEvent wheel_event =
        SyntheticWebMouseWheelEventBuilder::Build(10, 10, 0, -53, 0, true);
    wheel_event.phase = blink::WebMouseWheelEvent::kPhaseBegan;
    GetWidgetHost()->ForwardWheelEvent(wheel_event);

    // The compositor should send the event ack, and not be blocked by the event
    // above. The event watcher runs until we get the InputMsgAck callback
    EXPECT_EQ(INPUT_EVENT_ACK_STATE_SET_NON_BLOCKING,
              input_msg_watcher->WaitForAck());

    // Expect that the compositor scrolled at least one pixel while the
    // main thread was in a busy loop.
    gfx::Vector2dF default_scroll_offset;
    while (observer.LastRenderFrameMetadata()
               .root_scroll_offset.value_or(default_scroll_offset)
               .y() <= 0) {
      observer.WaitForMetadataChange();
    }
  }

  void DoTouchScroll() {
    EXPECT_EQ(0, GetScrollTop());

    int scrollHeight =
        ExecuteScriptAndExtractInt("document.documentElement.scrollHeight");
    EXPECT_EQ(kWebsiteHeight, scrollHeight);

    RenderFrameSubmissionObserver observer(
        GetWidgetHost()->render_frame_metadata_provider());

    SyntheticSmoothScrollGestureParams params;
    params.gesture_source_type = SyntheticGestureParams::TOUCH_INPUT;
    params.anchor = gfx::PointF(50, 50);
    params.distances.push_back(gfx::Vector2d(0, -45));

    std::unique_ptr<SyntheticSmoothScrollGesture> gesture(
        new SyntheticSmoothScrollGesture(params));
    GetWidgetHost()->QueueSyntheticGesture(
        std::move(gesture),
        base::BindOnce(
            &CompositorEventAckBrowserTest::OnSyntheticGestureCompleted,
            base::Unretained(this)));

    // Expect that the compositor scrolled at least one pixel while the
    // main thread was in a busy loop.
    gfx::Vector2dF default_scroll_offset;
    while (observer.LastRenderFrameMetadata()
               .root_scroll_offset.value_or(default_scroll_offset)
               .y() <= 0) {
      observer.WaitForMetadataChange();
    }
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(CompositorEventAckBrowserTest);
};

IN_PROC_BROWSER_TEST_F(CompositorEventAckBrowserTest, MouseWheel) {
  LoadURL(kCompositorEventAckDataURL);
  DoWheelScroll();
}

// Disabled on MacOS because it doesn't support touch input.
#if defined(OS_MACOSX)
#define MAYBE_TouchStart DISABLED_TouchStart
#else
#define MAYBE_TouchStart TouchStart
#endif
IN_PROC_BROWSER_TEST_F(CompositorEventAckBrowserTest, MAYBE_TouchStart) {
  LoadURL(kCompositorEventAckDataURL);
  DoTouchScroll();
}

// Disabled on MacOS because it doesn't support touch input.
#if defined(OS_MACOSX)
#define MAYBE_TouchStartDuringFling DISABLED_TouchStartDuringFling
#else
#define MAYBE_TouchStartDuringFling TouchStartDuringFling
#endif
IN_PROC_BROWSER_TEST_F(CompositorEventAckBrowserTest,
                       MAYBE_TouchStartDuringFling) {
  LoadURL(kBlockingTouchStartDataURL);

  // Send the touch events via routing since they need to be registered by the
  // TouchEventAckQueue.
  auto* root_view = GetWidgetHost()->GetView();
  auto* input_event_router = GetWidgetHost()->delegate()->GetInputEventRouter();

  // Send a TouchStart so that we can set allowed touch action to Auto.
  SyntheticWebTouchEvent touch_event;
  touch_event.PressPoint(50, 50);
  touch_event.SetTimeStamp(ui::EventTimeForNow());
  input_event_router->RouteTouchEvent(root_view, &touch_event,
                                      ui::LatencyInfo());
  GetWidgetHost()->input_router()->OnSetTouchAction(cc::kTouchActionAuto);

  // Send GSB to start scrolling sequence.
  blink::WebGestureEvent gesture_scroll_begin(
      blink::WebGestureEvent::kGestureScrollBegin,
      blink::WebInputEvent::kNoModifiers, ui::EventTimeForNow());
  gesture_scroll_begin.SetSourceDevice(blink::WebGestureDevice::kTouchscreen);
  gesture_scroll_begin.data.scroll_begin.delta_hint_units =
      ui::input_types::ScrollGranularity::kScrollByPrecisePixel;
  gesture_scroll_begin.data.scroll_begin.delta_x_hint = 0.f;
  gesture_scroll_begin.data.scroll_begin.delta_y_hint = -5.f;
  GetWidgetHost()->ForwardGestureEvent(gesture_scroll_begin);

  //  Send a GFS and wait for the page to scroll making sure that fling progress
  //  has started.
  blink::WebGestureEvent gesture_fling_start(
      blink::WebGestureEvent::kGestureFlingStart,
      blink::WebInputEvent::kNoModifiers, ui::EventTimeForNow());
  gesture_fling_start.SetSourceDevice(blink::WebGestureDevice::kTouchscreen);
  gesture_fling_start.data.fling_start.velocity_x = 0.f;
  gesture_fling_start.data.fling_start.velocity_y = -2000.f;
  GetWidgetHost()->ForwardGestureEvent(gesture_fling_start);
  RenderFrameSubmissionObserver observer(
      GetWidgetHost()->render_frame_metadata_provider());
  gfx::Vector2dF default_scroll_offset;
  while (observer.LastRenderFrameMetadata()
             .root_scroll_offset.value_or(default_scroll_offset)
             .y() <= 0)
    observer.WaitForMetadataChange();

  touch_event.ReleasePoint(0);
  //  TODO(wjmaclean): Figure out why we can send two touch events with the same
  //  id, and not only does it work, it fails to work if we give the second
  //  event a unique id!
  //  touch_event.unique_touch_event_id = ui::GetNextTouchEventId();
  input_event_router->RouteTouchEvent(root_view, &touch_event,
                                      ui::LatencyInfo());
  touch_event.ResetPoints();

  // Send a touch start event and wait for its ack. The touch start must be
  // uncancelable since there is an on-going fling with touchscreen source. The
  // test will timeout if the touch start event is cancelable since there is a
  // busy loop in the blocking touch start event listener.
  InputEventAckWaiter touch_start_ack_observer(GetWidgetHost(),
                                               WebInputEvent::kTouchStart);
  touch_event.PressPoint(50, 50);
  touch_event.SetTimeStamp(ui::EventTimeForNow());
  input_event_router->RouteTouchEvent(root_view, &touch_event,
                                      ui::LatencyInfo());
  touch_start_ack_observer.Wait();
}

// Disabled on MacOS because it doesn't support touch input.
#if defined(OS_MACOSX)
#define MAYBE_PassiveTouchStartBlockingTouchEnd \
  DISABLED_PassiveTouchStartBlockingTouchEnd
#else
#define MAYBE_PassiveTouchStartBlockingTouchEnd \
  PassiveTouchStartBlockingTouchEnd
#endif
IN_PROC_BROWSER_TEST_F(CompositorEventAckBrowserTest,
                       MAYBE_PassiveTouchStartBlockingTouchEnd) {
  LoadURL(kPassiveTouchStartBlockingTouchEndDataURL);
  DoTouchScroll();
}

}  // namespace content
