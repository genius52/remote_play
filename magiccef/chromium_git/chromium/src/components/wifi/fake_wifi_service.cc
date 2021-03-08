// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/wifi/fake_wifi_service.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/values.h"
#include "components/onc/onc_constants.h"

namespace wifi {

FakeWiFiService::FakeWiFiService() {
  // Populate data expected by unit test.
  {
    NetworkProperties network_properties;
    network_properties.connection_state = onc::connection_state::kConnected;
    network_properties.guid = "stub_wifi1_guid";
    network_properties.name = "wifi1";
    network_properties.type = onc::network_type::kWiFi;
    network_properties.frequency = 0;
    network_properties.ssid = "wifi1";
    network_properties.security = onc::wifi::kWEP_PSK;
    network_properties.signal_strength = 40;
    networks_.push_back(network_properties);
  }
  {
    NetworkProperties network_properties;
    network_properties.connection_state = onc::connection_state::kNotConnected;
    network_properties.guid = "stub_wifi2_guid";
    network_properties.name = "wifi2_PSK";
    network_properties.type = onc::network_type::kWiFi;
    network_properties.frequency = 5000;
    network_properties.frequency_set.insert(2400);
    network_properties.frequency_set.insert(5000);
    network_properties.ssid = "wifi2_PSK";
    network_properties.security = onc::wifi::kWPA_PSK;
    network_properties.signal_strength = 80;
    networks_.push_back(network_properties);
  }
}

FakeWiFiService::~FakeWiFiService() {
}

void FakeWiFiService::Initialize(
    scoped_refptr<base::SequencedTaskRunner> task_runner) {
}

void FakeWiFiService::UnInitialize() {
}

void FakeWiFiService::GetProperties(const std::string& network_guid,
                                    base::DictionaryValue* properties,
                                    std::string* error) {
  NetworkList::iterator network_properties = FindNetwork(network_guid);
  if (network_properties == networks_.end()) {
    *error = "Error.InvalidNetworkGuid";
    return;
  }
  properties->Swap(network_properties->ToValue(false).get());
}

void FakeWiFiService::GetManagedProperties(
    const std::string& network_guid,
    base::DictionaryValue* managed_properties,
    std::string* error) {
  // Not implemented
  *error = kErrorWiFiService;
}

void FakeWiFiService::GetState(const std::string& network_guid,
                               base::DictionaryValue* properties,
                               std::string* error) {
  NetworkList::iterator network_properties = FindNetwork(network_guid);
  if (network_properties == networks_.end()) {
    *error = "Error.InvalidNetworkGuid";
    return;
  }
  properties->Swap(network_properties->ToValue(true).get());
}

void FakeWiFiService::SetProperties(
    const std::string& network_guid,
    std::unique_ptr<base::DictionaryValue> properties,
    std::string* error) {
  NetworkList::iterator network_properties = FindNetwork(network_guid);
  if (network_properties == networks_.end() ||
      !network_properties->UpdateFromValue(*properties)) {
    *error = "Error.DBusFailed";
  }
}

void FakeWiFiService::CreateNetwork(
    bool shared,
    std::unique_ptr<base::DictionaryValue> properties,
    std::string* network_guid,
    std::string* error) {
  NetworkProperties network_properties;
  if (network_properties.UpdateFromValue(*properties)) {
    network_properties.guid = network_properties.ssid;
    networks_.push_back(network_properties);
    *network_guid = network_properties.guid;
  } else {
    *error = "Error.DBusFailed";
  }
}

void FakeWiFiService::GetVisibleNetworks(const std::string& network_type,
                                         base::ListValue* network_list,
                                         bool include_details) {
  for (NetworkList::const_iterator it = networks_.begin();
       it != networks_.end();
       ++it) {
    if (network_type.empty() || network_type == onc::network_type::kAllTypes ||
        it->type == network_type) {
      std::unique_ptr<base::DictionaryValue> network(
          it->ToValue(!include_details));
      network_list->Append(std::move(network));
    }
  }
}

void FakeWiFiService::RequestNetworkScan() {
  NotifyNetworkListChanged(networks_);
}

void FakeWiFiService::StartConnect(const std::string& network_guid,
                                   std::string* error) {
  NetworkList::iterator network_properties = FindNetwork(network_guid);
  if (network_properties == networks_.end()) {
    *error = "Error.InvalidNetworkGuid";
    return;
  }
  DisconnectAllNetworksOfType(network_properties->type);
  network_properties->connection_state = onc::connection_state::kConnected;
  SortNetworks();
  NotifyNetworkListChanged(networks_);
  NotifyNetworkChanged(network_guid);
}

void FakeWiFiService::StartDisconnect(const std::string& network_guid,
                                      std::string* error) {
  NetworkList::iterator network_properties = FindNetwork(network_guid);
  if (network_properties == networks_.end()) {
    *error = "Error.InvalidNetworkGuid";
    return;
  }
  network_properties->connection_state = onc::connection_state::kNotConnected;
  SortNetworks();
  NotifyNetworkListChanged(networks_);
  NotifyNetworkChanged(network_guid);
}

void FakeWiFiService::GetKeyFromSystem(const std::string& network_guid,
                                       std::string* key_data,
                                       std::string* error) {
  *error = "not-found";
}

void FakeWiFiService::SetEventObservers(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    const NetworkGuidListCallback& networks_changed_observer,
    const NetworkGuidListCallback& network_list_changed_observer) {
  task_runner_.swap(task_runner);
  networks_changed_observer_ = networks_changed_observer;
  network_list_changed_observer_ = network_list_changed_observer;
}

void FakeWiFiService::RequestConnectedNetworkUpdate() {
}

void FakeWiFiService::GetConnectedNetworkSSID(std::string* ssid,
                                              std::string* error) {
  *ssid = "";
  *error = "";
}

NetworkList::iterator FakeWiFiService::FindNetwork(
    const std::string& network_guid) {
  for (NetworkList::iterator it = networks_.begin(); it != networks_.end();
       ++it) {
    if (it->guid == network_guid)
      return it;
  }
  return networks_.end();
}

void FakeWiFiService::DisconnectAllNetworksOfType(const std::string& type) {
  for (NetworkList::iterator it = networks_.begin(); it != networks_.end();
       ++it) {
    if (it->type == type)
      it->connection_state = onc::connection_state::kNotConnected;
  }
}

void FakeWiFiService::SortNetworks() {
  // Sort networks, so connected/connecting is up front, then by type:
  // Ethernet, WiFi, Cellular, VPN
  networks_.sort(NetworkProperties::OrderByType);
}

void FakeWiFiService::NotifyNetworkListChanged(const NetworkList& networks) {
  WiFiService::NetworkGuidList current_networks;
  for (NetworkList::const_iterator it = networks.begin(); it != networks.end();
       ++it) {
    current_networks.push_back(it->guid);
  }

  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(network_list_changed_observer_, current_networks));
}

void FakeWiFiService::NotifyNetworkChanged(const std::string& network_guid) {
  WiFiService::NetworkGuidList changed_networks(1, network_guid);
  task_runner_->PostTask(
      FROM_HERE, base::BindOnce(networks_changed_observer_, changed_networks));
}

}  // namespace wifi
