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
// $hash=99f9355398dd9cffd37716e8aaff3e6002795985$
//

#ifndef CEF_LIBCEF_DLL_CPPTOC_TEST_TRANSLATOR_TEST_REF_PTR_LIBRARY_CHILD_CPPTOC_H_
#define CEF_LIBCEF_DLL_CPPTOC_TEST_TRANSLATOR_TEST_REF_PTR_LIBRARY_CHILD_CPPTOC_H_
#pragma once

#if !defined(BUILDING_CEF_SHARED)
#error This file can be included DLL-side only
#endif

#include "include/capi/test/cef_translator_test_capi.h"
#include "include/test/cef_translator_test.h"
#include "libcef_dll/cpptoc/cpptoc_ref_counted.h"

// Wrap a C++ class with a C structure.
// This class may be instantiated and accessed DLL-side only.
class CefTranslatorTestRefPtrLibraryChildCppToC
    : public CefCppToCRefCounted<CefTranslatorTestRefPtrLibraryChildCppToC,
                                 CefTranslatorTestRefPtrLibraryChild,
                                 cef_translator_test_ref_ptr_library_child_t> {
 public:
  CefTranslatorTestRefPtrLibraryChildCppToC();
  virtual ~CefTranslatorTestRefPtrLibraryChildCppToC();
};

#endif  // CEF_LIBCEF_DLL_CPPTOC_TEST_TRANSLATOR_TEST_REF_PTR_LIBRARY_CHILD_CPPTOC_H_
