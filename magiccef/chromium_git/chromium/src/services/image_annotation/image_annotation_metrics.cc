// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/image_annotation/image_annotation_metrics.h"

#include <map>
#include <string>

#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/stringprintf.h"

namespace image_annotation {

using namespace metrics_internal;

void ReportCacheHit(const bool cache_hit) {
  UMA_HISTOGRAM_BOOLEAN(kCacheHit, cache_hit);
}

void ReportJsonParseSuccess(const bool success) {
  UMA_HISTOGRAM_BOOLEAN(kJsonParseSuccess, success);
}

void ReportPixelFetchSuccess(const bool success) {
  UMA_HISTOGRAM_BOOLEAN(kPixelFetchSuccess, success);
}

void ReportOcrAnnotation(const double confidence, const bool empty) {
  const int confidence_percent = static_cast<int>(std::round(confidence * 100));
  UMA_HISTOGRAM_PERCENTAGE(base::StringPrintf(kAnnotationConfidence, "Ocr"),
                           confidence_percent);
  UMA_HISTOGRAM_BOOLEAN(base::StringPrintf(kAnnotationEmpty, "Ocr"), empty);
}

void ReportDescAnnotation(const mojom::AnnotationType type,
                          const double confidence,
                          const bool empty) {
  static const base::NoDestructor<std::map<mojom::AnnotationType, std::string>>
      kTypeNames({{mojom::AnnotationType::kOcr, "Ocr"},
                  {mojom::AnnotationType::kLabel, "Label"},
                  {mojom::AnnotationType::kCaption, "Caption"}});

  const auto lookup = kTypeNames->find(type);
  const std::string type_name = base::StrCat(
      {"Desc", lookup == kTypeNames->end() ? "Unknown" : lookup->second});

  const int confidence_percent = static_cast<int>(std::round(confidence * 100));

  // We use function variants here since our histogram name is not a "runtime
  // constant".
  base::UmaHistogramPercentage(
      base::StringPrintf(kAnnotationConfidence, type_name.c_str()),
      confidence_percent);
  base::UmaHistogramBoolean(
      base::StringPrintf(kAnnotationEmpty, type_name.c_str()), empty);

  UMA_HISTOGRAM_ENUMERATION(kDescType, type);
}

void ReportDescFailure(const DescFailureReason reason) {
  UMA_HISTOGRAM_ENUMERATION(kDescFailure, reason);
}

void ReportServerNetError(const int code) {
  base::UmaHistogramSparse(kServerNetError, code);
}

void ReportServerResponseCode(const int code) {
  base::UmaHistogramSparse(kServerHttpResponseCode, code);
}

void ReportServerLatency(const base::TimeDelta latency) {
  // Use a custom time histogram with ~10 buckets per order of magnitude between
  // 1ms and 30sec.
  UMA_HISTOGRAM_CUSTOM_TIMES(kServerLatency, latency,
                             base::TimeDelta::FromMilliseconds(1),
                             base::TimeDelta::FromSeconds(30), 50);
}

void ReportImageRequestIncludesDesc(const bool includes_desc) {
  UMA_HISTOGRAM_BOOLEAN(kImageRequestIncludesDesc, includes_desc);
}

void ReportServerRequestSizeKB(const size_t size_kb) {
  // Use a custom memory histogram with ~10 buckets per order of magnitude
  // between 1KB and 30MB.
  UMA_HISTOGRAM_CUSTOM_COUNTS(kServerRequestSize, size_kb, 1, 30000, 50);
}

void ReportServerResponseSizeBytes(const size_t size_bytes) {
  // Use a custom memory histogram with ~10 buckets per order of magnitude
  // between 1byte and 1MB (at which point we stop downloading).
  UMA_HISTOGRAM_CUSTOM_COUNTS(kServerResponseSize, size_bytes, 1, 1000000, 70);
}

void ReportOcrStatus(const int status) {
  base::UmaHistogramSparse(base::StringPrintf(kAnnotationStatus, "Ocr"),
                           status);
}

void ReportDescStatus(const int status) {
  base::UmaHistogramSparse(base::StringPrintf(kAnnotationStatus, "Desc"),
                           status);
}

void ReportEngineKnown(const bool known) {
  UMA_HISTOGRAM_BOOLEAN(kEngineKnown, known);
}

void ReportSourcePixelCount(const size_t pixel_count) {
  // Use a custom memory histogram with ~10 buckets per order of magnitude
  // 1x1 and 8K image resolution.
  UMA_HISTOGRAM_CUSTOM_COUNTS(kSourcePixelCount, pixel_count, 1, 7680 * 4320,
                              80);
}

void ReportEncodedJpegSize(const size_t size_bytes) {
  // Use a custom memory histogram with ~10 buckets per order of magnitude
  // between 1byte and 1MB.
  UMA_HISTOGRAM_CUSTOM_COUNTS(kEncodedJpegSize, size_bytes, 1, 1000000, 70);
}

void ReportClientResult(const ClientResult result) {
  UMA_HISTOGRAM_ENUMERATION(kClientResult, result);
}

}  // namespace image_annotation
