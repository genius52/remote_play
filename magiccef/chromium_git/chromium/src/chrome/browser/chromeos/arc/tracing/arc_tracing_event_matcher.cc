// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/tracing/arc_tracing_event_matcher.h"

#include "base/strings/string_split.h"
#include "chrome/browser/chromeos/arc/tracing/arc_tracing_event.h"

namespace arc {

ArcTracingEventMatcher::ArcTracingEventMatcher() = default;

ArcTracingEventMatcher::ArcTracingEventMatcher(const std::string& data) {
  std::string::size_type position = data.find(':');
  DCHECK(position);
  category_ = data.substr(0, position);
  name_ = data.substr(position + 1);
  DCHECK(!category_.empty());
  position = name_.find('(');
  if (position == std::string::npos)
    return;

  DCHECK_EQ(')', name_.back());
  for (const std::string& arg : base::SplitString(
           name_.substr(position + 1, name_.length() - position - 2), ";",
           base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    std::string::size_type separator = arg.find('=');
    args_[arg.substr(0, separator)] = arg.substr(separator + 1);
  }
  name_ = name_.substr(0, position);
}

ArcTracingEventMatcher& ArcTracingEventMatcher::SetPhase(char phase) {
  phase_ = phase;
  return *this;
}

ArcTracingEventMatcher& ArcTracingEventMatcher::SetCategory(
    const std::string& category) {
  category_ = category;
  return *this;
}

ArcTracingEventMatcher& ArcTracingEventMatcher::SetName(
    const std::string& name) {
  name_ = name;
  return *this;
}

ArcTracingEventMatcher& ArcTracingEventMatcher::AddArgument(
    const std::string& key,
    const std::string& value) {
  args_[key] = value;
  return *this;
}

bool ArcTracingEventMatcher::Match(const ArcTracingEvent& event) const {
  if (phase_ && phase_ != event.GetPhase())
    return false;
  if (!category_.empty() && event.GetCategory() != category_)
    return false;
  if (!name_.empty() && event.GetName() != name_)
    return false;
  for (const auto& arg : args_) {
    if (event.GetArgAsString(arg.first, std::string() /* default_value */) !=
        arg.second) {
      return false;
    }
  }
  return true;
}

}  // namespace arc
