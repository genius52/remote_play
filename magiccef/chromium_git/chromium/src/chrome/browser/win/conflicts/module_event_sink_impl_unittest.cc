// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/win/conflicts/module_event_sink_impl.h"

#include <windows.h>

#include <memory>

#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// The address of this module in memory. The linker will take care of defining
// this symbol.
extern "C" IMAGE_DOS_HEADER __ImageBase;

// An invalid load address.
const uint64_t kInvalidLoadAddress = 0xDEADBEEF;

}  // namespace

class ModuleEventSinkImplTest : public testing::Test {
 protected:
  ModuleEventSinkImplTest() = default;
  ~ModuleEventSinkImplTest() override = default;

  bool CreateModuleSinkImpl() {
    HANDLE process_handle = 0;
    if (!::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentProcess(),
                           ::GetCurrentProcess(), &process_handle, 0, FALSE,
                           DUPLICATE_SAME_ACCESS)) {
      return false;
    }

    module_event_sink_impl_ = std::make_unique<ModuleEventSinkImpl>(
        base::Process(process_handle), content::PROCESS_TYPE_BROWSER,
        base::BindRepeating(&ModuleEventSinkImplTest::OnModuleEvent,
                            base::Unretained(this)));
    return true;
  }

  void OnModuleEvent(content::ProcessType process_type,
                     const base::FilePath& module_path,
                     uint32_t module_size,
                     uint32_t module_time_date_stamp) {
    ++module_event_count_;
  }

  int module_event_count() { return module_event_count_; }

  // Must be first.
  content::TestBrowserThreadBundle test_browser_thread_bundle_;

  std::unique_ptr<ModuleEventSinkImpl> module_event_sink_impl_;

  int module_event_count_ = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ModuleEventSinkImplTest);
};

TEST_F(ModuleEventSinkImplTest, CallsForwardedAsExpected) {
  const uintptr_t kValidLoadAddress = reinterpret_cast<uintptr_t>(&__ImageBase);

  EXPECT_EQ(0, module_event_count());

  ASSERT_TRUE(CreateModuleSinkImpl());
  EXPECT_EQ(0, module_event_count());

  // An invalid load event should not cause a module entry.
  module_event_sink_impl_->OnModuleEvents({kInvalidLoadAddress});
  test_browser_thread_bundle_.RunUntilIdle();
  EXPECT_EQ(0, module_event_count());

  // A valid load event should cause a module entry.
  module_event_sink_impl_->OnModuleEvents({kValidLoadAddress});
  test_browser_thread_bundle_.RunUntilIdle();
  EXPECT_EQ(1, module_event_count());
}

TEST_F(ModuleEventSinkImplTest, MultipleEvents) {
  const uintptr_t kLoadAddress1 = reinterpret_cast<uintptr_t>(&__ImageBase);
  const uintptr_t kLoadAddress2 =
      reinterpret_cast<uintptr_t>(::GetModuleHandle(L"kernel32.dll"));
  ASSERT_TRUE(CreateModuleSinkImpl());

  EXPECT_EQ(0, module_event_count());

  module_event_sink_impl_->OnModuleEvents({kLoadAddress1, kLoadAddress2});
  test_browser_thread_bundle_.RunUntilIdle();
  EXPECT_EQ(2, module_event_count());
}
