// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/browser/fileapi/async_file_util_adapter.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/macros.h"
#include "base/sequenced_task_runner.h"
#include "base/task_runner_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/services/filesystem/public/interfaces/types.mojom.h"
#include "storage/browser/blob/shareable_file_reference.h"
#include "storage/browser/fileapi/file_system_context.h"
#include "storage/browser/fileapi/file_system_file_util.h"
#include "storage/browser/fileapi/file_system_operation_context.h"
#include "storage/browser/fileapi/file_system_url.h"
#include "storage/common/fileapi/file_system_util.h"

using base::Owned;
using base::Unretained;
using storage::ShareableFileReference;

namespace storage {

namespace {

class EnsureFileExistsHelper {
 public:
  EnsureFileExistsHelper() : error_(base::File::FILE_OK), created_(false) {}

  void RunWork(FileSystemFileUtil* file_util,
               FileSystemOperationContext* context,
               const FileSystemURL& url) {
    error_ = file_util->EnsureFileExists(context, url, &created_);
  }

  void Reply(AsyncFileUtil::EnsureFileExistsCallback callback) {
    std::move(callback).Run(error_, created_);
  }

 private:
  base::File::Error error_;
  bool created_;
  DISALLOW_COPY_AND_ASSIGN(EnsureFileExistsHelper);
};

class GetFileInfoHelper {
 public:
  GetFileInfoHelper()
      : error_(base::File::FILE_OK) {}

  void GetFileInfo(FileSystemFileUtil* file_util,
                   FileSystemOperationContext* context,
                   const FileSystemURL& url,
                   bool calculate_total_size) {
    error_ = file_util->GetFileInfo(context, url, &file_info_, &platform_path_);
    if (error_ == base::File::FILE_OK && calculate_total_size &&
        file_info_.is_directory) {
      file_info_.size = 0;
      auto enumerator = file_util->CreateFileEnumerator(context, url, true);
      base::FilePath path = enumerator->Next();
      while (!path.empty()) {
        if (!enumerator->IsDirectory()) {
          file_info_.size += enumerator->Size();
        }
        path = enumerator->Next();
      }
    }
  }

  void CreateSnapshotFile(FileSystemFileUtil* file_util,
                          FileSystemOperationContext* context,
                          const FileSystemURL& url) {
    scoped_file_ = file_util->CreateSnapshotFile(
        context, url, &error_, &file_info_, &platform_path_);
  }

  void ReplyFileInfo(AsyncFileUtil::GetFileInfoCallback callback) {
    std::move(callback).Run(error_, file_info_);
  }

  void ReplySnapshotFile(AsyncFileUtil::CreateSnapshotFileCallback callback) {
    std::move(callback).Run(
        error_, file_info_, platform_path_,
        ShareableFileReference::GetOrCreate(std::move(scoped_file_)));
  }

 private:
  base::File::Error error_;
  base::File::Info file_info_;
  base::FilePath platform_path_;
  storage::ScopedFile scoped_file_;
  DISALLOW_COPY_AND_ASSIGN(GetFileInfoHelper);
};

void ReadDirectoryHelper(FileSystemFileUtil* file_util,
                         FileSystemOperationContext* context,
                         const FileSystemURL& url,
                         base::SingleThreadTaskRunner* origin_runner,
                         AsyncFileUtil::ReadDirectoryCallback callback) {
  base::File::Info file_info;
  base::FilePath platform_path;
  base::File::Error error =
      file_util->GetFileInfo(context, url, &file_info, &platform_path);

  if (error == base::File::FILE_OK && !file_info.is_directory)
    error = base::File::FILE_ERROR_NOT_A_DIRECTORY;

  std::vector<filesystem::mojom::DirectoryEntry> entries;
  if (error != base::File::FILE_OK) {
    origin_runner->PostTask(FROM_HERE, base::BindOnce(callback, error, entries,
                                                      false /* has_more */));
    return;
  }

  // Note: Increasing this value may make some tests in LayoutTests meaningless.
  // (Namely, read-directory-many.html and read-directory-sync-many.html are
  // assuming that they are reading much more entries than this constant.)
  const size_t kResultChunkSize = 100;

  std::unique_ptr<FileSystemFileUtil::AbstractFileEnumerator> file_enum(
      file_util->CreateFileEnumerator(context, url, false));

  base::FilePath current;
  while (!(current = file_enum->Next()).empty()) {
    entries.emplace_back(VirtualPath::BaseName(current),
                         file_enum->IsDirectory()
                             ? filesystem::mojom::FsFileType::DIRECTORY
                             : filesystem::mojom::FsFileType::REGULAR_FILE);

    if (entries.size() == kResultChunkSize) {
      origin_runner->PostTask(
          FROM_HERE, base::BindOnce(callback, base::File::FILE_OK, entries,
                                    true /* has_more */));
      entries.clear();
    }
  }
  origin_runner->PostTask(FROM_HERE,
                          base::BindOnce(callback, base::File::FILE_OK, entries,
                                         false /* has_more */));
}

void RunCreateOrOpenCallback(FileSystemOperationContext* context,
                             AsyncFileUtil::CreateOrOpenCallback callback,
                             base::File file) {
  if (callback.IsCancelled()) {
    // If |callback| been cancelled, free |file| on the correct task runner.
    context->task_runner()->PostTask(
        FROM_HERE,
        BindOnce([](base::File file) { file.Close(); }, std::move(file)));
    return;
  }

  std::move(callback).Run(std::move(file), base::Closure());
}

}  // namespace

AsyncFileUtilAdapter::AsyncFileUtilAdapter(
    FileSystemFileUtil* sync_file_util)
    : sync_file_util_(sync_file_util) {
  DCHECK(sync_file_util_.get());
}

AsyncFileUtilAdapter::~AsyncFileUtilAdapter() = default;

void AsyncFileUtilAdapter::CreateOrOpen(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& url,
    int file_flags,
    CreateOrOpenCallback callback) {
  FileSystemOperationContext* context_ptr = context.release();
  base::PostTaskAndReplyWithResult(
      context_ptr->task_runner(), FROM_HERE,
      BindOnce(&FileSystemFileUtil::CreateOrOpen,
               Unretained(sync_file_util_.get()), context_ptr, url, file_flags),
      BindOnce(&RunCreateOrOpenCallback, base::Owned(context_ptr),
               std::move(callback)));
}

void AsyncFileUtilAdapter::EnsureFileExists(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& url,
    EnsureFileExistsCallback callback) {
  EnsureFileExistsHelper* helper = new EnsureFileExistsHelper;
  FileSystemOperationContext* context_ptr = context.release();
  const bool success = context_ptr->task_runner()->PostTaskAndReply(
      FROM_HERE,
      BindOnce(&EnsureFileExistsHelper::RunWork, Unretained(helper),
               sync_file_util_.get(), base::Owned(context_ptr), url),
      BindOnce(&EnsureFileExistsHelper::Reply, Owned(helper),
               std::move(callback)));
  DCHECK(success);
}

void AsyncFileUtilAdapter::CreateDirectory(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& url,
    bool exclusive,
    bool recursive,
    StatusCallback callback) {
  FileSystemOperationContext* context_ptr = context.release();
  const bool success = base::PostTaskAndReplyWithResult(
      context_ptr->task_runner(), FROM_HERE,
      base::BindOnce(&FileSystemFileUtil::CreateDirectory,
                     Unretained(sync_file_util_.get()),
                     base::Owned(context_ptr), url, exclusive, recursive),
      std::move(callback));
  DCHECK(success);
}

void AsyncFileUtilAdapter::GetFileInfo(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& url,
    int fields,
    GetFileInfoCallback callback) {
  FileSystemOperationContext* context_ptr = context.release();
  GetFileInfoHelper* helper = new GetFileInfoHelper;
  bool calculate_total_size =
      (fields & FileSystemOperation::GET_METADATA_FIELD_TOTAL_SIZE);
  const bool success = context_ptr->task_runner()->PostTaskAndReply(
      FROM_HERE,
      BindOnce(&GetFileInfoHelper::GetFileInfo, Unretained(helper),
               sync_file_util_.get(), base::Owned(context_ptr), url,
               calculate_total_size),
      BindOnce(&GetFileInfoHelper::ReplyFileInfo, Owned(helper),
               std::move(callback)));
  DCHECK(success);
}

void AsyncFileUtilAdapter::ReadDirectory(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& url,
    ReadDirectoryCallback callback) {
  FileSystemOperationContext* context_ptr = context.release();
  const bool success = context_ptr->task_runner()->PostTask(
      FROM_HERE,
      BindOnce(&ReadDirectoryHelper, sync_file_util_.get(),
               base::Owned(context_ptr), url,
               base::RetainedRef(base::ThreadTaskRunnerHandle::Get()),
               callback));
  DCHECK(success);
}

void AsyncFileUtilAdapter::Touch(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& url,
    const base::Time& last_access_time,
    const base::Time& last_modified_time,
    StatusCallback callback) {
  FileSystemOperationContext* context_ptr = context.release();
  const bool success = base::PostTaskAndReplyWithResult(
      context_ptr->task_runner(), FROM_HERE,
      base::BindOnce(
          &FileSystemFileUtil::Touch, Unretained(sync_file_util_.get()),
          base::Owned(context_ptr), url, last_access_time, last_modified_time),
      std::move(callback));
  DCHECK(success);
}

void AsyncFileUtilAdapter::Truncate(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& url,
    int64_t length,
    StatusCallback callback) {
  FileSystemOperationContext* context_ptr = context.release();
  const bool success = base::PostTaskAndReplyWithResult(
      context_ptr->task_runner(), FROM_HERE,
      base::BindOnce(&FileSystemFileUtil::Truncate,
                     Unretained(sync_file_util_.get()),
                     base::Owned(context_ptr), url, length),
      std::move(callback));
  DCHECK(success);
}

void AsyncFileUtilAdapter::CopyFileLocal(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& src_url,
    const FileSystemURL& dest_url,
    CopyOrMoveOption option,
    CopyFileProgressCallback progress_callback,
    StatusCallback callback) {
  // TODO(hidehiko): Support progress_callback.
  FileSystemOperationContext* context_ptr = context.release();
  const bool success = base::PostTaskAndReplyWithResult(
      context_ptr->task_runner(), FROM_HERE,
      base::BindOnce(&FileSystemFileUtil::CopyOrMoveFile,
                     Unretained(sync_file_util_.get()),
                     base::Owned(context_ptr), src_url, dest_url, option,
                     true /* copy */),
      std::move(callback));
  DCHECK(success);
}

void AsyncFileUtilAdapter::MoveFileLocal(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& src_url,
    const FileSystemURL& dest_url,
    CopyOrMoveOption option,
    StatusCallback callback) {
  FileSystemOperationContext* context_ptr = context.release();
  const bool success = base::PostTaskAndReplyWithResult(
      context_ptr->task_runner(), FROM_HERE,
      base::BindOnce(&FileSystemFileUtil::CopyOrMoveFile,
                     Unretained(sync_file_util_.get()),
                     base::Owned(context_ptr), src_url, dest_url, option,
                     false /* copy */),
      std::move(callback));
  DCHECK(success);
}

void AsyncFileUtilAdapter::CopyInForeignFile(
    std::unique_ptr<FileSystemOperationContext> context,
    const base::FilePath& src_file_path,
    const FileSystemURL& dest_url,
    StatusCallback callback) {
  FileSystemOperationContext* context_ptr = context.release();
  const bool success = base::PostTaskAndReplyWithResult(
      context_ptr->task_runner(), FROM_HERE,
      base::BindOnce(&FileSystemFileUtil::CopyInForeignFile,
                     Unretained(sync_file_util_.get()),
                     base::Owned(context_ptr), src_file_path, dest_url),
      std::move(callback));
  DCHECK(success);
}

void AsyncFileUtilAdapter::DeleteFile(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& url,
    StatusCallback callback) {
  FileSystemOperationContext* context_ptr = context.release();
  const bool success = base::PostTaskAndReplyWithResult(
      context_ptr->task_runner(), FROM_HERE,
      base::BindOnce(&FileSystemFileUtil::DeleteFile,
                     Unretained(sync_file_util_.get()),
                     base::Owned(context_ptr), url),
      std::move(callback));
  DCHECK(success);
}

void AsyncFileUtilAdapter::DeleteDirectory(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& url,
    StatusCallback callback) {
  FileSystemOperationContext* context_ptr = context.release();
  const bool success = base::PostTaskAndReplyWithResult(
      context_ptr->task_runner(), FROM_HERE,
      base::BindOnce(&FileSystemFileUtil::DeleteDirectory,
                     Unretained(sync_file_util_.get()),
                     base::Owned(context_ptr), url),
      std::move(callback));
  DCHECK(success);
}

void AsyncFileUtilAdapter::DeleteRecursively(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& url,
    StatusCallback callback) {
  std::move(callback).Run(base::File::FILE_ERROR_INVALID_OPERATION);
}

void AsyncFileUtilAdapter::CreateSnapshotFile(
    std::unique_ptr<FileSystemOperationContext> context,
    const FileSystemURL& url,
    CreateSnapshotFileCallback callback) {
  FileSystemOperationContext* context_ptr = context.release();
  GetFileInfoHelper* helper = new GetFileInfoHelper;
  const bool success = context_ptr->task_runner()->PostTaskAndReply(
      FROM_HERE,
      BindOnce(&GetFileInfoHelper::CreateSnapshotFile, Unretained(helper),
               sync_file_util_.get(), base::Owned(context_ptr), url),
      BindOnce(&GetFileInfoHelper::ReplySnapshotFile, Owned(helper),
               std::move(callback)));
  DCHECK(success);
}

}  // namespace storage
