// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_ARC_FILEAPI_ARC_DOCUMENTS_PROVIDER_FILE_STREAM_WRITER_H_
#define CHROME_BROWSER_CHROMEOS_ARC_FILEAPI_ARC_DOCUMENTS_PROVIDER_FILE_STREAM_WRITER_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "net/base/completion_once_callback.h"
#include "storage/browser/fileapi/file_stream_writer.h"
#include "storage/browser/fileapi/file_system_url.h"

class GURL;

namespace arc {

// FileStreamWriter implementation for ARC documents provider file system.
// It actually delegates operations to ArcContentFileSystemFileStreamWriter.
// TODO(crbug.com/678886): Write unit tests.
class ArcDocumentsProviderFileStreamWriter : public storage::FileStreamWriter {
 public:
  ArcDocumentsProviderFileStreamWriter(const storage::FileSystemURL& url,
                                       int64_t offset);
  ~ArcDocumentsProviderFileStreamWriter() override;

  // storage::FileStreamWriter overrides:
  int Write(net::IOBuffer* buffer,
            int bufffer_length,
            net::CompletionOnceCallback callback) override;
  int Cancel(net::CompletionOnceCallback callback) override;
  int Flush(net::CompletionOnceCallback callback) override;

 private:
  void OnResolveToContentUrl(const GURL& content_url);
  void RunPendingWrite(scoped_refptr<net::IOBuffer> buffer,
                       int buffer_length,
                       net::CompletionOnceCallback callback);
  void RunPendingCancel(net::CompletionOnceCallback callback);
  void RunPendingFlush(net::CompletionOnceCallback callback);
  const int64_t offset_;
  bool content_url_resolved_;
  const storage::FileSystemURL arc_url_;
  std::unique_ptr<storage::FileStreamWriter> underlying_writer_;
  std::vector<base::OnceClosure> pending_operations_;

  base::WeakPtrFactory<ArcDocumentsProviderFileStreamWriter> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ArcDocumentsProviderFileStreamWriter);
};

}  // namespace arc

#endif  // CHROME_BROWSER_CHROMEOS_ARC_FILEAPI_ARC_DOCUMENTS_PROVIDER_FILE_STREAM_WRITER_H_
