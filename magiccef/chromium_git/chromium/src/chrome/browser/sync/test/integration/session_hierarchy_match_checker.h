// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_TEST_INTEGRATION_SESSION_HIERARCHY_MATCH_CHECKER_H_
#define CHROME_BROWSER_SYNC_TEST_INTEGRATION_SESSION_HIERARCHY_MATCH_CHECKER_H_

#include "chrome/browser/sync/test/integration/single_client_status_change_checker.h"
#include "components/sync/test/fake_server/fake_server_verifier.h"
#include "components/sync/test/fake_server/sessions_hierarchy.h"

// Checker to block until the FakeServer records a SessionsHierarchy identical
// to the SessionsHierarchy specified in the constructor.
class SessionHierarchyMatchChecker : public SingleClientStatusChangeChecker {
 public:
  SessionHierarchyMatchChecker(
      const fake_server::SessionsHierarchy& sessions_hierarchy,
      syncer::ProfileSyncService* service,
      fake_server::FakeServer* fake_server);

  // StatusChangeChecker implementation.
  bool IsExitConditionSatisfied() override;
  std::string GetDebugMessage() const override;

 private:
  const fake_server::SessionsHierarchy sessions_hierarchy_;
  fake_server::FakeServerVerifier verifier_;
};

#endif  // CHROME_BROWSER_SYNC_TEST_INTEGRATION_SESSION_HIERARCHY_MATCH_CHECKER_H_