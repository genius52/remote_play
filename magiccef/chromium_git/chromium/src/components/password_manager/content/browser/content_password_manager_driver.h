// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PASSWORD_MANAGER_CONTENT_BROWSER_CONTENT_PASSWORD_MANAGER_DRIVER_H_
#define COMPONENTS_PASSWORD_MANAGER_CONTENT_BROWSER_CONTENT_PASSWORD_MANAGER_DRIVER_H_

#include <map>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "components/autofill/content/common/autofill_agent.mojom.h"
#include "components/autofill/content/common/autofill_driver.mojom.h"
#include "components/autofill/core/common/password_form_field_prediction_map.h"
#include "components/autofill/core/common/password_form_generation_data.h"
#include "components/password_manager/core/browser/password_autofill_manager.h"
#include "components/password_manager/core/browser/password_generation_frame_helper.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "components/password_manager/core/browser/password_manager_driver.h"
#include "mojo/public/cpp/bindings/associated_binding.h"

namespace autofill {
struct PasswordForm;
}

namespace content {
class NavigationHandle;
class RenderFrameHost;
}

namespace password_manager {
enum class BadMessageReason;

// There is one ContentPasswordManagerDriver per RenderFrameHost.
// The lifetime is managed by the ContentPasswordManagerDriverFactory.
class ContentPasswordManagerDriver
    : public PasswordManagerDriver,
      public autofill::mojom::PasswordManagerDriver {
 public:
  ContentPasswordManagerDriver(content::RenderFrameHost* render_frame_host,
                               PasswordManagerClient* client,
                               autofill::AutofillClient* autofill_client);
  ~ContentPasswordManagerDriver() override;

  // Gets the driver for |render_frame_host|.
  static ContentPasswordManagerDriver* GetForRenderFrameHost(
      content::RenderFrameHost* render_frame_host);

  void BindRequest(
      autofill::mojom::PasswordManagerDriverAssociatedRequest request);

  // PasswordManagerDriver implementation.
  void FillPasswordForm(
      const autofill::PasswordFormFillData& form_data) override;
  void AllowPasswordGenerationForForm(
      const autofill::PasswordForm& form) override;
  void FormsEligibleForGenerationFound(
      const std::vector<autofill::PasswordFormGenerationData>& forms) override;
  void FormEligibleForGenerationFound(
      const autofill::NewPasswordFormGenerationData& form) override;
  void AutofillDataReceived(
      const std::map<autofill::FormData,
                     autofill::PasswordFormFieldPredictionMap>& predictions)
      override;
  void GeneratedPasswordAccepted(const base::string16& password) override;
  void FillSuggestion(const base::string16& username,
                      const base::string16& password) override;
  void FillIntoFocusedField(bool is_password,
                            const base::string16& credential,
                            base::OnceCallback<void(autofill::FillingStatus)>
                                compeleted_callback) override;
  void PreviewSuggestion(const base::string16& username,
                         const base::string16& password) override;
  void ShowInitialPasswordAccountSuggestions(
      const autofill::PasswordFormFillData& form_data) override;
  void ClearPreviewedForm() override;
  PasswordGenerationFrameHelper* GetPasswordGenerationHelper() override;
  PasswordManager* GetPasswordManager() override;
  PasswordAutofillManager* GetPasswordAutofillManager() override;
  void SendLoggingAvailability() override;
  autofill::AutofillDriver* GetAutofillDriver() override;
  bool IsMainFrame() const override;
  GURL GetLastCommittedURL() const override;

  void DidNavigateFrame(content::NavigationHandle* navigation_handle);
  // Notify the renderer that the user wants to generate password manually.
  void GeneratePassword(autofill::mojom::PasswordGenerationAgent::
                            UserTriggeredGeneratePasswordCallback callback);

  content::RenderFrameHost* render_frame_host() const {
    return render_frame_host_;
  }

 protected:
  // autofill::mojom::PasswordManagerDriver:
  // Note that these messages received from a potentially compromised renderer.
  // For that reason, any access to form data should be validated via
  // bad_message::CheckChildProcessSecurityPolicy.
  void PasswordFormsParsed(
      const std::vector<autofill::PasswordForm>& forms) override;
  void PasswordFormsRendered(
      const std::vector<autofill::PasswordForm>& visible_forms,
      bool did_stop_loading) override;
  void PasswordFormSubmitted(
      const autofill::PasswordForm& password_form) override;
  void ShowManualFallbackForSaving(const autofill::PasswordForm& form) override;
  void HideManualFallbackForSaving() override;
  void SameDocumentNavigation(
      const autofill::PasswordForm& password_form) override;
  void ShowPasswordSuggestions(base::i18n::TextDirection text_direction,
                               const base::string16& typed_username,
                               int options,
                               const gfx::RectF& bounds) override;
  void RecordSavePasswordProgress(const std::string& log) override;
  void UserModifiedPasswordField() override;
  void CheckSafeBrowsingReputation(const GURL& form_action,
                                   const GURL& frame_url) override;
  void FocusedInputChanged(
      autofill::mojom::FocusedFieldType focused_field_type) override;
  void LogFirstFillingResult(uint32_t form_renderer_id,
                             int32_t result) override;

 private:
  const autofill::mojom::AutofillAgentAssociatedPtr& GetAutofillAgent();

  const autofill::mojom::PasswordAutofillAgentAssociatedPtr&
  GetPasswordAutofillAgent();

  const autofill::mojom::PasswordGenerationAgentAssociatedPtr&
  GetPasswordGenerationAgent();

  content::RenderFrameHost* render_frame_host_;
  PasswordManagerClient* client_;
  PasswordGenerationFrameHelper password_generation_helper_;
  PasswordAutofillManager password_autofill_manager_;

  // It should be filled in the constructor, since later the frame might be
  // detached and it would be impossible to check whether the frame is a main
  // frame.
  const bool is_main_frame_;

  autofill::mojom::PasswordAutofillAgentAssociatedPtr password_autofill_agent_;

  autofill::mojom::PasswordGenerationAgentAssociatedPtr password_gen_agent_;

  mojo::AssociatedBinding<autofill::mojom::PasswordManagerDriver>
      password_manager_binding_;

  base::WeakPtrFactory<ContentPasswordManagerDriver> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ContentPasswordManagerDriver);
};

}  // namespace password_manager

#endif  // COMPONENTS_PASSWORD_MANAGER_CONTENT_BROWSER_CONTENT_PASSWORD_MANAGER_DRIVER_H_
