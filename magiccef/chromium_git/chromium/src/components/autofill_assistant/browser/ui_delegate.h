// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_UI_DELEGATE_H_
#define COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_UI_DELEGATE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/optional.h"
#include "components/autofill_assistant/browser/metrics.h"
#include "components/autofill_assistant/browser/payment_request.h"
#include "components/autofill_assistant/browser/rectf.h"
#include "components/autofill_assistant/browser/state.h"

namespace autofill_assistant {

// UI delegate called for script executions.
class UiDelegate {
 public:
  // Colors of the overlay. Empty string to use the default.
  struct OverlayColors {
    // Overlay background color.
    std::string background;

    // Color of the border around the highlighted portions of the overlay.
    std::string highlight_border;
  };

  virtual ~UiDelegate() = default;

  // Returns the current state of the controller.
  virtual AutofillAssistantState GetState() = 0;

  // Asks for updated coordinates for the touchable area. This is called to
  // speed up update of the touchable areas when there are good reasons to think
  // that the current coordinates are out of date, such as while scrolling.
  virtual void UpdateTouchableArea() = 0;

  // Called when user interaction within the allowed touchable area was
  // detected. This should cause rerun of preconditions check.
  virtual void OnUserInteractionInsideTouchableArea() = 0;

  // Returns a string describing the current execution context. This is useful
  // when analyzing feedback forms and for debugging in general.
  virtual std::string GetDebugContext() = 0;

  // Returns the current status message.
  virtual std::string GetStatusMessage() const = 0;

  // Returns the current contextual information. May be null if empty.
  virtual const Details* GetDetails() const = 0;

  // Returns the current info box data. May be null if empty.
  virtual const InfoBox* GetInfoBox() const = 0;

  // Returns the current progress; a percentage.
  virtual int GetProgress() const = 0;

  // Returns whether the progress bar is visible.
  virtual bool GetProgressVisible() const = 0;

  // Returns the current set of suggestions.
  virtual const std::vector<Chip>& GetSuggestions() const = 0;

  // Selects a suggestion, from the set of suggestions returned by
  // GetSuggestions().
  virtual void SelectSuggestion(int suggestion) = 0;

  // Returns the current set of actions.
  virtual const std::vector<Chip>& GetActions() const = 0;

  // Selects an action, from the set of actions returned by GetActions().
  virtual void SelectAction(int action) = 0;

  // If the controller is waiting for payment request information, this
  // field contains a non-null options describing the request.
  virtual const PaymentRequestOptions* GetPaymentRequestOptions() const = 0;

  // If the controller is waiting for payment request information, this
  // field contains a non-null object describing the currently selected data.
  virtual const PaymentInformation* GetPaymentRequestInformation() const = 0;

  // Sets shipping address, in response to the current payment request options.
  virtual void SetShippingAddress(
      std::unique_ptr<autofill::AutofillProfile> address) = 0;

  // Sets billing address, in response to the current payment request options.
  virtual void SetBillingAddress(
      std::unique_ptr<autofill::AutofillProfile> address) = 0;

  // Sets contact info, in response to the current payment request options.
  virtual void SetContactInfo(std::string name,
                              std::string phone,
                              std::string email) = 0;

  // Sets credit card, in response to the current payment request options.
  virtual void SetCreditCard(std::unique_ptr<autofill::CreditCard> card) = 0;

  // Sets the state of the third party terms & conditions, pertaining to the
  // current payment request options.
  virtual void SetTermsAndConditions(
      TermsAndConditionsState terms_and_conditions) = 0;

  // Adds the rectangles that correspond to the current touchable area to the
  // given vector.
  //
  // At the end of this call, |rectangles| contains one element per configured
  // rectangles, though these can correspond to empty rectangles. Coordinates
  // absolute CSS coordinates.
  //
  // Note that the vector is not cleared before rectangles are added.
  virtual void GetTouchableArea(std::vector<RectF>* rectangles) const = 0;

  // Returns the current size of the visual viewport. May be empty if unknown.
  //
  // The rectangle is expressed in absolute CSS coordinates.
  virtual void GetVisualViewport(RectF* viewport) const = 0;

  // Reports a fatal error to Autofill Assistant, which should then stop.
  virtual void OnFatalError(const std::string& error_message,
                            Metrics::DropOutReason reason) = 0;

  // Returns whether the viewport should be resized.
  virtual bool GetResizeViewport() = 0;

  virtual ConfigureBottomSheetProto::PeekMode GetPeekMode() = 0;

  // Fills in the overlay colors.
  virtual void GetOverlayColors(OverlayColors* colors) const = 0;

  // Returns the current form. May be null if there is no form to show.
  virtual const FormProto* GetForm() const = 0;

  // Sets a counter value.
  virtual void SetCounterValue(int input_index,
                               int counter_index,
                               int value) = 0;

  // Sets whether a selection choice is selected.
  virtual void SetChoiceSelected(int input_index,
                                 int choice_index,
                                 bool selected) = 0;

 protected:
  UiDelegate() = default;
};
}  // namespace autofill_assistant
#endif  // COMPONENTS_AUTOFILL_ASSISTANT_BROWSER_UI_DELEGATE_H_
