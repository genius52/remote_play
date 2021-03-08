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
// $hash=75b119ac78b228395dc573a23c616e94d11d0c4c$
//

#include "libcef_dll/ctocpp/register_cdm_callback_ctocpp.h"
#include "libcef_dll/shutdown_checker.h"

// VIRTUAL METHODS - Body may be edited by hand.

NO_SANITIZE("cfi-icall")
void CefRegisterCdmCallbackCToCpp::OnCdmRegistrationComplete(
    cef_cdm_registration_error_t result,
    const CefString& error_message) {
  shutdown_checker::AssertNotShutdown();

  cef_register_cdm_callback_t* _struct = GetStruct();
  if (CEF_MEMBER_MISSING(_struct, on_cdm_registration_complete))
    return;

  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  // Unverified params: error_message

  // Execute
  _struct->on_cdm_registration_complete(_struct, result,
                                        error_message.GetStruct());
}

// CONSTRUCTOR - Do not edit by hand.

CefRegisterCdmCallbackCToCpp::CefRegisterCdmCallbackCToCpp() {}

// DESTRUCTOR - Do not edit by hand.

CefRegisterCdmCallbackCToCpp::~CefRegisterCdmCallbackCToCpp() {
  shutdown_checker::AssertNotShutdown();
}

template <>
cef_register_cdm_callback_t* CefCToCppRefCounted<
    CefRegisterCdmCallbackCToCpp,
    CefRegisterCdmCallback,
    cef_register_cdm_callback_t>::UnwrapDerived(CefWrapperType type,
                                                CefRegisterCdmCallback* c) {
  NOTREACHED() << "Unexpected class type: " << type;
  return NULL;
}

template <>
CefWrapperType CefCToCppRefCounted<CefRegisterCdmCallbackCToCpp,
                                   CefRegisterCdmCallback,
                                   cef_register_cdm_callback_t>::kWrapperType =
    WT_REGISTER_CDM_CALLBACK;
