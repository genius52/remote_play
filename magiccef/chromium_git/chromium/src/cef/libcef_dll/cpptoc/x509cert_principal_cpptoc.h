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
// $hash=5f39ce3caf8947a2813c04e3e93693c4356e8fdb$
//

#ifndef CEF_LIBCEF_DLL_CPPTOC_X509CERT_PRINCIPAL_CPPTOC_H_
#define CEF_LIBCEF_DLL_CPPTOC_X509CERT_PRINCIPAL_CPPTOC_H_
#pragma once

#if !defined(BUILDING_CEF_SHARED)
#error This file can be included DLL-side only
#endif

#include "include/capi/cef_x509_certificate_capi.h"
#include "include/cef_x509_certificate.h"
#include "libcef_dll/cpptoc/cpptoc_ref_counted.h"

// Wrap a C++ class with a C structure.
// This class may be instantiated and accessed DLL-side only.
class CefX509CertPrincipalCppToC
    : public CefCppToCRefCounted<CefX509CertPrincipalCppToC,
                                 CefX509CertPrincipal,
                                 cef_x509cert_principal_t> {
 public:
  CefX509CertPrincipalCppToC();
  virtual ~CefX509CertPrincipalCppToC();
};

#endif  // CEF_LIBCEF_DLL_CPPTOC_X509CERT_PRINCIPAL_CPPTOC_H_
