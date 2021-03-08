// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/page_load_metrics/observers/signed_exchange_page_load_metrics_observer.h"

#include "chrome/browser/page_load_metrics/page_load_metrics_util.h"
#include "third_party/blink/public/platform/web_loading_behavior_flag.h"

namespace internal {

#define HISTOGRAM_SXG_PREFIX "PageLoad.Clients.SignedExchange."
#define HISTOGRAM_CACHED_SXG_PREFIX "PageLoad.Clients.SignedExchange.Cached."

constexpr char kHistogramSignedExchangePrefix[] = HISTOGRAM_SXG_PREFIX;
constexpr char kHistogramCachedSignedExchangePrefix[] =
    HISTOGRAM_CACHED_SXG_PREFIX;

#define SXG_LOAD_METRIC_VARIABLE(name, suffix)            \
  constexpr char kHistogramSignedExchange##name[] =       \
      HISTOGRAM_SXG_PREFIX suffix;                        \
  constexpr char kHistogramCachedSignedExchange##name[] = \
      HISTOGRAM_CACHED_SXG_PREFIX suffix;

SXG_LOAD_METRIC_VARIABLE(ParseStart, "ParseTiming.NavigationToParseStart")
SXG_LOAD_METRIC_VARIABLE(FirstInputDelay, "InteractiveTiming.FirstInputDelay3")
SXG_LOAD_METRIC_VARIABLE(FirstPaint, "PaintTiming.NavigationToFirstPaint")
SXG_LOAD_METRIC_VARIABLE(FirstContentfulPaint,
                         "PaintTiming.NavigationToFirstContentfulPaint")
SXG_LOAD_METRIC_VARIABLE(ParseStartToFirstContentfulPaint,
                         "PaintTiming.ParseStartToFirstContentfulPaint")
SXG_LOAD_METRIC_VARIABLE(
    FirstMeaningfulPaint,
    "Experimental.PaintTiming.NavigationToFirstMeaningfulPaint")
SXG_LOAD_METRIC_VARIABLE(
    ParseStartToFirstMeaningfulPaint,
    "Experimental.PaintTiming.ParseStartToFirstMeaningfulPaint")
SXG_LOAD_METRIC_VARIABLE(
    DomContentLoaded,
    "DocumentTiming.NavigationToDOMContentLoadedEventFired")
SXG_LOAD_METRIC_VARIABLE(Load, "DocumentTiming.NavigationToLoadEventFired")

#define SXG_PAGE_LOAD_HISTOGRAM(name, sample)                             \
  {                                                                       \
    const base::TimeDelta value = sample;                                 \
    PAGE_LOAD_HISTOGRAM(internal::kHistogramSignedExchange##name, value); \
    if (was_cached_) {                                                    \
      PAGE_LOAD_HISTOGRAM(internal::kHistogramCachedSignedExchange##name, \
                          value);                                         \
    }                                                                     \
  }

#undef SXG_LOAD_METRIC_VARIABLE
#undef HISTOGRAM_CACHED_SXG_PREFIX
#undef HISTOGRAM_SXG_PREFIX

}  // namespace internal

SignedExchangePageLoadMetricsObserver::SignedExchangePageLoadMetricsObserver() {
}

page_load_metrics::PageLoadMetricsObserver::ObservePolicy
SignedExchangePageLoadMetricsObserver::OnCommit(
    content::NavigationHandle* navigation_handle,
    ukm::SourceId source_id) {
  if (navigation_handle->IsSignedExchangeInnerResponse()) {
    was_cached_ = navigation_handle->WasResponseCached();
    return CONTINUE_OBSERVING;
  }

  return STOP_OBSERVING;
}

void SignedExchangePageLoadMetricsObserver::OnFirstPaintInPage(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const page_load_metrics::PageLoadExtraInfo& info) {
  if (!WasStartedInForegroundOptionalEventInForeground(
          timing.paint_timing->first_paint, info)) {
    return;
  }

  SXG_PAGE_LOAD_HISTOGRAM(FirstPaint, timing.paint_timing->first_paint.value());
}

void SignedExchangePageLoadMetricsObserver::OnFirstContentfulPaintInPage(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const page_load_metrics::PageLoadExtraInfo& info) {
  if (!WasStartedInForegroundOptionalEventInForeground(
          timing.paint_timing->first_contentful_paint, info)) {
    return;
  }

  SXG_PAGE_LOAD_HISTOGRAM(FirstContentfulPaint,
                          timing.paint_timing->first_contentful_paint.value());
  SXG_PAGE_LOAD_HISTOGRAM(ParseStartToFirstContentfulPaint,
                          timing.paint_timing->first_contentful_paint.value() -
                              timing.parse_timing->parse_start.value());
}

void SignedExchangePageLoadMetricsObserver::
    OnFirstMeaningfulPaintInMainFrameDocument(
        const page_load_metrics::mojom::PageLoadTiming& timing,
        const page_load_metrics::PageLoadExtraInfo& info) {
  if (!WasStartedInForegroundOptionalEventInForeground(
          timing.paint_timing->first_meaningful_paint, info)) {
    return;
  }

  SXG_PAGE_LOAD_HISTOGRAM(FirstMeaningfulPaint,
                          timing.paint_timing->first_meaningful_paint.value());
  SXG_PAGE_LOAD_HISTOGRAM(ParseStartToFirstMeaningfulPaint,
                          timing.paint_timing->first_meaningful_paint.value() -
                              timing.parse_timing->parse_start.value());
}

void SignedExchangePageLoadMetricsObserver::OnDomContentLoadedEventStart(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const page_load_metrics::PageLoadExtraInfo& info) {
  if (!WasStartedInForegroundOptionalEventInForeground(
          timing.document_timing->dom_content_loaded_event_start, info)) {
    return;
  }

  SXG_PAGE_LOAD_HISTOGRAM(
      DomContentLoaded,
      timing.document_timing->dom_content_loaded_event_start.value());
}

void SignedExchangePageLoadMetricsObserver::OnLoadEventStart(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const page_load_metrics::PageLoadExtraInfo& info) {
  if (!WasStartedInForegroundOptionalEventInForeground(
          timing.document_timing->load_event_start, info)) {
    return;
  }

  SXG_PAGE_LOAD_HISTOGRAM(Load,
                          timing.document_timing->load_event_start.value());
}

void SignedExchangePageLoadMetricsObserver::OnFirstInputInPage(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const page_load_metrics::PageLoadExtraInfo& info) {
  if (!WasStartedInForegroundOptionalEventInForeground(
          timing.interactive_timing->first_input_timestamp, info)) {
    return;
  }

  // Copied from the CorePageLoadMetricsObserver implementation.
  UMA_HISTOGRAM_CUSTOM_TIMES(
      internal::kHistogramSignedExchangeFirstInputDelay,
      timing.interactive_timing->first_input_delay.value(),
      base::TimeDelta::FromMilliseconds(1), base::TimeDelta::FromSeconds(60),
      50);
  if (was_cached_) {
    UMA_HISTOGRAM_CUSTOM_TIMES(
        internal::kHistogramCachedSignedExchangeFirstInputDelay,
        timing.interactive_timing->first_input_delay.value(),
        base::TimeDelta::FromMilliseconds(1), base::TimeDelta::FromSeconds(60),
        50);
  }
}

void SignedExchangePageLoadMetricsObserver::OnParseStart(
    const page_load_metrics::mojom::PageLoadTiming& timing,
    const page_load_metrics::PageLoadExtraInfo& info) {
  if (!WasStartedInForegroundOptionalEventInForeground(
          timing.parse_timing->parse_start, info)) {
    return;
  }

  SXG_PAGE_LOAD_HISTOGRAM(ParseStart, timing.parse_timing->parse_start.value());
}
