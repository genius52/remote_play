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
// $hash=887754026eb62e631a0279fbba8fb1c370f0bcf7$
//

#include "libcef_dll/cpptoc/cookie_manager_cpptoc.h"
#include "libcef_dll/ctocpp/completion_callback_ctocpp.h"
#include "libcef_dll/ctocpp/cookie_visitor_ctocpp.h"
#include "libcef_dll/ctocpp/delete_cookies_callback_ctocpp.h"
#include "libcef_dll/ctocpp/set_cookie_callback_ctocpp.h"
#include "libcef_dll/transfer_util.h"

// GLOBAL FUNCTIONS - Body may be edited by hand.

CEF_EXPORT cef_cookie_manager_t* cef_cookie_manager_get_global_manager(
    cef_completion_callback_t* callback) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  // Unverified params: callback

  // Execute
  CefRefPtr<CefCookieManager> _retval = CefCookieManager::GetGlobalManager(
      CefCompletionCallbackCToCpp::Wrap(callback));

  // Return type: refptr_same
  return CefCookieManagerCppToC::Wrap(_retval);
}

namespace {

// MEMBER FUNCTIONS - Body may be edited by hand.

void CEF_CALLBACK
cookie_manager_set_supported_schemes(struct _cef_cookie_manager_t* self,
                                     cef_string_list_t schemes,
                                     int include_defaults,
                                     cef_completion_callback_t* callback) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return;
  // Verify param: schemes; type: string_vec_byref_const
  DCHECK(schemes);
  if (!schemes)
    return;
  // Unverified params: callback

  // Translate param: schemes; type: string_vec_byref_const
  std::vector<CefString> schemesList;
  transfer_string_list_contents(schemes, schemesList);

  // Execute
  CefCookieManagerCppToC::Get(self)->SetSupportedSchemes(
      schemesList, include_defaults ? true : false,
      CefCompletionCallbackCToCpp::Wrap(callback));
}

int CEF_CALLBACK
cookie_manager_visit_all_cookies(struct _cef_cookie_manager_t* self,
                                 struct _cef_cookie_visitor_t* visitor) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return 0;
  // Verify param: visitor; type: refptr_diff
  DCHECK(visitor);
  if (!visitor)
    return 0;

  // Execute
  bool _retval = CefCookieManagerCppToC::Get(self)->VisitAllCookies(
      CefCookieVisitorCToCpp::Wrap(visitor));

  // Return type: bool
  return _retval;
}

int CEF_CALLBACK
cookie_manager_visit_url_cookies(struct _cef_cookie_manager_t* self,
                                 const cef_string_t* url,
                                 int includeHttpOnly,
                                 struct _cef_cookie_visitor_t* visitor) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return 0;
  // Verify param: url; type: string_byref_const
  DCHECK(url);
  if (!url)
    return 0;
  // Verify param: visitor; type: refptr_diff
  DCHECK(visitor);
  if (!visitor)
    return 0;

  // Execute
  bool _retval = CefCookieManagerCppToC::Get(self)->VisitUrlCookies(
      CefString(url), includeHttpOnly ? true : false,
      CefCookieVisitorCToCpp::Wrap(visitor));

  // Return type: bool
  return _retval;
}

int CEF_CALLBACK
cookie_manager_set_cookie(struct _cef_cookie_manager_t* self,
                          const cef_string_t* url,
                          const struct _cef_cookie_t* cookie,
                          struct _cef_set_cookie_callback_t* callback) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return 0;
  // Verify param: url; type: string_byref_const
  DCHECK(url);
  if (!url)
    return 0;
  // Verify param: cookie; type: struct_byref_const
  DCHECK(cookie);
  if (!cookie)
    return 0;
  // Unverified params: callback

  // Translate param: cookie; type: struct_byref_const
  CefCookie cookieObj;
  if (cookie)
    cookieObj.Set(*cookie, false);

  // Execute
  bool _retval = CefCookieManagerCppToC::Get(self)->SetCookie(
      CefString(url), cookieObj, CefSetCookieCallbackCToCpp::Wrap(callback));

  // Return type: bool
  return _retval;
}

int CEF_CALLBACK
cookie_manager_delete_cookies(struct _cef_cookie_manager_t* self,
                              const cef_string_t* url,
                              const cef_string_t* cookie_name,
                              struct _cef_delete_cookies_callback_t* callback) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return 0;
  // Unverified params: url, cookie_name, callback

  // Execute
  bool _retval = CefCookieManagerCppToC::Get(self)->DeleteCookies(
      CefString(url), CefString(cookie_name),
      CefDeleteCookiesCallbackCToCpp::Wrap(callback));

  // Return type: bool
  return _retval;
}

int CEF_CALLBACK
cookie_manager_flush_store(struct _cef_cookie_manager_t* self,
                           cef_completion_callback_t* callback) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return 0;
  // Unverified params: callback

  // Execute
  bool _retval = CefCookieManagerCppToC::Get(self)->FlushStore(
      CefCompletionCallbackCToCpp::Wrap(callback));

  // Return type: bool
  return _retval;
}

}  // namespace

// CONSTRUCTOR - Do not edit by hand.

CefCookieManagerCppToC::CefCookieManagerCppToC() {
  GetStruct()->set_supported_schemes = cookie_manager_set_supported_schemes;
  GetStruct()->visit_all_cookies = cookie_manager_visit_all_cookies;
  GetStruct()->visit_url_cookies = cookie_manager_visit_url_cookies;
  GetStruct()->set_cookie = cookie_manager_set_cookie;
  GetStruct()->delete_cookies = cookie_manager_delete_cookies;
  GetStruct()->flush_store = cookie_manager_flush_store;
}

// DESTRUCTOR - Do not edit by hand.

CefCookieManagerCppToC::~CefCookieManagerCppToC() {}

template <>
CefRefPtr<CefCookieManager> CefCppToCRefCounted<
    CefCookieManagerCppToC,
    CefCookieManager,
    cef_cookie_manager_t>::UnwrapDerived(CefWrapperType type,
                                         cef_cookie_manager_t* s) {
  NOTREACHED() << "Unexpected class type: " << type;
  return NULL;
}

template <>
CefWrapperType CefCppToCRefCounted<CefCookieManagerCppToC,
                                   CefCookieManager,
                                   cef_cookie_manager_t>::kWrapperType =
    WT_COOKIE_MANAGER;