// Copyright (c) 2019 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.
//
// ---------------------------------------------------------------------------
//
// This file was generated by the CEF translator tool. If making changes by
// hand only do so within the body of existing method and function
// implementations. See the translator.README.txt file in the tools directory
// for more information.
//
// $hash=bfe2b659fc2fd899a0f06e3608831a4132912564$
//

#ifndef CEF_LIBCEF_DLL_CTOCPP_WRITE_HANDLER_CTOCPP_H_
#define CEF_LIBCEF_DLL_CTOCPP_WRITE_HANDLER_CTOCPP_H_
#pragma once

#if !defined(BUILDING_CEF_SHARED)
#error This file can be included DLL-side only
#endif

#include "include/capi/cef_stream_capi.h"
#include "include/cef_stream.h"
#include "libcef_dll/ctocpp/ctocpp_ref_counted.h"

// Wrap a C structure with a C++ class.
// This class may be instantiated and accessed DLL-side only.
class CefWriteHandlerCToCpp : public CefCToCppRefCounted<CefWriteHandlerCToCpp,
                                                         CefWriteHandler,
                                                         cef_write_handler_t> {
 public:
  CefWriteHandlerCToCpp();
  virtual ~CefWriteHandlerCToCpp();

  // CefWriteHandler methods.
  size_t Write(const void* ptr, size_t size, size_t n) override;
  int Seek(int64 offset, int whence) override;
  int64 Tell() override;
  int Flush() override;
  bool MayBlock() override;
};

#endif  // CEF_LIBCEF_DLL_CTOCPP_WRITE_HANDLER_CTOCPP_H_