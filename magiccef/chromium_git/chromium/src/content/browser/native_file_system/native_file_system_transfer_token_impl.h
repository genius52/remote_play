// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_NATIVE_FILE_SYSTEM_NATIVE_FILE_SYSTEM_TRANSFER_TOKEN_IMPL_H_
#define CONTENT_BROWSER_NATIVE_FILE_SYSTEM_NATIVE_FILE_SYSTEM_TRANSFER_TOKEN_IMPL_H_

#include "storage/browser/fileapi/file_system_url.h"
#include "storage/browser/fileapi/isolated_context.h"
#include "third_party/blink/public/mojom/native_file_system/native_file_system_transfer_token.mojom.h"

namespace content {

// This is the browser side implementation of the NativeFileSystemTransferToken
// mojom interface.
//
// Instances of this class are immutable, but since this implements a mojo
// interface all its methods are called on the same sequence anyway.
class NativeFileSystemTransferTokenImpl
    : public blink::mojom::NativeFileSystemTransferToken {
 public:
  enum class HandleType { kFile, kDirectory };

  NativeFileSystemTransferTokenImpl(
      const storage::FileSystemURL& url,
      storage::IsolatedContext::ScopedFSHandle file_system,
      HandleType type);

  const base::UnguessableToken& token() const { return token_; }
  const storage::FileSystemURL& url() const { return url_; }
  HandleType type() const { return type_; }

  // blink::mojom::NativeFileSystemTransferToken:
  void GetInternalID(GetInternalIDCallback callback) override;

 private:
  const base::UnguessableToken token_;
  const storage::FileSystemURL url_;
  const storage::IsolatedContext::ScopedFSHandle file_system_;
  const HandleType type_;

  DISALLOW_COPY_AND_ASSIGN(NativeFileSystemTransferTokenImpl);
};

}  // namespace content

#endif  // CONTENT_BROWSER_NATIVE_FILE_SYSTEM_NATIVE_FILE_SYSTEM_TRANSFER_TOKEN_IMPL_H_
