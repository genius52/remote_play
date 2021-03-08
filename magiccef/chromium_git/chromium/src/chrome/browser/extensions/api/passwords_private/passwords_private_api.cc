// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/passwords_private/passwords_private_api.h"

#include <memory>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/values.h"
#include "chrome/browser/extensions/api/passwords_private/passwords_private_delegate_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/common/extensions/api/passwords_private.h"
#include "components/password_manager/core/browser/manage_passwords_referrer.h"
#include "components/password_manager/core/browser/password_manager_util.h"
#include "components/sync/driver/sync_service.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_function_registry.h"

namespace extensions {

////////////////////////////////////////////////////////////////////////////////
// PasswordsPrivateRecordPasswordsPageAccessInSettingsFunction

PasswordsPrivateRecordPasswordsPageAccessInSettingsFunction::
    ~PasswordsPrivateRecordPasswordsPageAccessInSettingsFunction() {}

ExtensionFunction::ResponseAction
PasswordsPrivateRecordPasswordsPageAccessInSettingsFunction::Run() {
  UMA_HISTOGRAM_ENUMERATION(
      "PasswordManager.ManagePasswordsReferrer",
      password_manager::ManagePasswordsReferrer::kChromeSettings);
  if (password_manager_util::IsSyncingWithNormalEncryption(
          ProfileSyncServiceFactory::GetForProfile(
              Profile::FromBrowserContext(browser_context())))) {
    // We record this second histogram to better understand the impact of the
    // Google Password Manager experiment for signed in and syncing users.
    UMA_HISTOGRAM_ENUMERATION(
        "PasswordManager.ManagePasswordsReferrerSignedInAndSyncing",
        password_manager::ManagePasswordsReferrer::kChromeSettings);
  }
  return RespondNow(NoArguments());
}

////////////////////////////////////////////////////////////////////////////////
// PasswordsPrivateChangeSavedPasswordFunction

PasswordsPrivateChangeSavedPasswordFunction::
    ~PasswordsPrivateChangeSavedPasswordFunction() {}

ExtensionFunction::ResponseAction
PasswordsPrivateChangeSavedPasswordFunction::Run() {
  std::unique_ptr<api::passwords_private::ChangeSavedPassword::Params>
      parameters =
          api::passwords_private::ChangeSavedPassword::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(parameters.get());

  PasswordsPrivateDelegateFactory::GetForBrowserContext(browser_context(),
                                                        true /* create */)
      ->ChangeSavedPassword(
          parameters->id, base::UTF8ToUTF16(parameters->new_username),
          parameters->new_password ? base::make_optional(base::UTF8ToUTF16(
                                         *parameters->new_password))
                                   : base::nullopt);

  return RespondNow(NoArguments());
}

////////////////////////////////////////////////////////////////////////////////
// PasswordsPrivateRemoveSavedPasswordFunction

PasswordsPrivateRemoveSavedPasswordFunction::
    ~PasswordsPrivateRemoveSavedPasswordFunction() {}

ExtensionFunction::ResponseAction
    PasswordsPrivateRemoveSavedPasswordFunction::Run() {
  std::unique_ptr<api::passwords_private::RemoveSavedPassword::Params>
      parameters =
          api::passwords_private::RemoveSavedPassword::Params::Create(*args_);
  EXTENSION_FUNCTION_VALIDATE(parameters.get());

  PasswordsPrivateDelegate* delegate =
      PasswordsPrivateDelegateFactory::GetForBrowserContext(browser_context(),
                                                            true /* create */);

  delegate->RemoveSavedPassword(parameters->id);

  return RespondNow(NoArguments());
}

////////////////////////////////////////////////////////////////////////////////
// PasswordsPrivateRemovePasswordExceptionFunction

PasswordsPrivateRemovePasswordExceptionFunction::
    ~PasswordsPrivateRemovePasswordExceptionFunction() {}

ExtensionFunction::ResponseAction
    PasswordsPrivateRemovePasswordExceptionFunction::Run() {
  std::unique_ptr<api::passwords_private::RemovePasswordException::Params>
      parameters =
          api::passwords_private::RemovePasswordException::Params::Create(
              *args_);
  EXTENSION_FUNCTION_VALIDATE(parameters.get());

  PasswordsPrivateDelegate* delegate =
      PasswordsPrivateDelegateFactory::GetForBrowserContext(browser_context(),
                                                            true /* create */);
  delegate->RemovePasswordException(parameters->id);

  return RespondNow(NoArguments());
}

////////////////////////////////////////////////////////////////////////////////
// PasswordsPrivateUndoRemoveSavedPasswordOrExceptionFunction

PasswordsPrivateUndoRemoveSavedPasswordOrExceptionFunction::
    ~PasswordsPrivateUndoRemoveSavedPasswordOrExceptionFunction() {}

ExtensionFunction::ResponseAction
PasswordsPrivateUndoRemoveSavedPasswordOrExceptionFunction::Run() {
  PasswordsPrivateDelegate* delegate =
      PasswordsPrivateDelegateFactory::GetForBrowserContext(browser_context(),
                                                            true /* create */);
  delegate->UndoRemoveSavedPasswordOrException();

  return RespondNow(NoArguments());
}

////////////////////////////////////////////////////////////////////////////////
// PasswordsPrivateRequestPlaintextPasswordFunction

PasswordsPrivateRequestPlaintextPasswordFunction::
    ~PasswordsPrivateRequestPlaintextPasswordFunction() {}

ExtensionFunction::ResponseAction
    PasswordsPrivateRequestPlaintextPasswordFunction::Run() {
  std::unique_ptr<api::passwords_private::RequestPlaintextPassword::Params>
      parameters =
          api::passwords_private::RequestPlaintextPassword::Params::Create(
              *args_);
  EXTENSION_FUNCTION_VALIDATE(parameters.get());

  PasswordsPrivateDelegate* delegate =
      PasswordsPrivateDelegateFactory::GetForBrowserContext(browser_context(),
                                                            true /* create */);
  delegate->RequestShowPassword(
      parameters->id,
      base::BindOnce(
          &PasswordsPrivateRequestPlaintextPasswordFunction::GotPassword, this),
      GetSenderWebContents());

  // GotPassword() might respond before we reach this point.
  return did_respond() ? AlreadyResponded() : RespondLater();
}

void PasswordsPrivateRequestPlaintextPasswordFunction::GotPassword(
    base::Optional<base::string16> password) {
  if (password)
    Respond(OneArgument(std::make_unique<base::Value>(std::move(*password))));
  else
    Respond(NoArguments());
}

////////////////////////////////////////////////////////////////////////////////
// PasswordsPrivateGetSavedPasswordListFunction

PasswordsPrivateGetSavedPasswordListFunction::
    ~PasswordsPrivateGetSavedPasswordListFunction() {}

ExtensionFunction::ResponseAction
PasswordsPrivateGetSavedPasswordListFunction::Run() {
  // GetList() can immediately call GotList() (which would Respond() before
  // RespondLater()). So we post a task to preserve order.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&PasswordsPrivateGetSavedPasswordListFunction::GetList,
                     this));
  return RespondLater();
}

void PasswordsPrivateGetSavedPasswordListFunction::GetList() {
  PasswordsPrivateDelegate* delegate =
      PasswordsPrivateDelegateFactory::GetForBrowserContext(browser_context(),
                                                            true /* create */);
  delegate->GetSavedPasswordsList(base::BindOnce(
      &PasswordsPrivateGetSavedPasswordListFunction::GotList, this));
}

void PasswordsPrivateGetSavedPasswordListFunction::GotList(
    const PasswordsPrivateDelegate::UiEntries& list) {
  Respond(ArgumentList(
      api::passwords_private::GetSavedPasswordList::Results::Create(list)));
}

////////////////////////////////////////////////////////////////////////////////
// PasswordsPrivateGetPasswordExceptionListFunction

PasswordsPrivateGetPasswordExceptionListFunction::
    ~PasswordsPrivateGetPasswordExceptionListFunction() {}

ExtensionFunction::ResponseAction
PasswordsPrivateGetPasswordExceptionListFunction::Run() {
  // GetList() can immediately call GotList() (which would Respond() before
  // RespondLater()). So we post a task to preserve order.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::BindOnce(&PasswordsPrivateGetPasswordExceptionListFunction::GetList,
                     this));
  return RespondLater();
}

void PasswordsPrivateGetPasswordExceptionListFunction::GetList() {
  PasswordsPrivateDelegate* delegate =
      PasswordsPrivateDelegateFactory::GetForBrowserContext(browser_context(),
                                                            true /* create */);
  delegate->GetPasswordExceptionsList(base::Bind(
      &PasswordsPrivateGetPasswordExceptionListFunction::GotList, this));
}

void PasswordsPrivateGetPasswordExceptionListFunction::GotList(
    const PasswordsPrivateDelegate::ExceptionEntries& entries) {
  Respond(ArgumentList(
      api::passwords_private::GetPasswordExceptionList::Results::Create(
          entries)));
}

////////////////////////////////////////////////////////////////////////////////
// PasswordsPrivateImportPasswordsFunction

PasswordsPrivateImportPasswordsFunction::
    ~PasswordsPrivateImportPasswordsFunction() {}

ExtensionFunction::ResponseAction
PasswordsPrivateImportPasswordsFunction::Run() {
  PasswordsPrivateDelegate* delegate =
      PasswordsPrivateDelegateFactory::GetForBrowserContext(browser_context(),
                                                            true /* create */);
  delegate->ImportPasswords(GetSenderWebContents());
  return RespondNow(NoArguments());
}

////////////////////////////////////////////////////////////////////////////////
// PasswordsPrivateExportPasswordsFunction

PasswordsPrivateExportPasswordsFunction::
    ~PasswordsPrivateExportPasswordsFunction() {}

ExtensionFunction::ResponseAction
PasswordsPrivateExportPasswordsFunction::Run() {
  PasswordsPrivateDelegate* delegate =
      PasswordsPrivateDelegateFactory::GetForBrowserContext(browser_context(),
                                                            true /* create */);
  delegate->ExportPasswords(
      base::BindOnce(
          &PasswordsPrivateExportPasswordsFunction::ExportRequestCompleted,
          this),
      GetSenderWebContents());
  return RespondLater();
}

void PasswordsPrivateExportPasswordsFunction::ExportRequestCompleted(
    const std::string& error) {
  if (error.empty())
    Respond(NoArguments());
  else
    Error(error);
}

////////////////////////////////////////////////////////////////////////////////
// PasswordsPrivateCancelExportPasswordsFunction

PasswordsPrivateCancelExportPasswordsFunction::
    ~PasswordsPrivateCancelExportPasswordsFunction() {}

ExtensionFunction::ResponseAction
PasswordsPrivateCancelExportPasswordsFunction::Run() {
  PasswordsPrivateDelegate* delegate =
      PasswordsPrivateDelegateFactory::GetForBrowserContext(browser_context(),
                                                            true /* create */);
  delegate->CancelExportPasswords();
  return RespondNow(NoArguments());
}

////////////////////////////////////////////////////////////////////////////////
// PasswordsPrivateRequestExportProgressStatusFunction

PasswordsPrivateRequestExportProgressStatusFunction::
    ~PasswordsPrivateRequestExportProgressStatusFunction() {}

ExtensionFunction::ResponseAction
PasswordsPrivateRequestExportProgressStatusFunction::Run() {
  PasswordsPrivateDelegate* delegate =
      PasswordsPrivateDelegateFactory::GetForBrowserContext(browser_context(),
                                                            true /* create */);
  return RespondNow(OneArgument(std::make_unique<base::Value>(
      ToString(delegate->GetExportProgressStatus()))));
}

}  // namespace extensions
