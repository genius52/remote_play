// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Utilities for converting goldens to and from the structures used by the time
// limit processor.

#ifndef CHROME_BROWSER_CHROMEOS_CHILD_ACCOUNTS_TIME_LIMIT_CONSISTENCY_TEST_CONSISTENCY_GOLDEN_CONVERTER_H_
#define CHROME_BROWSER_CHROMEOS_CHILD_ACCOUNTS_TIME_LIMIT_CONSISTENCY_TEST_CONSISTENCY_GOLDEN_CONVERTER_H_

#include <memory>

#include "base/optional.h"
#include "chrome/browser/chromeos/child_accounts/time_limit_consistency_test/goldens/consistency_golden.pb.h"
#include "chrome/browser/chromeos/child_accounts/usage_time_limit_processor.h"

namespace base {
class DictionaryValue;
}  // namespace base

namespace chromeos {
namespace time_limit_consistency {

// Converts the input part of a consistency golden case to the structure used by
// the time limit processor.
std::unique_ptr<base::DictionaryValue> ConvertGoldenInputToProcessorInput(
    const ConsistencyGoldenInput& input);

// Converts the output struct generated by the time limit processor to the
// consistency golden output proto.
ConsistencyGoldenOutput ConvertProcessorOutputToGoldenOutput(
    const usage_time_limit::State& state);

// Generates a State struct representing the previous state required by the
// processor if there is a UNLOCK_USAGE_LIMIT override present. The generated
// state simulates being locked by usage limit since one minute before the
// override was created.
// If the override is of another type, base::nullopt will be returned.
base::Optional<usage_time_limit::State>
GenerateUnlockUsageLimitOverrideStateFromInput(
    const ConsistencyGoldenInput& input);

}  // namespace time_limit_consistency
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_CHILD_ACCOUNTS_TIME_LIMIT_CONSISTENCY_TEST_CONSISTENCY_GOLDEN_CONVERTER_H_
