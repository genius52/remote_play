// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/installer/setup/install_worker.h"

#include <memory>
#include <string>
#include <tuple>

#include "base/strings/stringprintf.h"
#include "base/test/scoped_os_info_override_win.h"
#include "base/test/test_reg_util_win.h"
#include "base/version.h"
#include "base/win/registry.h"
#include "base/win/win_util.h"
#include "base/win/windows_version.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/install_static/install_util.h"
#include "chrome/install_static/test/scoped_install_details.h"
#include "chrome/installer/setup/installer_state.h"
#include "chrome/installer/setup/setup_util.h"
#include "chrome/installer/util/create_reg_key_work_item.h"
#include "chrome/installer/util/delete_reg_key_work_item.h"
#include "chrome/installer/util/delete_tree_work_item.h"
#include "chrome/installer/util/google_update_constants.h"
#include "chrome/installer/util/helper.h"
#include "chrome/installer/util/installation_state.h"
#include "chrome/installer/util/set_reg_value_work_item.h"
#include "chrome/installer/util/util_constants.h"
#include "chrome/installer/util/work_item_list.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::win::RegKey;
using installer::InstallationState;
using installer::InstallerState;
using installer::ProductState;

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::AtMost;
using ::testing::Bool;
using ::testing::Combine;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrCaseEq;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::TestParamInfo;
using ::testing::Values;

namespace {

// Mock classes to help with testing
//------------------------------------------------------------------------------

class MockWorkItemList : public WorkItemList {
 public:
  MockWorkItemList() {}

  MOCK_METHOD5(AddCopyTreeWorkItem,
               WorkItem*(const std::wstring&,
                         const std::wstring&,
                         const std::wstring&,
                         CopyOverWriteOption,
                         const std::wstring&));
  MOCK_METHOD1(AddCreateDirWorkItem, WorkItem* (const base::FilePath&));
  MOCK_METHOD3(AddCreateRegKeyWorkItem,
               WorkItem*(HKEY, const std::wstring&, REGSAM));
  MOCK_METHOD3(AddDeleteRegKeyWorkItem,
               WorkItem*(HKEY, const std::wstring&, REGSAM));
  MOCK_METHOD4(
      AddDeleteRegValueWorkItem,
      WorkItem*(HKEY, const std::wstring&, REGSAM, const std::wstring&));
  MOCK_METHOD2(AddDeleteTreeWorkItem,
               WorkItem*(const base::FilePath&, const base::FilePath&));
  MOCK_METHOD4(AddMoveTreeWorkItem,
               WorkItem*(const std::wstring&,
                         const std::wstring&,
                         const std::wstring&,
                         MoveTreeOption));
  // Workaround for gmock problems with disambiguating between string pointers
  // and DWORD.
  virtual WorkItem* AddSetRegValueWorkItem(HKEY a1,
                                           const std::wstring& a2,
                                           REGSAM a3,
                                           const std::wstring& a4,
                                           const std::wstring& a5,
                                           bool a6) {
    return AddSetRegStringValueWorkItem(a1, a2, a3, a4, a5, a6);
  }

  virtual WorkItem* AddSetRegValueWorkItem(HKEY a1,
                                           const std::wstring& a2,
                                           REGSAM a3,
                                           const std::wstring& a4,
                                           DWORD a5,
                                           bool a6) {
    return AddSetRegDwordValueWorkItem(a1, a2, a3, a4, a5, a6);
  }

  MOCK_METHOD6(AddSetRegStringValueWorkItem,
               WorkItem*(HKEY,
                         const std::wstring&,
                         REGSAM,
                         const std::wstring&,
                         const std::wstring&,
                         bool));
  MOCK_METHOD6(AddSetRegDwordValueWorkItem,
               WorkItem*(HKEY,
                         const std::wstring&,
                         REGSAM,
                         const std::wstring&,
                         DWORD,
                         bool));
  MOCK_METHOD3(AddSelfRegWorkItem, WorkItem* (const std::wstring&,
                                              bool,
                                              bool));
};

class MockProductState : public ProductState {
 public:
  // Takes ownership of |version|.
  void set_version(base::Version* version) { version_.reset(version); }
  void set_brand(const std::wstring& brand) { brand_ = brand; }
  void set_eula_accepted(DWORD eula_accepted) {
    has_eula_accepted_ = true;
    eula_accepted_ = eula_accepted;
  }
  void clear_eula_accepted() { has_eula_accepted_ = false; }
  void set_usagestats(DWORD usagestats) {
    has_usagestats_ = true;
    usagestats_ = usagestats;
  }
  void clear_usagestats() { has_usagestats_ = false; }
  void set_oem_install(const std::wstring& oem_install) {
    has_oem_install_ = true;
    oem_install_ = oem_install;
  }
  void clear_oem_install() { has_oem_install_ = false; }
  void SetUninstallProgram(const base::FilePath& setup_exe) {
    uninstall_command_ = base::CommandLine(setup_exe);
  }
  void AddUninstallSwitch(const std::string& option) {
    uninstall_command_.AppendSwitch(option);
  }
};

// Okay, so this isn't really a mock as such, but it does add setter methods
// to make it easier to build custom InstallationStates.
class MockInstallationState : public InstallationState {
 public:
  // Included for testing.
  void SetProductState(bool system_install,
                       const ProductState& product_state) {
    ProductState& target = system_install ? system_chrome_ : user_chrome_;
    target.CopyFrom(product_state);
  }
};

class MockInstallerState : public InstallerState {
 public:
  void set_level(Level level) {
    InstallerState::set_level(level);
  }

  void set_operation(Operation operation) { operation_ = operation; }

  void set_state_key(const std::wstring& state_key) {
    state_key_ = state_key;
  }
};

void AddChromeToInstallationState(bool system_level,
                                  base::Version* current_version,
                                  MockInstallationState* installation_state) {
  MockProductState product_state;
  product_state.set_version(new base::Version(*current_version));
  product_state.set_brand(L"TEST");
  product_state.set_eula_accepted(1);
  base::FilePath install_path = installer::GetChromeInstallPath(system_level);
  product_state.SetUninstallProgram(
      install_path.AppendASCII(current_version->GetString())
          .Append(installer::kInstallerDir)
          .Append(installer::kSetupExe));
  product_state.AddUninstallSwitch(installer::switches::kUninstall);
  if (system_level)
    product_state.AddUninstallSwitch(installer::switches::kSystemLevel);

  installation_state->SetProductState(system_level, product_state);
}

MockInstallationState* BuildChromeInstallationState(
    bool system_level,
    base::Version* current_version) {
  std::unique_ptr<MockInstallationState> installation_state(
      new MockInstallationState());
  AddChromeToInstallationState(system_level, current_version,
                               installation_state.get());
  return installation_state.release();
}

MockInstallerState* BuildBasicInstallerState(
    bool system_install,
    const InstallationState& machine_state,
    InstallerState::Operation operation) {
  std::unique_ptr<MockInstallerState> installer_state(new MockInstallerState());

  InstallerState::Level level = system_install ? InstallerState::SYSTEM_LEVEL
                                               : InstallerState::USER_LEVEL;
  installer_state->set_level(level);
  installer_state->set_operation(operation);
  // Hope this next one isn't checked for now.
  installer_state->set_state_key(L"PROBABLY_INVALID_REG_PATH");
  return installer_state.release();
}

void AddChromeToInstallerState(const InstallationState& machine_state,
                               MockInstallerState* installer_state) {
  // Fresh install or upgrade?
  const ProductState* chrome =
      machine_state.GetProductState(installer_state->system_install());
  if (chrome) {
    installer_state->set_target_path_for_testing(
        chrome->GetSetupPath().DirName().DirName().DirName());
  } else {
    installer_state->set_target_path_for_testing(
        installer::GetChromeInstallPath(installer_state->system_install()));
  }
}

MockInstallerState* BuildChromeInstallerState(
    bool system_install,
    const InstallationState& machine_state,
    InstallerState::Operation operation) {
  std::unique_ptr<MockInstallerState> installer_state(
      BuildBasicInstallerState(system_install, machine_state, operation));
  AddChromeToInstallerState(machine_state, installer_state.get());
  return installer_state.release();
}

}  // namespace

// The test fixture
//------------------------------------------------------------------------------

class InstallWorkerTest : public testing::Test {
 public:
  void SetUp() override {
    current_version_.reset(new base::Version("1.0.0.0"));
    new_version_.reset(new base::Version("42.0.0.0"));

    // Don't bother ensuring that these paths exist. Since we're just
    // building the work item lists and not running them, they shouldn't
    // actually be touched.
    archive_path_ =
        base::FilePath(L"C:\\UnlikelyPath\\Temp\\chrome_123\\chrome.7z");
    src_path_ = base::FilePath(
        L"C:\\UnlikelyPath\\Temp\\chrome_123\\source\\Chrome-bin");
    setup_path_ = base::FilePath(
        L"C:\\UnlikelyPath\\Temp\\CR_123.tmp\\setup.exe");
    temp_dir_ = base::FilePath(L"C:\\UnlikelyPath\\Temp\\chrome_123");
  }

 protected:
  std::unique_ptr<base::Version> current_version_;
  std::unique_ptr<base::Version> new_version_;
  base::FilePath archive_path_;
  base::FilePath setup_path_;
  base::FilePath src_path_;
  base::FilePath temp_dir_;
};

// Tests
//------------------------------------------------------------------------------

TEST_F(InstallWorkerTest, TestInstallChromeSystem) {
  const bool system_level = true;
  NiceMock<MockWorkItemList> work_item_list;

  const HKEY kRegRoot = system_level ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
  static const wchar_t kRegKeyPath[] = L"Software\\Chromium\\test";
  std::unique_ptr<CreateRegKeyWorkItem> create_reg_key_work_item(
      WorkItem::CreateCreateRegKeyWorkItem(kRegRoot, kRegKeyPath,
                                           WorkItem::kWow64Default));
  std::unique_ptr<SetRegValueWorkItem> set_reg_value_work_item(
      WorkItem::CreateSetRegValueWorkItem(
          kRegRoot, kRegKeyPath, WorkItem::kWow64Default, L"", L"", false));
  std::unique_ptr<DeleteTreeWorkItem> delete_tree_work_item(
      WorkItem::CreateDeleteTreeWorkItem(base::FilePath(), base::FilePath()));
  std::unique_ptr<DeleteRegKeyWorkItem> delete_reg_key_work_item(
      WorkItem::CreateDeleteRegKeyWorkItem(kRegRoot, kRegKeyPath,
                                           WorkItem::kWow64Default));

  std::unique_ptr<InstallationState> installation_state(
      BuildChromeInstallationState(system_level, current_version_.get()));

  std::unique_ptr<InstallerState> installer_state(
      BuildChromeInstallerState(system_level, *installation_state,
                                InstallerState::SINGLE_INSTALL_OR_UPDATE));

  // Set up some expectations.
  // TODO(robertshield): Set up some real expectations.
  EXPECT_CALL(work_item_list, AddCopyTreeWorkItem(_, _, _, _, _))
      .Times(AtLeast(1));
  EXPECT_CALL(work_item_list, AddCreateRegKeyWorkItem(_, _, _))
      .WillRepeatedly(Return(create_reg_key_work_item.get()));
  EXPECT_CALL(work_item_list, AddSetRegStringValueWorkItem(_, _, _, _, _, _))
      .WillRepeatedly(Return(set_reg_value_work_item.get()));
  EXPECT_CALL(work_item_list, AddDeleteTreeWorkItem(_, _))
      .WillRepeatedly(Return(delete_tree_work_item.get()));
  EXPECT_CALL(work_item_list, AddDeleteRegKeyWorkItem(_, _, _))
      .WillRepeatedly(Return(delete_reg_key_work_item.get()));

  AddInstallWorkItems(*installation_state.get(),
                      *installer_state.get(),
                      setup_path_,
                      archive_path_,
                      src_path_,
                      temp_dir_,
                      current_version_.get(),
                      *new_version_.get(),
                      &work_item_list);
}

// Tests for installer::AddUpdateBrandCodeWorkItem().
//------------------------------------------------------------------------------

// Parameters for AddUpdateBrandCodeWorkItem tests:
//   bool: is domain joined
//   bool: is registered with MDM
//   bool: is Windows 10 home edition
using AddUpdateBrandCodeWorkItemTestParams = std::tuple<bool, bool, bool>;

// These tests run at system level.
static const bool kSystemLevel = true;

class AddUpdateBrandCodeWorkItemTest
    : public ::testing::TestWithParam<AddUpdateBrandCodeWorkItemTestParams> {
 public:
  AddUpdateBrandCodeWorkItemTest()
      : is_domain_joined_(std::get<0>(GetParam())),
        is_registered_(std::get<1>(GetParam())),
        is_home_edition_(std::get<2>(GetParam())),
        scoped_install_details_(kSystemLevel),
        current_version_(new base::Version("1.0.0.0")),
        installation_state_(
            BuildChromeInstallationState(kSystemLevel, current_version_.get())),
        installer_state_(BuildChromeInstallerState(
            kSystemLevel,
            *installation_state_,
            InstallerState::SINGLE_INSTALL_OR_UPDATE)),
        scoped_domain_state_(is_domain_joined_),
        scoped_registration_state_(is_registered_),
        scoped_os_info_override_(
            is_home_edition_
                ? base::test::ScopedOSInfoOverride::Type::kWin10Home
                : base::test::ScopedOSInfoOverride::Type::kWin10Pro) {}

  void SetUp() override {
    // Override registry so that tests don't mess up the machine's state.
    ASSERT_NO_FATAL_FAILURE(registry_override_.OverrideRegistry(
        HKEY_LOCAL_MACHINE, &registry_override_hklm_path_));
  }

  void SetupExpectations(const base::string16& brand,
                         StrictMock<MockWorkItemList>* work_item_list) {
    if (!brand.empty()) {
      base::win::RegKey key(installer_state_->root_key(),
                            install_static::GetClientStateKeyPath().c_str(),
                            KEY_WRITE);
      ASSERT_TRUE(key.Valid());
      ASSERT_EQ(
          0, key.WriteValue(google_update::kRegRLZBrandField, brand.c_str()));
    }

    if (!installer::GetUpdatedBrandCode(brand).empty() &&
        (is_domain_joined_ || (is_registered_ && !is_home_edition_))) {
      EXPECT_CALL(*work_item_list,
                  AddSetRegStringValueWorkItem(_, _, _, _, _, _))
          .WillOnce(Return(nullptr));  // Return value ignored.
    }
  }

  const InstallerState* installer_state() { return installer_state_.get(); }

 private:
  const bool is_domain_joined_;
  const bool is_registered_;
  const bool is_home_edition_;

  install_static::ScopedInstallDetails scoped_install_details_;
  std::unique_ptr<base::Version> current_version_;
  std::unique_ptr<InstallationState> installation_state_;
  std::unique_ptr<InstallerState> installer_state_;
  registry_util::RegistryOverrideManager registry_override_;
  base::string16 registry_override_hklm_path_;
  base::win::ScopedDomainStateForTesting scoped_domain_state_;
  base::win::ScopedDeviceRegisteredWithManagementForTesting
      scoped_registration_state_;
  base::test::ScopedOSInfoOverride scoped_os_info_override_;
};

TEST_P(AddUpdateBrandCodeWorkItemTest, NoBrand) {
  StrictMock<MockWorkItemList> work_item_list;
  SetupExpectations(L"", &work_item_list);
  installer::AddUpdateBrandCodeWorkItem(*installer_state(), &work_item_list);
}

TEST_P(AddUpdateBrandCodeWorkItemTest, GGRV) {
  StrictMock<MockWorkItemList> work_item_list;
  SetupExpectations(L"GGRV", &work_item_list);
  installer::AddUpdateBrandCodeWorkItem(*installer_state(), &work_item_list);
}

TEST_P(AddUpdateBrandCodeWorkItemTest, GGLS) {
  StrictMock<MockWorkItemList> work_item_list;
  SetupExpectations(L"GGLS", &work_item_list);
  installer::AddUpdateBrandCodeWorkItem(*installer_state(), &work_item_list);
}

TEST_P(AddUpdateBrandCodeWorkItemTest, TEST) {
  StrictMock<MockWorkItemList> work_item_list;
  SetupExpectations(L"TEST", &work_item_list);
  installer::AddUpdateBrandCodeWorkItem(*installer_state(), &work_item_list);
}

struct AddUpdateBrandCodeWorkItemTestParamToString {
  std::string operator()(
      const TestParamInfo<AddUpdateBrandCodeWorkItemTestParams>& info) const {
    const char* joined = std::get<0>(info.param) ? "joined" : "notjoined";
    const char* registered =
        std::get<1>(info.param) ? "registered" : "notregistered";
    const char* home = std::get<2>(info.param) ? "home" : "nothome";
    return base::StringPrintf("%s_%s_%s", joined, registered, home);
  }
};

INSTANTIATE_TEST_SUITE_P(AddUpdateBrandCodeWorkItemTest,
                         AddUpdateBrandCodeWorkItemTest,
                         Combine(Bool(), Bool(), Bool()),
                         AddUpdateBrandCodeWorkItemTestParamToString());
