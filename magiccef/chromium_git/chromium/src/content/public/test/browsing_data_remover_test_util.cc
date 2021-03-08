// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/test/browsing_data_remover_test_util.h"

#include "base/bind.h"
#include "base/task/thread_pool/thread_pool.h"
#include "base/threading/thread_task_runner_handle.h"

namespace content {

BrowsingDataRemoverCompletionObserver::BrowsingDataRemoverCompletionObserver(
    BrowsingDataRemover* remover)
    : observer_(this),
      origin_task_runner_(base::ThreadTaskRunnerHandle::Get()) {
  observer_.Add(remover);
}

BrowsingDataRemoverCompletionObserver::
    ~BrowsingDataRemoverCompletionObserver() {}

void BrowsingDataRemoverCompletionObserver::BlockUntilCompletion() {
  base::ThreadPoolInstance::Get()->FlushAsyncForTesting(base::BindOnce(
      &BrowsingDataRemoverCompletionObserver::FlushForTestingComplete,
      base::Unretained(this)));
  run_loop_.Run();
}

void BrowsingDataRemoverCompletionObserver::OnBrowsingDataRemoverDone() {
  browsing_data_remover_done_ = true;
  observer_.RemoveAll();
  QuitRunLoopWhenTasksComplete();
}

void BrowsingDataRemoverCompletionObserver::FlushForTestingComplete() {
  if (origin_task_runner_->RunsTasksInCurrentSequence()) {
    flush_for_testing_complete_ = true;
    QuitRunLoopWhenTasksComplete();
    return;
  }
  origin_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &BrowsingDataRemoverCompletionObserver::FlushForTestingComplete,
          base::Unretained(this)));
}

void BrowsingDataRemoverCompletionObserver::QuitRunLoopWhenTasksComplete() {
  if (!flush_for_testing_complete_ || !browsing_data_remover_done_)
    return;

  run_loop_.QuitWhenIdle();
}

BrowsingDataRemoverCompletionInhibitor::BrowsingDataRemoverCompletionInhibitor(
    BrowsingDataRemover* remover)
    : remover_(remover),
      run_loop_(new base::RunLoop),
      origin_task_runner_(base::ThreadTaskRunnerHandle::Get()) {
  DCHECK(remover);
  remover_->SetWouldCompleteCallbackForTesting(
      base::Bind(&BrowsingDataRemoverCompletionInhibitor::
                     OnBrowsingDataRemoverWouldComplete,
                 base::Unretained(this)));
}

BrowsingDataRemoverCompletionInhibitor::
    ~BrowsingDataRemoverCompletionInhibitor() {
  Reset();
}

void BrowsingDataRemoverCompletionInhibitor::Reset() {
  if (!remover_)
    return;
  remover_->SetWouldCompleteCallbackForTesting(
      base::Callback<void(const base::Closure&)>());
  remover_ = nullptr;
}

void BrowsingDataRemoverCompletionInhibitor::BlockUntilNearCompletion() {
  base::ThreadPoolInstance::Get()->FlushAsyncForTesting(base::BindOnce(
      &BrowsingDataRemoverCompletionInhibitor::FlushForTestingComplete,
      base::Unretained(this)));
  run_loop_->Run();
  run_loop_ = std::make_unique<base::RunLoop>();
  flush_for_testing_complete_ = false;
  browsing_data_remover_would_complete_done_ = false;
}

void BrowsingDataRemoverCompletionInhibitor::ContinueToCompletion() {
  DCHECK(!continue_to_completion_callback_.is_null());
  continue_to_completion_callback_.Run();
  continue_to_completion_callback_.Reset();
}

void BrowsingDataRemoverCompletionInhibitor::OnBrowsingDataRemoverWouldComplete(
    const base::Closure& continue_to_completion) {
  DCHECK(continue_to_completion_callback_.is_null());
  continue_to_completion_callback_ = continue_to_completion;
  browsing_data_remover_would_complete_done_ = true;
  QuitRunLoopWhenTasksComplete();
}

void BrowsingDataRemoverCompletionInhibitor::FlushForTestingComplete() {
  if (origin_task_runner_->RunsTasksInCurrentSequence()) {
    flush_for_testing_complete_ = true;
    QuitRunLoopWhenTasksComplete();
    return;
  }
  origin_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          &BrowsingDataRemoverCompletionInhibitor::FlushForTestingComplete,
          base::Unretained(this)));
}

void BrowsingDataRemoverCompletionInhibitor::QuitRunLoopWhenTasksComplete() {
  if (!flush_for_testing_complete_ ||
      !browsing_data_remover_would_complete_done_) {
    return;
  }

  run_loop_->QuitWhenIdle();
}

}  // namespace content
