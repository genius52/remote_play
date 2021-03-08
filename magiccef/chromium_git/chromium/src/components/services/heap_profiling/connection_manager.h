// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SERVICES_HEAP_PROFILING_CONNECTION_MANAGER_H_
#define COMPONENTS_SERVICES_HEAP_PROFILING_CONNECTION_MANAGER_H_

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "components/services/heap_profiling/allocation.h"
#include "components/services/heap_profiling/public/mojom/heap_profiling_service.mojom.h"
#include "services/resource_coordinator/public/mojom/memory_instrumentation/memory_instrumentation.mojom.h"

namespace heap_profiling {

struct ExportParams;

using VmRegions =
    base::flat_map<base::ProcessId,
                   std::vector<memory_instrumentation::mojom::VmRegionPtr>>;

// Manages all connections and logging for each process. Pipes are supplied by
// the pipe server and this class will connect them to a parser and logger.
//
// Note |backtrace_storage| must outlive ConnectionManager.
//
// This object is constructed on the UI thread, but the rest of the usage
// (including deletion) is on the IO thread.
class ConnectionManager {
  using AddressToStringMap = std::unordered_map<uint64_t, std::string>;
  using CompleteCallback = base::OnceClosure;
  using ContextMap = std::map<std::string, int>;
  using DumpProcessesForTracingCallback = memory_instrumentation::mojom::
      HeapProfiler::DumpProcessesForTracingCallback;

 public:
  ConnectionManager();
  ~ConnectionManager();

  // Dumping is asynchronous so will not be complete when this function
  // returns. The dump is complete when the callback provided in the args is
  // fired.
  void DumpProcessesForTracing(bool strip_path_from_mapped_files,
                               DumpProcessesForTracingCallback callback,
                               VmRegions vm_regions);

  void OnNewConnection(base::ProcessId pid,
                       mojom::ProfilingClientPtr client,
                       mojom::ProcessType process_type,
                       mojom::ProfilingParamsPtr params);

  std::vector<base::ProcessId> GetConnectionPids();
  std::vector<base::ProcessId> GetConnectionPidsThatNeedVmRegions();

 private:
  struct Connection;
  struct DumpProcessesForTracingTracking;

  void HeapProfileRetrieved(
      scoped_refptr<DumpProcessesForTracingTracking> tracking,
      base::ProcessId pid,
      mojom::ProcessType process_type,
      bool strip_path_from_mapped_files,
      uint32_t sampling_rate,
      mojom::HeapProfilePtr profile);

  bool ConvertProfileToExportParams(mojom::HeapProfilePtr profile,
                                    uint32_t sampling_rate,
                                    ExportParams* out_params);

  // Notification that a connection is complete. Unlike OnNewConnection which
  // is signaled by the pipe server, this is signaled by the allocation tracker
  // to ensure that the pipeline for this process has been flushed of all
  // messages.
  void OnConnectionComplete(base::ProcessId pid);

  // Reports the ProcessTypes of the processes being profiled.
  void ReportMetrics();

  // The next ID to use when exporting a heap dump.
  size_t next_id_ = 1;

  // Maps process ID to the connection information for it.
  base::flat_map<base::ProcessId, std::unique_ptr<Connection>> connections_;
  base::Lock connections_lock_;

  // Every 24-hours, reports the types of profiled processes.
  base::RepeatingTimer metrics_timer_;

  // Must be the last.
  base::WeakPtrFactory<ConnectionManager> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(ConnectionManager);
};

}  // namespace heap_profiling

#endif  // COMPONENTS_SERVICES_HEAP_PROFILING_CONNECTION_MANAGER_H_
