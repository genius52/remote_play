// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/performance_manager/graph/node_attached_data.h"

#include <utility>

#include "base/logging.h"
#include "chrome/browser/performance_manager/graph/graph_impl.h"
#include "chrome/browser/performance_manager/graph/node_base.h"

namespace performance_manager {

NodeAttachedData::NodeAttachedData() = default;
NodeAttachedData::~NodeAttachedData() = default;

// static
void NodeAttachedDataMapHelper::AttachInMap(
    const NodeBase* node,
    std::unique_ptr<NodeAttachedData> data) {
  DCHECK(node->graph()->NodeInGraph(node));
  GraphImpl::NodeAttachedDataKey data_key =
      std::make_pair(node, data->GetKey());
  auto& map = node->graph()->node_attached_data_map_;
  DCHECK(!base::ContainsKey(map, data_key));
  map[data_key] = std::move(data);
}

// static
NodeAttachedData* NodeAttachedDataMapHelper::GetFromMap(const NodeBase* node,
                                                        const void* key) {
  DCHECK(node->graph()->NodeInGraph(node));
  GraphImpl::NodeAttachedDataKey data_key = std::make_pair(node, key);
  auto& map = node->graph()->node_attached_data_map_;
  auto it = map.find(data_key);
  if (it == map.end())
    return nullptr;
  DCHECK_EQ(key, it->second->GetKey());
  return it->second.get();
}

// static
std::unique_ptr<NodeAttachedData> NodeAttachedDataMapHelper::DetachFromMap(
    const NodeBase* node,
    const void* key) {
  DCHECK(node->graph()->NodeInGraph(node));
  GraphImpl::NodeAttachedDataKey data_key = std::make_pair(node, key);
  auto& map = node->graph()->node_attached_data_map_;
  auto it = map.find(data_key);

  std::unique_ptr<NodeAttachedData> data;
  if (it != map.end()) {
    data = std::move(it->second);
    map.erase(it);
  }

  return data;
}

}  // namespace performance_manager