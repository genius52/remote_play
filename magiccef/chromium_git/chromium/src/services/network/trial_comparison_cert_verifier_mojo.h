// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_TRIAL_COMPARISON_CERT_VERIFIER_MOJO_H_
#define SERVICES_NETWORK_TRIAL_COMPARISON_CERT_VERIFIER_MOJO_H_

#include <memory>

#include "base/component_export.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "base/optional.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "net/cert/cert_verifier.h"
#include "services/network/public/mojom/trial_comparison_cert_verifier.mojom.h"

namespace net {
class CertVerifyProc;
class CertVerifyResult;
class TrialComparisonCertVerifier;
}  // namespace net

namespace network {

// Wrapper around TrialComparisonCertVerifier that does trial configuration and
// reporting over Mojo pipes.
class COMPONENT_EXPORT(NETWORK_SERVICE) TrialComparisonCertVerifierMojo
    : public net::CertVerifier,
      public mojom::TrialComparisonCertVerifierConfigClient {
 public:
  // |initial_allowed| is the initial setting for whether the trial is allowed.
  // |config_client_request| is the Mojo pipe over which trial configuration
  // updates are received.
  // |report_client| is the Mojo pipe used to send trial reports.
  // |primary_verify_proc| and |trial_verify_proc| are the CertVerifyProc
  // implementations to compare.
  TrialComparisonCertVerifierMojo(
      bool initial_allowed,
      mojom::TrialComparisonCertVerifierConfigClientRequest
          config_client_request,
      mojom::TrialComparisonCertVerifierReportClientPtrInfo report_client,
      scoped_refptr<net::CertVerifyProc> primary_verify_proc,
      scoped_refptr<net::CertVerifyProc> trial_verify_proc);
  ~TrialComparisonCertVerifierMojo() override;

  // CertVerifier implementation
  int Verify(const RequestParams& params,
             net::CertVerifyResult* verify_result,
             net::CompletionOnceCallback callback,
             std::unique_ptr<Request>* out_req,
             const net::NetLogWithSource& net_log) override;
  void SetConfig(const Config& config) override;

 private:
  // mojom::TrialComparisonCertVerifierConfigClient implementation:
  void OnTrialConfigUpdated(bool allowed) override;

  mojo::Binding<mojom::TrialComparisonCertVerifierConfigClient> binding_;

  mojom::TrialComparisonCertVerifierReportClientPtr report_client_;

  std::unique_ptr<net::TrialComparisonCertVerifier>
      trial_comparison_cert_verifier_;

  DISALLOW_COPY_AND_ASSIGN(TrialComparisonCertVerifierMojo);
};

}  // namespace network

#endif  // SERVICES_NETWORK_TRIAL_COMPARISON_CERT_VERIFIER_MOJO_H_
