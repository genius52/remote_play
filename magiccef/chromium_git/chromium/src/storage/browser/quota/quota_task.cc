// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/browser/quota/quota_task.h"

#include <algorithm>
#include <functional>

#include "base/bind.h"
#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/stl_util.h"
#include "base/threading/thread_task_runner_handle.h"

using base::TaskRunner;

namespace storage {

// QuotaTask ---------------------------------------------------------------

QuotaTask::~QuotaTask() = default;

void QuotaTask::Start() {
  DCHECK(observer_);
  observer()->RegisterTask(this);
  Run();
}

QuotaTask::QuotaTask(QuotaTaskObserver* observer)
    : observer_(observer),
      original_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      delete_scheduled_(false) {
}

void QuotaTask::CallCompleted() {
  DCHECK(original_task_runner_->BelongsToCurrentThread());
  if (observer_) {
    observer_->UnregisterTask(this);
    Completed();
  }
}

void QuotaTask::Abort() {
  DCHECK(original_task_runner_->BelongsToCurrentThread());
  observer_ = nullptr;
  Aborted();
}

void QuotaTask::DeleteSoon() {
  DCHECK(original_task_runner_->BelongsToCurrentThread());
  if (delete_scheduled_)
    return;
  delete_scheduled_ = true;
  base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE, this);
}

// QuotaTaskObserver -------------------------------------------------------

QuotaTaskObserver::~QuotaTaskObserver() {
  for (auto* task : running_quota_tasks_)
    task->Abort();
}

QuotaTaskObserver::QuotaTaskObserver() = default;

void QuotaTaskObserver::RegisterTask(QuotaTask* task) {
  running_quota_tasks_.insert(task);
}

void QuotaTaskObserver::UnregisterTask(QuotaTask* task) {
  DCHECK(base::ContainsKey(running_quota_tasks_, task));
  running_quota_tasks_.erase(task);
}

}  // namespace storage
