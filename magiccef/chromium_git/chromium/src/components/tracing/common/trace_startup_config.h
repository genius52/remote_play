// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_TRACING_COMMON_TRACE_STARTUP_CONFIG_H_
#define COMPONENTS_TRACING_COMMON_TRACE_STARTUP_CONFIG_H_

#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/trace_event/trace_config.h"
#include "build/build_config.h"
#include "components/tracing/tracing_export.h"

namespace base {
template <typename Type>
struct DefaultSingletonTraits;
}  // namespace base

namespace content {
class CommandlineStartupTracingTest;
class BackgroundStartupTracingTest;
}

namespace tracing {

// TraceStartupConfig is a singleton that contains the configurations of startup
// tracing. One can use --trace-startup flag or, for more complicated
// configurations, create a trace config file and use it to configure startup
// tracing.
//
// The trace config file should be JSON formated. One example is:
//   {
//     "trace_config": {
//        "record_mode": "record-until-full",
//        "included_categories": ["cc", "skia"]
//      },
//      "startup_duration": 5,
//      "result_file": "chrometrace.log"
//   }
//
// trace_config:     The configuration of tracing. Please see the details in
//                   base/trace_event/trace_config.h.
//
// startup_duration: The duration for startup tracing in terms of seconds.
//                   Tracing will stop automatically after the duration. If this
//                   value is not specified, the duration is 0 and one needs
//                   to stop tracing by other ways, e.g., by DevTools, or get
//                   the result file after shutting the browser down.
//
// result_file:      The file that contains the trace log. The default result
//                   file path is chrometrace.log, except on Android, where the
//                   default path is generated by tracing controller. Chrome
//                   will dump the trace log to this file
//                   1) after startup_duration if it is specified;
//                   2) or after browser shutdown if startup duration is 0.
//                   One can also stop tracing and get the result by other ways,
//                   e.g., by DevTools. In that case, the trace log will not be
//                   saved to this file.
//
// result_directory: A directory to save traces to. The saved traces will be
//                   named chrometrace.log, prefixed with the Unix timestamp of
//                   Chrome's startup time. This is meant for cases where Chrome
//                   is started multiple times from one command line, such as
//                   during tests. This option is ignored if result_file is also
//                   specified.
//
// The trace config file can be specified by the --trace-config-file flag on
// most platforms except on Android, e.g., --trace-config-file=path/to/file/.
// This flag should not be used with --trace-startup; otherwise,
// --trace-config-file flag will be ignored. If the --trace-config-file flag is
// used without the file path, Chrome will do startup tracing with 5 seconds'
// duration.
//
// On Android, Chrome does not read the --trace-config-file flag, because not
// all Chrome based browsers read customized flag, e.g., Android WebView. Chrome
// on Android reads from a fixed file location:
//   /data/local/chrome-trace-config.json
// If this file exists, Chrome will start tracing according to the configuration
// specified in the file, otherwise, Chrome will not start tracing.
//
// In order for Chrome to read this file on Android M+, SE Linux mode must be
// set to 0. Run "adb shell setenforce 0". Note that on Android, the output path
// is ignored, and the result is emitted to a location generated by
// TracingControllerAndroid::GenerateTracingFilePath.
class TRACING_EXPORT TraceStartupConfig {
 public:
  enum class SessionOwner { kTracingController, kDevToolsTracingHandler };

  static TraceStartupConfig* GetInstance();

  // Default minimum startup trace config with enough events to debug issues.
  static base::trace_event::TraceConfig GetDefaultBrowserStartupConfig();

  // IsEnabled() returns true if
  // - valid trace config file or trace startup flags are specified,
  // - the specified startup duration is zero or we are not passed the positive
  //   startup duration, and
  // - startup tracing is not stopped by other means, e.g. via DevTools
  //   protocol.
  bool IsEnabled() const;

  // SetDisabled() is used by the tracing controller to indicate that startup
  // tracing is finished.
  void SetDisabled();

  // Returns true while startup tracing is not finished, if startup duration is
  // positive.
  bool IsTracingStartupForDuration() const;

  base::trace_event::TraceConfig GetTraceConfig() const;
  int GetStartupDuration() const;

  // Returns true while startup tracing is not finished, if trace should be
  // saved to result file.
  bool ShouldTraceToResultFile() const;
  base::FilePath GetResultFile() const;
  void OnTraceToResultFileFinished();

  // Get the background tracing config set in application preferences on the
  // previous session, for current session.
  bool GetBackgroundStartupTracingEnabled() const;

  // Set the background tracing config in preferences for the next session.
  void SetBackgroundStartupTracingEnabled(bool enabled);

  // Returns when the startup tracing is finished and written to file, false on
  // all other cases.
  bool finished_writing_to_file_for_testing() const {
    return finished_writing_to_file_;
  }

  SessionOwner GetSessionOwner() const;

  // Called by a potential session owner to determine if it should take
  // ownership of the startup tracing session and begin tracing. Returns |true|
  // if the passed |owner| should adopt the session.
  bool AttemptAdoptBySessionOwner(SessionOwner owner);

 private:
  // This allows constructor and destructor to be private and usable only
  // by the Singleton class.
  friend struct base::DefaultSingletonTraits<TraceStartupConfig>;
  friend class content::CommandlineStartupTracingTest;
  friend class content::BackgroundStartupTracingTest;

  TraceStartupConfig();
  ~TraceStartupConfig();

  bool IsUsingPerfettoOutput() const;

  bool EnableFromCommandLine();
  bool EnableFromConfigFile();
  bool EnableFromBackgroundTracing();

  bool ParseTraceConfigFileContent(const std::string& content);

  bool is_enabled_ = false;
  bool is_enabled_from_background_tracing_ = false;
  base::trace_event::TraceConfig trace_config_;
  int startup_duration_ = 0;
  bool should_trace_to_result_file_ = false;
  base::FilePath result_file_;
  bool finished_writing_to_file_ = false;
  SessionOwner session_owner_ = SessionOwner::kTracingController;
  bool session_adopted_ = false;

  DISALLOW_COPY_AND_ASSIGN(TraceStartupConfig);
};

}  // namespace tracing

#endif  // COMPONENTS_TRACING_COMMON_TRACE_STARTUP_CONFIG_H_
