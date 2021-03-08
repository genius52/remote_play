;// Copyright 2018 The Chromium Authors. All rights reserved.
;// Use of this source code is governed by a BSD-style license that can be
;// found in the LICENSE file.
;//
;// Defines the names and types of messages that are logged with the SYSLOG
;// macro.
SeverityNames=(Informational=0x0:STATUS_SEVERITY_INFORMATIONAL
               Warning=0x1:STATUS_SEVERITY_WARNING
               Error=0x2:STATUS_SEVERITY_ERROR
               Fatal=0x3:STATUS_SEVERITY_FATAL
              )
FacilityNames=(GCP=0x0:FACILITY_SYSTEM)
LanguageNames=(English=0x409:MSG00409)

;// TODO(rogerta): Subdivide into more categories if needed.
MessageIdTypedef=WORD

MessageId=0x1
SymbolicName=GCP_CATEGORY
Language=English
GCP Events
.

MessageIdTypedef=DWORD

MessageId=0x100
Severity=Error
Facility=GCP
SymbolicName=MSG_LOG_MESSAGE
Language=English
%1!S!
.
