// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/accessibility/ax_tree.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_serializable_tree.h"
#include "ui/accessibility/ax_tree_observer.h"
#include "ui/accessibility/ax_tree_serializer.h"
#include "ui/gfx/transform.h"

namespace ui {

namespace {

std::string IntVectorToString(const std::vector<int>& items) {
  std::string str;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0)
      str += ",";
    str += base::NumberToString(items[i]);
  }
  return str;
}

std::string GetBoundsAsString(const AXTree& tree, int32_t id) {
  AXNode* node = tree.GetFromId(id);
  gfx::RectF bounds = tree.GetTreeBounds(node);
  return base::StringPrintf("(%.0f, %.0f) size (%.0f x %.0f)", bounds.x(),
                            bounds.y(), bounds.width(), bounds.height());
}

std::string GetUnclippedBoundsAsString(const AXTree& tree, int32_t id) {
  AXNode* node = tree.GetFromId(id);
  gfx::RectF bounds = tree.GetTreeBounds(node, nullptr, false);
  return base::StringPrintf("(%.0f, %.0f) size (%.0f x %.0f)", bounds.x(),
                            bounds.y(), bounds.width(), bounds.height());
}

bool IsNodeOffscreen(const AXTree& tree, int32_t id) {
  AXNode* node = tree.GetFromId(id);
  bool result = false;
  tree.GetTreeBounds(node, &result);
  return result;
}

class TestAXTreeObserver : public AXTreeObserver {
 public:
  TestAXTreeObserver(AXTree* tree)
      : tree_(tree), tree_data_changed_(false), root_changed_(false) {
    tree_->AddObserver(this);
  }
  ~TestAXTreeObserver() final { tree_->RemoveObserver(this); }

  void OnNodeDataWillChange(AXTree* tree,
                            const AXNodeData& old_node_data,
                            const AXNodeData& new_node_data) override {}
  void OnTreeDataChanged(AXTree* tree,
                         const ui::AXTreeData& old_data,
                         const ui::AXTreeData& new_data) override {
    tree_data_changed_ = true;
  }
  void OnNodeWillBeDeleted(AXTree* tree, AXNode* node) override {
    deleted_ids_.push_back(node->id());
  }

  void OnSubtreeWillBeDeleted(AXTree* tree, AXNode* node) override {
    subtree_deleted_ids_.push_back(node->id());
  }

  void OnNodeWillBeReparented(AXTree* tree, AXNode* node) override {
    node_will_be_reparented_ids_.push_back(node->id());
  }

  void OnSubtreeWillBeReparented(AXTree* tree, AXNode* node) override {
    subtree_will_be_reparented_ids_.push_back(node->id());
  }

  void OnNodeCreated(AXTree* tree, AXNode* node) override {
    created_ids_.push_back(node->id());
  }

  void OnNodeReparented(AXTree* tree, AXNode* node) override {
    node_reparented_ids_.push_back(node->id());
  }

  void OnNodeChanged(AXTree* tree, AXNode* node) override {
    changed_ids_.push_back(node->id());
    if (call_posinset_and_setsize)
      AssertPosinsetAndSetsizeZero(node);
  }

  void OnAtomicUpdateFinished(AXTree* tree,
                              bool root_changed,
                              const std::vector<Change>& changes) override {
    root_changed_ = root_changed;

    for (size_t i = 0; i < changes.size(); ++i) {
      int id = changes[i].node->id();
      switch (changes[i].type) {
        case NODE_CREATED:
          node_creation_finished_ids_.push_back(id);
          break;
        case SUBTREE_CREATED:
          subtree_creation_finished_ids_.push_back(id);
          break;
        case NODE_REPARENTED:
          node_reparented_finished_ids_.push_back(id);
          break;
        case SUBTREE_REPARENTED:
          subtree_reparented_finished_ids_.push_back(id);
          break;
        case NODE_CHANGED:
          change_finished_ids_.push_back(id);
          break;
      }
    }
  }

  void OnRoleChanged(AXTree* tree,
                     AXNode* node,
                     ax::mojom::Role old_role,
                     ax::mojom::Role new_role) override {
    attribute_change_log_.push_back(base::StringPrintf(
        "Role changed from %s to %s", ToString(old_role), ToString(new_role)));
  }

  void OnStateChanged(AXTree* tree,
                      AXNode* node,
                      ax::mojom::State state,
                      bool new_value) override {
    attribute_change_log_.push_back(base::StringPrintf(
        "%s changed to %s", ToString(state), new_value ? "true" : "false"));
  }

  void OnStringAttributeChanged(AXTree* tree,
                                AXNode* node,
                                ax::mojom::StringAttribute attr,
                                const std::string& old_value,
                                const std::string& new_value) override {
    attribute_change_log_.push_back(
        base::StringPrintf("%s changed from %s to %s", ToString(attr),
                           old_value.c_str(), new_value.c_str()));
  }

  void OnIntAttributeChanged(AXTree* tree,
                             AXNode* node,
                             ax::mojom::IntAttribute attr,
                             int32_t old_value,
                             int32_t new_value) override {
    attribute_change_log_.push_back(base::StringPrintf(
        "%s changed from %d to %d", ToString(attr), old_value, new_value));
  }

  void OnFloatAttributeChanged(AXTree* tree,
                               AXNode* node,
                               ax::mojom::FloatAttribute attr,
                               float old_value,
                               float new_value) override {
    attribute_change_log_.push_back(
        base::StringPrintf("%s changed from %s to %s", ToString(attr),
                           base::NumberToString(old_value).c_str(),
                           base::NumberToString(new_value).c_str()));
  }

  void OnBoolAttributeChanged(AXTree* tree,
                              AXNode* node,
                              ax::mojom::BoolAttribute attr,
                              bool new_value) override {
    attribute_change_log_.push_back(base::StringPrintf(
        "%s changed to %s", ToString(attr), new_value ? "true" : "false"));
  }

  void OnIntListAttributeChanged(
      AXTree* tree,
      AXNode* node,
      ax::mojom::IntListAttribute attr,
      const std::vector<int32_t>& old_value,
      const std::vector<int32_t>& new_value) override {
    attribute_change_log_.push_back(
        base::StringPrintf("%s changed from %s to %s", ToString(attr),
                           IntVectorToString(old_value).c_str(),
                           IntVectorToString(new_value).c_str()));
  }

  bool tree_data_changed() const { return tree_data_changed_; }
  bool root_changed() const { return root_changed_; }
  const std::vector<int32_t>& deleted_ids() { return deleted_ids_; }
  const std::vector<int32_t>& subtree_deleted_ids() {
    return subtree_deleted_ids_;
  }
  const std::vector<int32_t>& created_ids() { return created_ids_; }
  const std::vector<int32_t>& node_creation_finished_ids() {
    return node_creation_finished_ids_;
  }
  const std::vector<int32_t>& subtree_creation_finished_ids() {
    return subtree_creation_finished_ids_;
  }
  const std::vector<int32_t>& node_reparented_finished_ids() {
    return node_reparented_finished_ids_;
  }
  const std::vector<int32_t>& subtree_will_be_reparented_ids() {
    return subtree_will_be_reparented_ids_;
  }
  const std::vector<int32_t>& node_will_be_reparented_ids() {
    return node_will_be_reparented_ids_;
  }
  const std::vector<int32_t>& node_reparented_ids() {
    return node_reparented_ids_;
  }
  const std::vector<int32_t>& subtree_reparented_finished_ids() {
    return subtree_reparented_finished_ids_;
  }
  const std::vector<int32_t>& change_finished_ids() {
    return change_finished_ids_;
  }
  const std::vector<std::string>& attribute_change_log() {
    return attribute_change_log_;
  }

  bool call_posinset_and_setsize = false;
  void AssertPosinsetAndSetsizeZero(AXNode* node) {
    ASSERT_EQ(0, node->GetPosInSet());
    ASSERT_EQ(0, node->GetSetSize());
  }

 private:
  AXTree* tree_;
  bool tree_data_changed_;
  bool root_changed_;
  std::vector<int32_t> deleted_ids_;
  std::vector<int32_t> subtree_deleted_ids_;
  std::vector<int32_t> created_ids_;
  std::vector<int32_t> changed_ids_;
  std::vector<int32_t> subtree_will_be_reparented_ids_;
  std::vector<int32_t> node_will_be_reparented_ids_;
  std::vector<int32_t> node_creation_finished_ids_;
  std::vector<int32_t> subtree_creation_finished_ids_;
  std::vector<int32_t> node_reparented_ids_;
  std::vector<int32_t> node_reparented_finished_ids_;
  std::vector<int32_t> subtree_reparented_finished_ids_;
  std::vector<int32_t> change_finished_ids_;
  std::vector<std::string> attribute_change_log_;
};

}  // namespace

TEST(AXTreeTest, SerializeSimpleAXTree) {
  AXNodeData root;
  root.id = 1;
  root.role = ax::mojom::Role::kDialog;
  root.AddState(ax::mojom::State::kFocusable);
  root.relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);
  root.child_ids.push_back(2);
  root.child_ids.push_back(3);

  AXNodeData button;
  button.id = 2;
  button.role = ax::mojom::Role::kButton;
  button.relative_bounds.bounds = gfx::RectF(20, 20, 200, 30);

  AXNodeData checkbox;
  checkbox.id = 3;
  checkbox.role = ax::mojom::Role::kCheckBox;
  checkbox.relative_bounds.bounds = gfx::RectF(20, 50, 200, 30);

  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.push_back(root);
  initial_state.nodes.push_back(button);
  initial_state.nodes.push_back(checkbox);
  initial_state.has_tree_data = true;
  initial_state.tree_data.title = "Title";
  AXSerializableTree src_tree(initial_state);

  std::unique_ptr<AXTreeSource<const AXNode*, AXNodeData, AXTreeData>>
      tree_source(src_tree.CreateTreeSource());
  AXTreeSerializer<const AXNode*, AXNodeData, AXTreeData> serializer(
      tree_source.get());
  AXTreeUpdate update;
  serializer.SerializeChanges(src_tree.root(), &update);

  AXTree dst_tree;
  ASSERT_TRUE(dst_tree.Unserialize(update));

  const AXNode* root_node = dst_tree.root();
  ASSERT_TRUE(root_node != nullptr);
  EXPECT_EQ(root.id, root_node->id());
  EXPECT_EQ(root.role, root_node->data().role);

  ASSERT_EQ(2u, root_node->children().size());

  const AXNode* button_node = root_node->children()[0];
  EXPECT_EQ(button.id, button_node->id());
  EXPECT_EQ(button.role, button_node->data().role);

  const AXNode* checkbox_node = root_node->children()[1];
  EXPECT_EQ(checkbox.id, checkbox_node->id());
  EXPECT_EQ(checkbox.role, checkbox_node->data().role);

  EXPECT_EQ(
      "AXTree title=Title\n"
      "id=1 dialog FOCUSABLE (0, 0)-(800, 600) actions= child_ids=2,3\n"
      "  id=2 button (20, 20)-(200, 30) actions=\n"
      "  id=3 checkBox (20, 50)-(200, 30) actions=\n",
      dst_tree.ToString());
}

TEST(AXTreeTest, SerializeAXTreeUpdate) {
  AXNodeData list;
  list.id = 3;
  list.role = ax::mojom::Role::kList;
  list.child_ids.push_back(4);
  list.child_ids.push_back(5);
  list.child_ids.push_back(6);

  AXNodeData list_item_2;
  list_item_2.id = 5;
  list_item_2.role = ax::mojom::Role::kListItem;

  AXNodeData list_item_3;
  list_item_3.id = 6;
  list_item_3.role = ax::mojom::Role::kListItem;

  AXNodeData button;
  button.id = 7;
  button.role = ax::mojom::Role::kButton;

  AXTreeUpdate update;
  update.root_id = 3;
  update.nodes.push_back(list);
  update.nodes.push_back(list_item_2);
  update.nodes.push_back(list_item_3);
  update.nodes.push_back(button);

  EXPECT_EQ(
      "AXTreeUpdate: root id 3\n"
      "id=3 list (0, 0)-(0, 0) actions= child_ids=4,5,6\n"
      "  id=5 listItem (0, 0)-(0, 0) actions=\n"
      "  id=6 listItem (0, 0)-(0, 0) actions=\n"
      "id=7 button (0, 0)-(0, 0) actions=\n",
      update.ToString());
}

TEST(AXTreeTest, LeaveOrphanedDeletedSubtreeFails) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(3);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[0].child_ids.push_back(3);
  initial_state.nodes[1].id = 2;
  initial_state.nodes[2].id = 3;
  AXTree tree(initial_state);

  // This should fail because we delete a subtree rooted at id=2
  // but never update it.
  AXTreeUpdate update;
  update.node_id_to_clear = 2;
  update.nodes.resize(1);
  update.nodes[0].id = 3;
  EXPECT_FALSE(tree.Unserialize(update));
  ASSERT_EQ("Nodes left pending by the update: 2", tree.error());
}

TEST(AXTreeTest, LeaveOrphanedNewChildFails) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(1);
  initial_state.nodes[0].id = 1;
  AXTree tree(initial_state);

  // This should fail because we add a new child to the root node
  // but never update it.
  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0].id = 1;
  update.nodes[0].child_ids.push_back(2);
  EXPECT_FALSE(tree.Unserialize(update));
  ASSERT_EQ("Nodes left pending by the update: 2", tree.error());
}

TEST(AXTreeTest, DuplicateChildIdFails) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(1);
  initial_state.nodes[0].id = 1;
  AXTree tree(initial_state);

  // This should fail because a child id appears twice.
  AXTreeUpdate update;
  update.nodes.resize(2);
  update.nodes[0].id = 1;
  update.nodes[0].child_ids.push_back(2);
  update.nodes[0].child_ids.push_back(2);
  update.nodes[1].id = 2;
  EXPECT_FALSE(tree.Unserialize(update));
  ASSERT_EQ("Node 1 has duplicate child id 2", tree.error());
}

TEST(AXTreeTest, InvalidReparentingFails) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(3);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[1].id = 2;
  initial_state.nodes[1].child_ids.push_back(3);
  initial_state.nodes[2].id = 3;

  AXTree tree(initial_state);

  // This should fail because node 3 is reparented from node 2 to node 1
  // without deleting node 1's subtree first.
  AXTreeUpdate update;
  update.nodes.resize(3);
  update.nodes[0].id = 1;
  update.nodes[0].child_ids.push_back(3);
  update.nodes[0].child_ids.push_back(2);
  update.nodes[1].id = 2;
  update.nodes[2].id = 3;
  EXPECT_FALSE(tree.Unserialize(update));
  ASSERT_EQ("Node 3 reparented from 2 to 1", tree.error());
}

TEST(AXTreeTest, NoReparentingOfRootIfNoNewRoot) {
  AXNodeData root;
  root.id = 1;
  AXNodeData child1;
  child1.id = 2;
  AXNodeData child2;
  child2.id = 3;

  root.child_ids = {child1.id};
  child1.child_ids = {child2.id};

  AXTreeUpdate initial_state;
  initial_state.root_id = root.id;
  initial_state.nodes = {root, child1, child2};

  AXTree tree(initial_state);

  // Update the root but don't change it by reparenting |child2| to be a child
  // of the root.
  root.child_ids = {child1.id, child2.id};
  child1.child_ids = {};

  AXTreeUpdate update;
  update.root_id = root.id;
  update.node_id_to_clear = root.id;
  update.nodes = {root, child1, child2};

  TestAXTreeObserver test_observer(&tree);
  ASSERT_TRUE(tree.Unserialize(update));

  EXPECT_EQ(0U, test_observer.deleted_ids().size());
  EXPECT_EQ(0U, test_observer.subtree_deleted_ids().size());
  EXPECT_EQ(0U, test_observer.created_ids().size());

  EXPECT_EQ(0U, test_observer.node_creation_finished_ids().size());
  EXPECT_EQ(0U, test_observer.subtree_creation_finished_ids().size());
  EXPECT_EQ(0U, test_observer.node_reparented_finished_ids().size());

  ASSERT_EQ(2U, test_observer.subtree_reparented_finished_ids().size());
  EXPECT_EQ(child1.id, test_observer.subtree_reparented_finished_ids()[0]);
  EXPECT_EQ(child2.id, test_observer.subtree_reparented_finished_ids()[1]);

  ASSERT_EQ(1U, test_observer.change_finished_ids().size());
  EXPECT_EQ(root.id, test_observer.change_finished_ids()[0]);

  EXPECT_FALSE(test_observer.root_changed());
  EXPECT_FALSE(test_observer.tree_data_changed());
}

TEST(AXTreeTest, NoReparentingIfOnlyRemovedAndChangedNotReAdded) {
  AXNodeData root;
  root.id = 1;
  AXNodeData child1;
  child1.id = 2;
  AXNodeData child2;
  child2.id = 3;

  root.child_ids = {child1.id};
  child1.child_ids = {child2.id};

  AXTreeUpdate initial_state;
  initial_state.root_id = root.id;
  initial_state.nodes = {root, child1, child2};

  AXTree tree(initial_state);

  // Change existing attributes.
  AXTreeUpdate update;
  update.nodes.resize(2);
  update.nodes[0].id = 2;
  update.nodes[0].AddIntAttribute(ax::mojom::IntAttribute::kActivedescendantId,
                                  3);
  update.nodes[1].id = 1;

  TestAXTreeObserver test_observer(&tree);
  EXPECT_TRUE(tree.Unserialize(update)) << tree.error();

  EXPECT_EQ(2U, test_observer.deleted_ids().size());
  EXPECT_EQ(2U, test_observer.subtree_deleted_ids().size());
  EXPECT_EQ(0U, test_observer.created_ids().size());

  EXPECT_EQ(0U, test_observer.node_creation_finished_ids().size());
  EXPECT_EQ(0U, test_observer.subtree_creation_finished_ids().size());
  EXPECT_EQ(0U, test_observer.node_will_be_reparented_ids().size());
  EXPECT_EQ(0U, test_observer.subtree_will_be_reparented_ids().size());
  EXPECT_EQ(0U, test_observer.node_reparented_ids().size());
  EXPECT_EQ(0U, test_observer.node_reparented_finished_ids().size());
  ASSERT_EQ(0U, test_observer.subtree_reparented_finished_ids().size());

  EXPECT_FALSE(test_observer.root_changed());
  EXPECT_FALSE(test_observer.tree_data_changed());
}

TEST(AXTreeTest, ReparentRootIfRootChanged) {
  AXNodeData root;
  root.id = 1;
  AXNodeData child1;
  child1.id = 2;
  AXNodeData child2;
  child2.id = 3;

  root.child_ids = {child1.id};
  child1.child_ids = {child2.id};

  AXTreeUpdate initial_state;
  initial_state.root_id = root.id;
  initial_state.nodes = {root, child1, child2};

  AXTree tree(initial_state);

  // Create a new root and reparent |child2| to be a child of the new root.
  AXNodeData root2;
  root2.id = 4;
  root2.child_ids = {child1.id, child2.id};
  child1.child_ids = {};

  AXTreeUpdate update;
  update.root_id = root2.id;
  update.node_id_to_clear = root.id;
  update.nodes = {root2, child1, child2};

  TestAXTreeObserver test_observer(&tree);
  ASSERT_TRUE(tree.Unserialize(update));

  ASSERT_EQ(1U, test_observer.deleted_ids().size());
  EXPECT_EQ(root.id, test_observer.deleted_ids()[0]);

  ASSERT_EQ(1U, test_observer.subtree_deleted_ids().size());
  EXPECT_EQ(root.id, test_observer.subtree_deleted_ids()[0]);

  ASSERT_EQ(1U, test_observer.created_ids().size());
  EXPECT_EQ(root2.id, test_observer.created_ids()[0]);

  EXPECT_EQ(0U, test_observer.node_creation_finished_ids().size());

  ASSERT_EQ(1U, test_observer.subtree_creation_finished_ids().size());
  EXPECT_EQ(root2.id, test_observer.subtree_creation_finished_ids()[0]);

  ASSERT_EQ(2U, test_observer.node_reparented_finished_ids().size());
  EXPECT_EQ(child1.id, test_observer.node_reparented_finished_ids()[0]);
  EXPECT_EQ(child2.id, test_observer.node_reparented_finished_ids()[1]);

  EXPECT_EQ(0U, test_observer.subtree_reparented_finished_ids().size());

  EXPECT_EQ(0U, test_observer.change_finished_ids().size());

  EXPECT_TRUE(test_observer.root_changed());
  EXPECT_FALSE(test_observer.tree_data_changed());
}

TEST(AXTreeTest, TreeObserverIsCalled) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(2);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[1].id = 2;

  AXTree tree(initial_state);
  AXTreeUpdate update;
  update.root_id = 3;
  update.node_id_to_clear = 1;
  update.nodes.resize(2);
  update.nodes[0].id = 3;
  update.nodes[0].child_ids.push_back(4);
  update.nodes[1].id = 4;

  TestAXTreeObserver test_observer(&tree);
  ASSERT_TRUE(tree.Unserialize(update));

  ASSERT_EQ(2U, test_observer.deleted_ids().size());
  EXPECT_EQ(1, test_observer.deleted_ids()[0]);
  EXPECT_EQ(2, test_observer.deleted_ids()[1]);

  ASSERT_EQ(1U, test_observer.subtree_deleted_ids().size());
  EXPECT_EQ(1, test_observer.subtree_deleted_ids()[0]);

  ASSERT_EQ(2U, test_observer.created_ids().size());
  EXPECT_EQ(3, test_observer.created_ids()[0]);
  EXPECT_EQ(4, test_observer.created_ids()[1]);

  ASSERT_EQ(1U, test_observer.subtree_creation_finished_ids().size());
  EXPECT_EQ(3, test_observer.subtree_creation_finished_ids()[0]);

  ASSERT_EQ(1U, test_observer.node_creation_finished_ids().size());
  EXPECT_EQ(4, test_observer.node_creation_finished_ids()[0]);

  ASSERT_TRUE(test_observer.root_changed());
}

TEST(AXTreeTest, TreeObserverIsCalledForTreeDataChanges) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(1);
  initial_state.nodes[0].id = 1;
  initial_state.has_tree_data = true;
  initial_state.tree_data.title = "Initial";
  AXTree tree(initial_state);

  TestAXTreeObserver test_observer(&tree);

  // An empty update shouldn't change tree data.
  AXTreeUpdate empty_update;
  EXPECT_TRUE(tree.Unserialize(empty_update));
  EXPECT_FALSE(test_observer.tree_data_changed());
  EXPECT_EQ("Initial", tree.data().title);

  // An update with tree data shouldn't change tree data if
  // |has_tree_data| isn't set.
  AXTreeUpdate ignored_tree_data_update;
  ignored_tree_data_update.tree_data.title = "Ignore Me";
  EXPECT_TRUE(tree.Unserialize(ignored_tree_data_update));
  EXPECT_FALSE(test_observer.tree_data_changed());
  EXPECT_EQ("Initial", tree.data().title);

  // An update with |has_tree_data| set should update the tree data.
  AXTreeUpdate tree_data_update;
  tree_data_update.has_tree_data = true;
  tree_data_update.tree_data.title = "New Title";
  EXPECT_TRUE(tree.Unserialize(tree_data_update));
  EXPECT_TRUE(test_observer.tree_data_changed());
  EXPECT_EQ("New Title", tree.data().title);
}

TEST(AXTreeTest, ReparentingDoesNotTriggerNodeCreated) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(3);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[1].id = 2;
  initial_state.nodes[1].child_ids.push_back(3);
  initial_state.nodes[2].id = 3;

  AXTree tree(initial_state);
  TestAXTreeObserver test_observer(&tree);

  AXTreeUpdate update;
  update.nodes.resize(2);
  update.node_id_to_clear = 2;
  update.root_id = 1;
  update.nodes[0].id = 1;
  update.nodes[0].child_ids.push_back(3);
  update.nodes[1].id = 3;
  EXPECT_TRUE(tree.Unserialize(update)) << tree.error();
  std::vector<int> created = test_observer.node_creation_finished_ids();
  std::vector<int> subtree_reparented =
      test_observer.subtree_reparented_finished_ids();
  std::vector<int> node_reparented =
      test_observer.node_reparented_finished_ids();
  ASSERT_FALSE(base::ContainsValue(created, 3));
  ASSERT_TRUE(base::ContainsValue(subtree_reparented, 3));
  ASSERT_FALSE(base::ContainsValue(node_reparented, 3));
}

TEST(AXTreeTest, TreeObserverIsNotCalledForReparenting) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(2);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[1].id = 2;

  AXTree tree(initial_state);
  AXTreeUpdate update;
  update.node_id_to_clear = 1;
  update.root_id = 2;
  update.nodes.resize(2);
  update.nodes[0].id = 2;
  update.nodes[0].child_ids.push_back(4);
  update.nodes[1].id = 4;

  TestAXTreeObserver test_observer(&tree);

  EXPECT_TRUE(tree.Unserialize(update));

  ASSERT_EQ(1U, test_observer.deleted_ids().size());
  EXPECT_EQ(1, test_observer.deleted_ids()[0]);

  ASSERT_EQ(1U, test_observer.subtree_deleted_ids().size());
  EXPECT_EQ(1, test_observer.subtree_deleted_ids()[0]);

  ASSERT_EQ(1U, test_observer.created_ids().size());
  EXPECT_EQ(4, test_observer.created_ids()[0]);

  ASSERT_EQ(1U, test_observer.subtree_creation_finished_ids().size());
  EXPECT_EQ(4, test_observer.subtree_creation_finished_ids()[0]);

  ASSERT_EQ(1U, test_observer.subtree_reparented_finished_ids().size());
  EXPECT_EQ(2, test_observer.subtree_reparented_finished_ids()[0]);

  EXPECT_EQ(0U, test_observer.node_creation_finished_ids().size());
  EXPECT_EQ(0U, test_observer.node_reparented_finished_ids().size());

  ASSERT_TRUE(test_observer.root_changed());
}

// UAF caught by ax_tree_fuzzer
TEST(AXTreeTest, BogusAXTree) {
  AXTreeUpdate initial_state;
  AXNodeData node;
  node.id = 0;
  initial_state.nodes.push_back(node);
  initial_state.nodes.push_back(node);
  ui::AXTree tree;
  tree.Unserialize(initial_state);
}

// UAF caught by ax_tree_fuzzer
TEST(AXTreeTest, BogusAXTree2) {
  AXTreeUpdate initial_state;
  AXNodeData node;
  node.id = 0;
  initial_state.nodes.push_back(node);
  AXNodeData node2;
  node2.id = 0;
  node2.child_ids.push_back(0);
  node2.child_ids.push_back(0);
  initial_state.nodes.push_back(node2);
  ui::AXTree tree;
  tree.Unserialize(initial_state);
}

// UAF caught by ax_tree_fuzzer
TEST(AXTreeTest, BogusAXTree3) {
  AXTreeUpdate initial_state;
  AXNodeData node;
  node.id = 0;
  node.child_ids.push_back(1);
  initial_state.nodes.push_back(node);

  AXNodeData node2;
  node2.id = 1;
  node2.child_ids.push_back(1);
  node2.child_ids.push_back(1);
  initial_state.nodes.push_back(node2);

  ui::AXTree tree;
  tree.Unserialize(initial_state);
}

TEST(AXTreeTest, RoleAndStateChangeCallbacks) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(1);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].role = ax::mojom::Role::kButton;
  initial_state.nodes[0].SetCheckedState(ax::mojom::CheckedState::kTrue);
  initial_state.nodes[0].AddState(ax::mojom::State::kFocusable);
  AXTree tree(initial_state);

  TestAXTreeObserver test_observer(&tree);

  // Change the role and state.
  AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(1);
  update.nodes[0].id = 1;
  update.nodes[0].role = ax::mojom::Role::kCheckBox;
  update.nodes[0].SetCheckedState(ax::mojom::CheckedState::kFalse);
  update.nodes[0].AddState(ax::mojom::State::kFocusable);
  update.nodes[0].AddState(ax::mojom::State::kVisited);
  EXPECT_TRUE(tree.Unserialize(update));

  const std::vector<std::string>& change_log =
      test_observer.attribute_change_log();
  ASSERT_EQ(3U, change_log.size());
  EXPECT_EQ("Role changed from button to checkBox", change_log[0]);
  EXPECT_EQ("visited changed to true", change_log[1]);
  EXPECT_EQ("checkedState changed from 2 to 1", change_log[2]);
}

TEST(AXTreeTest, AttributeChangeCallbacks) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(1);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].AddStringAttribute(ax::mojom::StringAttribute::kName,
                                            "N1");
  initial_state.nodes[0].AddStringAttribute(
      ax::mojom::StringAttribute::kDescription, "D1");
  initial_state.nodes[0].AddBoolAttribute(ax::mojom::BoolAttribute::kLiveAtomic,
                                          true);
  initial_state.nodes[0].AddBoolAttribute(ax::mojom::BoolAttribute::kBusy,
                                          false);
  initial_state.nodes[0].AddFloatAttribute(
      ax::mojom::FloatAttribute::kMinValueForRange, 1.0);
  initial_state.nodes[0].AddFloatAttribute(
      ax::mojom::FloatAttribute::kMaxValueForRange, 10.0);
  initial_state.nodes[0].AddFloatAttribute(
      ax::mojom::FloatAttribute::kStepValueForRange, 3.0);
  initial_state.nodes[0].AddIntAttribute(ax::mojom::IntAttribute::kScrollX, 5);
  initial_state.nodes[0].AddIntAttribute(ax::mojom::IntAttribute::kScrollXMin,
                                         1);
  AXTree tree(initial_state);

  TestAXTreeObserver test_observer(&tree);

  // Change existing attributes.
  AXTreeUpdate update0;
  update0.root_id = 1;
  update0.nodes.resize(1);
  update0.nodes[0].id = 1;
  update0.nodes[0].AddStringAttribute(ax::mojom::StringAttribute::kName, "N2");
  update0.nodes[0].AddStringAttribute(ax::mojom::StringAttribute::kDescription,
                                      "D2");
  update0.nodes[0].AddBoolAttribute(ax::mojom::BoolAttribute::kLiveAtomic,
                                    false);
  update0.nodes[0].AddBoolAttribute(ax::mojom::BoolAttribute::kBusy, true);
  update0.nodes[0].AddFloatAttribute(
      ax::mojom::FloatAttribute::kMinValueForRange, 2.0);
  update0.nodes[0].AddFloatAttribute(
      ax::mojom::FloatAttribute::kMaxValueForRange, 9.0);
  update0.nodes[0].AddFloatAttribute(
      ax::mojom::FloatAttribute::kStepValueForRange, 0.5);
  update0.nodes[0].AddIntAttribute(ax::mojom::IntAttribute::kScrollX, 6);
  update0.nodes[0].AddIntAttribute(ax::mojom::IntAttribute::kScrollXMin, 2);
  EXPECT_TRUE(tree.Unserialize(update0));

  const std::vector<std::string>& change_log =
      test_observer.attribute_change_log();
  ASSERT_EQ(9U, change_log.size());
  EXPECT_EQ("name changed from N1 to N2", change_log[0]);
  EXPECT_EQ("description changed from D1 to D2", change_log[1]);
  EXPECT_EQ("liveAtomic changed to false", change_log[2]);
  EXPECT_EQ("busy changed to true", change_log[3]);
  EXPECT_EQ("minValueForRange changed from 1 to 2", change_log[4]);
  EXPECT_EQ("maxValueForRange changed from 10 to 9", change_log[5]);
  EXPECT_EQ("stepValueForRange changed from 3 to .5", change_log[6]);
  EXPECT_EQ("scrollX changed from 5 to 6", change_log[7]);
  EXPECT_EQ("scrollXMin changed from 1 to 2", change_log[8]);

  TestAXTreeObserver test_observer2(&tree);

  // Add and remove attributes.
  AXTreeUpdate update1;
  update1.root_id = 1;
  update1.nodes.resize(1);
  update1.nodes[0].id = 1;
  update1.nodes[0].AddStringAttribute(ax::mojom::StringAttribute::kDescription,
                                      "D3");
  update1.nodes[0].AddStringAttribute(ax::mojom::StringAttribute::kValue, "V3");
  update1.nodes[0].AddBoolAttribute(ax::mojom::BoolAttribute::kModal, true);
  update1.nodes[0].AddFloatAttribute(ax::mojom::FloatAttribute::kValueForRange,
                                     5.0);
  update1.nodes[0].AddFloatAttribute(
      ax::mojom::FloatAttribute::kMaxValueForRange, 9.0);
  update1.nodes[0].AddIntAttribute(ax::mojom::IntAttribute::kScrollX, 7);
  update1.nodes[0].AddIntAttribute(ax::mojom::IntAttribute::kScrollXMax, 10);
  EXPECT_TRUE(tree.Unserialize(update1));

  const std::vector<std::string>& change_log2 =
      test_observer2.attribute_change_log();
  ASSERT_EQ(11U, change_log2.size());
  EXPECT_EQ("name changed from N2 to ", change_log2[0]);
  EXPECT_EQ("description changed from D2 to D3", change_log2[1]);
  EXPECT_EQ("value changed from  to V3", change_log2[2]);
  EXPECT_EQ("busy changed to false", change_log2[3]);
  EXPECT_EQ("modal changed to true", change_log2[4]);
  EXPECT_EQ("minValueForRange changed from 2 to 0", change_log2[5]);
  EXPECT_EQ("stepValueForRange changed from 3 to .5", change_log[6]);
  EXPECT_EQ("valueForRange changed from 0 to 5", change_log2[7]);
  EXPECT_EQ("scrollXMin changed from 2 to 0", change_log2[8]);
  EXPECT_EQ("scrollX changed from 6 to 7", change_log2[9]);
  EXPECT_EQ("scrollXMax changed from 0 to 10", change_log2[10]);
}

TEST(AXTreeTest, IntListChangeCallbacks) {
  std::vector<int32_t> one;
  one.push_back(1);

  std::vector<int32_t> two;
  two.push_back(2);
  two.push_back(2);

  std::vector<int32_t> three;
  three.push_back(3);

  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(1);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].AddIntListAttribute(
      ax::mojom::IntListAttribute::kControlsIds, one);
  initial_state.nodes[0].AddIntListAttribute(
      ax::mojom::IntListAttribute::kRadioGroupIds, two);
  AXTree tree(initial_state);

  TestAXTreeObserver test_observer(&tree);

  // Change existing attributes.
  AXTreeUpdate update0;
  update0.root_id = 1;
  update0.nodes.resize(1);
  update0.nodes[0].id = 1;
  update0.nodes[0].AddIntListAttribute(
      ax::mojom::IntListAttribute::kControlsIds, two);
  update0.nodes[0].AddIntListAttribute(
      ax::mojom::IntListAttribute::kRadioGroupIds, three);
  EXPECT_TRUE(tree.Unserialize(update0));

  const std::vector<std::string>& change_log =
      test_observer.attribute_change_log();
  ASSERT_EQ(2U, change_log.size());
  EXPECT_EQ("controlsIds changed from 1 to 2,2", change_log[0]);
  EXPECT_EQ("radioGroupIds changed from 2,2 to 3", change_log[1]);

  TestAXTreeObserver test_observer2(&tree);

  // Add and remove attributes.
  AXTreeUpdate update1;
  update1.root_id = 1;
  update1.nodes.resize(1);
  update1.nodes[0].id = 1;
  update1.nodes[0].AddIntListAttribute(
      ax::mojom::IntListAttribute::kRadioGroupIds, two);
  update1.nodes[0].AddIntListAttribute(ax::mojom::IntListAttribute::kFlowtoIds,
                                       three);
  EXPECT_TRUE(tree.Unserialize(update1));

  const std::vector<std::string>& change_log2 =
      test_observer2.attribute_change_log();
  ASSERT_EQ(3U, change_log2.size());
  EXPECT_EQ("controlsIds changed from 2,2 to ", change_log2[0]);
  EXPECT_EQ("radioGroupIds changed from 3 to 2,2", change_log2[1]);
  EXPECT_EQ("flowtoIds changed from  to 3", change_log2[2]);
}

// Create a very simple tree and make sure that we can get the bounds of
// any node.
TEST(AXTreeTest, GetBoundsBasic) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(2);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);
  tree_update.nodes[0].child_ids.push_back(2);
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].relative_bounds.bounds = gfx::RectF(100, 10, 400, 300);
  AXTree tree(tree_update);

  EXPECT_EQ("(0, 0) size (800 x 600)", GetBoundsAsString(tree, 1));
  EXPECT_EQ("(100, 10) size (400 x 300)", GetBoundsAsString(tree, 2));
}

// If a node doesn't specify its location but at least one child does have
// a location, its computed bounds should be the union of all child bounds.
TEST(AXTreeTest, EmptyNodeBoundsIsUnionOfChildren) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(4);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);
  tree_update.nodes[0].child_ids.push_back(2);
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].relative_bounds.bounds =
      gfx::RectF();  // Deliberately empty.
  tree_update.nodes[1].child_ids.push_back(3);
  tree_update.nodes[1].child_ids.push_back(4);
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].relative_bounds.bounds = gfx::RectF(100, 10, 400, 20);
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].relative_bounds.bounds = gfx::RectF(200, 30, 400, 20);

  AXTree tree(tree_update);
  EXPECT_EQ("(100, 10) size (500 x 40)", GetBoundsAsString(tree, 2));
}

// If a node doesn't specify its location but at least one child does have
// a location, it will be offscreen if all of its children are offscreen.
TEST(AXTreeTest, EmptyNodeNotOffscreenEvenIfAllChildrenOffscreen) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(4);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);
  tree_update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  tree_update.nodes[0].AddBoolAttribute(
      ax::mojom::BoolAttribute::kClipsChildren, true);
  tree_update.nodes[0].child_ids.push_back(2);
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].relative_bounds.bounds =
      gfx::RectF();  // Deliberately empty.
  tree_update.nodes[1].child_ids.push_back(3);
  tree_update.nodes[1].child_ids.push_back(4);
  // Both children are offscreen
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].relative_bounds.bounds = gfx::RectF(900, 10, 400, 20);
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].relative_bounds.bounds = gfx::RectF(1000, 30, 400, 20);

  AXTree tree(tree_update);
  EXPECT_FALSE(IsNodeOffscreen(tree, 2));
  EXPECT_TRUE(IsNodeOffscreen(tree, 3));
  EXPECT_TRUE(IsNodeOffscreen(tree, 4));
}

// Test that getting the bounds of a node works when there's a transform.
TEST(AXTreeTest, GetBoundsWithTransform) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(3);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].relative_bounds.bounds = gfx::RectF(0, 0, 400, 300);
  tree_update.nodes[0].relative_bounds.transform.reset(new gfx::Transform());
  tree_update.nodes[0].relative_bounds.transform->Scale(2.0, 2.0);
  tree_update.nodes[0].child_ids.push_back(2);
  tree_update.nodes[0].child_ids.push_back(3);
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].relative_bounds.bounds = gfx::RectF(20, 10, 50, 5);
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].relative_bounds.bounds = gfx::RectF(20, 30, 50, 5);
  tree_update.nodes[2].relative_bounds.transform.reset(new gfx::Transform());
  tree_update.nodes[2].relative_bounds.transform->Scale(2.0, 2.0);

  AXTree tree(tree_update);
  EXPECT_EQ("(0, 0) size (800 x 600)", GetBoundsAsString(tree, 1));
  EXPECT_EQ("(40, 20) size (100 x 10)", GetBoundsAsString(tree, 2));
  EXPECT_EQ("(80, 120) size (200 x 20)", GetBoundsAsString(tree, 3));
}

// Test that getting the bounds of a node that's inside a container
// works correctly.
TEST(AXTreeTest, GetBoundsWithContainerId) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(4);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);
  tree_update.nodes[0].child_ids.push_back(2);
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].relative_bounds.bounds = gfx::RectF(100, 50, 600, 500);
  tree_update.nodes[1].child_ids.push_back(3);
  tree_update.nodes[1].child_ids.push_back(4);
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].relative_bounds.offset_container_id = 2;
  tree_update.nodes[2].relative_bounds.bounds = gfx::RectF(20, 30, 50, 5);
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].relative_bounds.bounds = gfx::RectF(20, 30, 50, 5);

  AXTree tree(tree_update);
  EXPECT_EQ("(120, 80) size (50 x 5)", GetBoundsAsString(tree, 3));
  EXPECT_EQ("(20, 30) size (50 x 5)", GetBoundsAsString(tree, 4));
}

// Test that getting the bounds of a node that's inside a scrolling container
// works correctly.
TEST(AXTreeTest, GetBoundsWithScrolling) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(3);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);
  tree_update.nodes[0].child_ids.push_back(2);
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].relative_bounds.bounds = gfx::RectF(100, 50, 600, 500);
  tree_update.nodes[1].AddIntAttribute(ax::mojom::IntAttribute::kScrollX, 5);
  tree_update.nodes[1].AddIntAttribute(ax::mojom::IntAttribute::kScrollY, 10);
  tree_update.nodes[1].child_ids.push_back(3);
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].relative_bounds.offset_container_id = 2;
  tree_update.nodes[2].relative_bounds.bounds = gfx::RectF(20, 30, 50, 5);

  AXTree tree(tree_update);
  EXPECT_EQ("(115, 70) size (50 x 5)", GetBoundsAsString(tree, 3));
}

TEST(AXTreeTest, GetBoundsEmptyBoundsInheritsFromParent) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(3);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);
  tree_update.nodes[1].AddBoolAttribute(
      ax::mojom::BoolAttribute::kClipsChildren, true);
  tree_update.nodes[0].child_ids.push_back(2);
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].relative_bounds.bounds = gfx::RectF(300, 200, 100, 100);
  tree_update.nodes[1].child_ids.push_back(3);
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].relative_bounds.bounds = gfx::RectF();

  AXTree tree(tree_update);
  EXPECT_EQ("(0, 0) size (800 x 600)", GetBoundsAsString(tree, 1));
  EXPECT_EQ("(300, 200) size (100 x 100)", GetBoundsAsString(tree, 2));
  EXPECT_EQ("(300, 200) size (100 x 100)", GetBoundsAsString(tree, 3));
  EXPECT_EQ("(0, 0) size (800 x 600)", GetUnclippedBoundsAsString(tree, 1));
  EXPECT_EQ("(300, 200) size (100 x 100)", GetUnclippedBoundsAsString(tree, 2));
  EXPECT_EQ("(300, 200) size (100 x 100)", GetUnclippedBoundsAsString(tree, 3));
  EXPECT_FALSE(IsNodeOffscreen(tree, 1));
  EXPECT_FALSE(IsNodeOffscreen(tree, 2));
  EXPECT_TRUE(IsNodeOffscreen(tree, 3));
}

TEST(AXTreeTest, GetBoundsCropsChildToRoot) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(5);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);
  tree_update.nodes[0].AddBoolAttribute(
      ax::mojom::BoolAttribute::kClipsChildren, true);
  tree_update.nodes[0].child_ids.push_back(2);
  tree_update.nodes[0].child_ids.push_back(3);
  tree_update.nodes[0].child_ids.push_back(4);
  tree_update.nodes[0].child_ids.push_back(5);
  // Cropped in the top left
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].relative_bounds.bounds =
      gfx::RectF(-100, -100, 150, 150);
  // Cropped in the bottom right
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].relative_bounds.bounds = gfx::RectF(700, 500, 150, 150);
  // Offscreen on the top
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].relative_bounds.bounds = gfx::RectF(50, -200, 150, 150);
  // Offscreen on the bottom
  tree_update.nodes[4].id = 5;
  tree_update.nodes[4].relative_bounds.bounds = gfx::RectF(50, 700, 150, 150);

  AXTree tree(tree_update);
  EXPECT_EQ("(0, 0) size (50 x 50)", GetBoundsAsString(tree, 2));
  EXPECT_EQ("(700, 500) size (100 x 100)", GetBoundsAsString(tree, 3));
  EXPECT_EQ("(50, 0) size (150 x 1)", GetBoundsAsString(tree, 4));
  EXPECT_EQ("(50, 599) size (150 x 1)", GetBoundsAsString(tree, 5));

  // Check the unclipped bounds are as expected.
  EXPECT_EQ("(-100, -100) size (150 x 150)",
            GetUnclippedBoundsAsString(tree, 2));
  EXPECT_EQ("(700, 500) size (150 x 150)", GetUnclippedBoundsAsString(tree, 3));
  EXPECT_EQ("(50, -200) size (150 x 150)", GetUnclippedBoundsAsString(tree, 4));
  EXPECT_EQ("(50, 700) size (150 x 150)", GetUnclippedBoundsAsString(tree, 5));
}

TEST(AXTreeTest, GetBoundsSetsOffscreenIfClipsChildren) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(5);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);
  tree_update.nodes[0].AddBoolAttribute(
      ax::mojom::BoolAttribute::kClipsChildren, true);
  tree_update.nodes[0].child_ids.push_back(2);
  tree_update.nodes[0].child_ids.push_back(3);

  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].relative_bounds.bounds = gfx::RectF(0, 0, 200, 200);
  tree_update.nodes[1].AddBoolAttribute(
      ax::mojom::BoolAttribute::kClipsChildren, true);
  tree_update.nodes[1].child_ids.push_back(4);

  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].relative_bounds.bounds = gfx::RectF(0, 0, 200, 200);
  tree_update.nodes[2].child_ids.push_back(5);

  // Clipped by its parent
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].relative_bounds.bounds = gfx::RectF(250, 250, 100, 100);
  tree_update.nodes[3].relative_bounds.offset_container_id = 2;

  // Outside of its parent, but its parent does not clip children,
  // so it should not be offscreen.
  tree_update.nodes[4].id = 5;
  tree_update.nodes[4].relative_bounds.bounds = gfx::RectF(250, 250, 100, 100);
  tree_update.nodes[4].relative_bounds.offset_container_id = 3;

  AXTree tree(tree_update);
  EXPECT_TRUE(IsNodeOffscreen(tree, 4));
  EXPECT_FALSE(IsNodeOffscreen(tree, 5));
}

TEST(AXTreeTest, GetBoundsUpdatesOffscreen) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(5);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].relative_bounds.bounds = gfx::RectF(0, 0, 800, 600);
  tree_update.nodes[0].role = ax::mojom::Role::kRootWebArea;
  tree_update.nodes[0].AddBoolAttribute(
      ax::mojom::BoolAttribute::kClipsChildren, true);
  tree_update.nodes[0].child_ids.push_back(2);
  tree_update.nodes[0].child_ids.push_back(3);
  tree_update.nodes[0].child_ids.push_back(4);
  tree_update.nodes[0].child_ids.push_back(5);
  // Fully onscreen
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].relative_bounds.bounds = gfx::RectF(10, 10, 150, 150);
  // Cropped in the bottom right
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].relative_bounds.bounds = gfx::RectF(700, 500, 150, 150);
  // Offscreen on the top
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].relative_bounds.bounds = gfx::RectF(50, -200, 150, 150);
  // Offscreen on the bottom
  tree_update.nodes[4].id = 5;
  tree_update.nodes[4].relative_bounds.bounds = gfx::RectF(50, 700, 150, 150);

  AXTree tree(tree_update);
  EXPECT_FALSE(IsNodeOffscreen(tree, 2));
  EXPECT_FALSE(IsNodeOffscreen(tree, 3));
  EXPECT_TRUE(IsNodeOffscreen(tree, 4));
  EXPECT_TRUE(IsNodeOffscreen(tree, 5));
}

TEST(AXTreeTest, IntReverseRelations) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(4);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].AddIntAttribute(
      ax::mojom::IntAttribute::kActivedescendantId, 2);
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[0].child_ids.push_back(3);
  initial_state.nodes[0].child_ids.push_back(4);
  initial_state.nodes[1].id = 2;
  initial_state.nodes[2].id = 3;
  initial_state.nodes[2].AddIntAttribute(ax::mojom::IntAttribute::kMemberOfId,
                                         1);
  initial_state.nodes[3].id = 4;
  initial_state.nodes[3].AddIntAttribute(ax::mojom::IntAttribute::kMemberOfId,
                                         1);
  AXTree tree(initial_state);

  auto reverse_active_descendant =
      tree.GetReverseRelations(ax::mojom::IntAttribute::kActivedescendantId, 2);
  ASSERT_EQ(1U, reverse_active_descendant.size());
  EXPECT_TRUE(base::ContainsKey(reverse_active_descendant, 1));

  reverse_active_descendant =
      tree.GetReverseRelations(ax::mojom::IntAttribute::kActivedescendantId, 1);
  ASSERT_EQ(0U, reverse_active_descendant.size());

  auto reverse_errormessage =
      tree.GetReverseRelations(ax::mojom::IntAttribute::kErrormessageId, 1);
  ASSERT_EQ(0U, reverse_errormessage.size());

  auto reverse_member_of =
      tree.GetReverseRelations(ax::mojom::IntAttribute::kMemberOfId, 1);
  ASSERT_EQ(2U, reverse_member_of.size());
  EXPECT_TRUE(base::ContainsKey(reverse_member_of, 3));
  EXPECT_TRUE(base::ContainsKey(reverse_member_of, 4));

  AXTreeUpdate update = initial_state;
  update.nodes.resize(5);
  update.nodes[0].int_attributes.clear();
  update.nodes[0].AddIntAttribute(ax::mojom::IntAttribute::kActivedescendantId,
                                  5);
  update.nodes[0].child_ids.push_back(5);
  update.nodes[2].int_attributes.clear();
  update.nodes[4].id = 5;
  update.nodes[4].AddIntAttribute(ax::mojom::IntAttribute::kMemberOfId, 1);

  EXPECT_TRUE(tree.Unserialize(update));

  reverse_active_descendant =
      tree.GetReverseRelations(ax::mojom::IntAttribute::kActivedescendantId, 2);
  ASSERT_EQ(0U, reverse_active_descendant.size());

  reverse_active_descendant =
      tree.GetReverseRelations(ax::mojom::IntAttribute::kActivedescendantId, 5);
  ASSERT_EQ(1U, reverse_active_descendant.size());
  EXPECT_TRUE(base::ContainsKey(reverse_active_descendant, 1));

  reverse_member_of =
      tree.GetReverseRelations(ax::mojom::IntAttribute::kMemberOfId, 1);
  ASSERT_EQ(2U, reverse_member_of.size());
  EXPECT_TRUE(base::ContainsKey(reverse_member_of, 4));
  EXPECT_TRUE(base::ContainsKey(reverse_member_of, 5));
}

TEST(AXTreeTest, IntListReverseRelations) {
  std::vector<int32_t> node_two;
  node_two.push_back(2);

  std::vector<int32_t> nodes_two_three;
  nodes_two_three.push_back(2);
  nodes_two_three.push_back(3);

  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(3);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].AddIntListAttribute(
      ax::mojom::IntListAttribute::kLabelledbyIds, node_two);
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[0].child_ids.push_back(3);
  initial_state.nodes[1].id = 2;
  initial_state.nodes[2].id = 3;

  AXTree tree(initial_state);

  auto reverse_labelled_by =
      tree.GetReverseRelations(ax::mojom::IntListAttribute::kLabelledbyIds, 2);
  ASSERT_EQ(1U, reverse_labelled_by.size());
  EXPECT_TRUE(base::ContainsKey(reverse_labelled_by, 1));

  reverse_labelled_by =
      tree.GetReverseRelations(ax::mojom::IntListAttribute::kLabelledbyIds, 3);
  ASSERT_EQ(0U, reverse_labelled_by.size());

  // Change existing attributes.
  AXTreeUpdate update = initial_state;
  update.nodes[0].intlist_attributes.clear();
  update.nodes[0].AddIntListAttribute(
      ax::mojom::IntListAttribute::kLabelledbyIds, nodes_two_three);
  EXPECT_TRUE(tree.Unserialize(update));

  reverse_labelled_by =
      tree.GetReverseRelations(ax::mojom::IntListAttribute::kLabelledbyIds, 3);
  ASSERT_EQ(1U, reverse_labelled_by.size());
  EXPECT_TRUE(base::ContainsKey(reverse_labelled_by, 1));
}

TEST(AXTreeTest, DeletingNodeUpdatesReverseRelations) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(3);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].child_ids = {2, 3};
  initial_state.nodes[1].id = 2;
  initial_state.nodes[2].id = 3;
  initial_state.nodes[2].AddIntAttribute(
      ax::mojom::IntAttribute::kActivedescendantId, 2);
  AXTree tree(initial_state);

  auto reverse_active_descendant =
      tree.GetReverseRelations(ax::mojom::IntAttribute::kActivedescendantId, 2);
  ASSERT_EQ(1U, reverse_active_descendant.size());
  EXPECT_TRUE(base::ContainsKey(reverse_active_descendant, 3));

  AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(1);
  update.nodes[0].id = 1;
  update.nodes[0].child_ids = {2};
  EXPECT_TRUE(tree.Unserialize(update));

  reverse_active_descendant =
      tree.GetReverseRelations(ax::mojom::IntAttribute::kActivedescendantId, 2);
  ASSERT_EQ(0U, reverse_active_descendant.size());
}

TEST(AXTreeTest, ReverseRelationsDoNotKeepGrowing) {
  // The number of total entries in int_reverse_relations and
  // intlist_reverse_relations should not keep growing as the tree
  // changes.

  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(2);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].AddIntAttribute(
      ax::mojom::IntAttribute::kActivedescendantId, 2);
  initial_state.nodes[0].AddIntListAttribute(
      ax::mojom::IntListAttribute::kLabelledbyIds, {2});
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[1].id = 2;
  AXTree tree(initial_state);

  for (int i = 0; i < 1000; ++i) {
    AXTreeUpdate update;
    update.root_id = 1;
    update.nodes.resize(2);
    update.nodes[0].id = 1;
    update.nodes[1].id = i + 3;
    update.nodes[0].AddIntAttribute(
        ax::mojom::IntAttribute::kActivedescendantId, update.nodes[1].id);
    update.nodes[0].AddIntListAttribute(
        ax::mojom::IntListAttribute::kLabelledbyIds, {update.nodes[1].id});
    update.nodes[1].AddIntAttribute(ax::mojom::IntAttribute::kMemberOfId, 1);
    update.nodes[0].child_ids.push_back(update.nodes[1].id);
    EXPECT_TRUE(tree.Unserialize(update));
  }

  size_t map_key_count = 0;
  size_t set_entry_count = 0;
  for (auto& iter : tree.int_reverse_relations()) {
    map_key_count += iter.second.size() + 1;
    for (auto it2 = iter.second.begin(); it2 != iter.second.end(); ++it2) {
      set_entry_count += it2->second.size();
    }
  }

  // Note: 10 is arbitary, the idea here is just that we mutated the tree
  // 1000 times, so if we have fewer than 10 entries in the maps / sets then
  // the map isn't growing / leaking. Same below.
  EXPECT_LT(map_key_count, 10U);
  EXPECT_LT(set_entry_count, 10U);

  map_key_count = 0;
  set_entry_count = 0;
  for (auto& iter : tree.intlist_reverse_relations()) {
    map_key_count += iter.second.size() + 1;
    for (auto it2 = iter.second.begin(); it2 != iter.second.end(); ++it2) {
      set_entry_count += it2->second.size();
    }
  }
  EXPECT_LT(map_key_count, 10U);
  EXPECT_LT(set_entry_count, 10U);
}

TEST(AXTreeTest, SkipIgnoredNodes) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(5);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].child_ids = {2, 3};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].AddState(ax::mojom::State::kIgnored);
  tree_update.nodes[1].child_ids = {4, 5};
  tree_update.nodes[2].id = 3;
  tree_update.nodes[3].id = 4;
  tree_update.nodes[4].id = 5;

  AXTree tree(tree_update);
  AXNode* root = tree.root();
  ASSERT_EQ(2u, root->children().size());
  ASSERT_EQ(2, root->children()[0]->id());
  ASSERT_EQ(3, root->children()[1]->id());

  EXPECT_EQ(3u, root->GetUnignoredChildCount());
  EXPECT_EQ(4, root->GetUnignoredChildAtIndex(0)->id());
  EXPECT_EQ(5, root->GetUnignoredChildAtIndex(1)->id());
  EXPECT_EQ(3, root->GetUnignoredChildAtIndex(2)->id());
  EXPECT_EQ(0u, root->GetUnignoredChildAtIndex(0)->GetUnignoredIndexInParent());
  EXPECT_EQ(1u, root->GetUnignoredChildAtIndex(1)->GetUnignoredIndexInParent());
  EXPECT_EQ(2u, root->GetUnignoredChildAtIndex(2)->GetUnignoredIndexInParent());

  EXPECT_EQ(1, root->GetUnignoredChildAtIndex(0)->GetUnignoredParent()->id());
}

TEST(AXTreeTest, TestRecursionUnignoredChildCount) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(5);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].child_ids = {2, 3};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].AddState(ax::mojom::State::kIgnored);
  tree_update.nodes[1].child_ids = {4};
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].AddState(ax::mojom::State::kIgnored);
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].child_ids = {5};
  tree_update.nodes[3].AddState(ax::mojom::State::kIgnored);
  tree_update.nodes[4].id = 5;
  AXTree tree(tree_update);

  AXNode* root = tree.root();
  EXPECT_EQ(2u, root->children().size());
  EXPECT_EQ(1u, root->GetUnignoredChildCount());
  EXPECT_EQ(5, root->GetUnignoredChildAtIndex(0)->id());
  AXNode* unignored = tree.GetFromId(5);
  EXPECT_EQ(0u, unignored->GetUnignoredChildCount());
}

TEST(AXTreeTest, NullUnignoredChildren) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(3);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].child_ids = {2, 3};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].AddState(ax::mojom::State::kIgnored);
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].AddState(ax::mojom::State::kIgnored);
  AXTree tree(tree_update);

  AXNode* root = tree.root();
  EXPECT_EQ(2u, root->children().size());
  EXPECT_EQ(0u, root->GetUnignoredChildCount());
  EXPECT_EQ(nullptr, root->GetUnignoredChildAtIndex(0));
  EXPECT_EQ(nullptr, root->GetUnignoredChildAtIndex(1));
}

TEST(AXTreeTest, ChildTreeIds) {
  ui::AXTreeID tree_id_1 = ui::AXTreeID::CreateNewAXTreeID();
  ui::AXTreeID tree_id_2 = ui::AXTreeID::CreateNewAXTreeID();
  ui::AXTreeID tree_id_3 = ui::AXTreeID::CreateNewAXTreeID();

  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(4);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[0].child_ids.push_back(3);
  initial_state.nodes[0].child_ids.push_back(4);
  initial_state.nodes[1].id = 2;
  initial_state.nodes[1].AddStringAttribute(
      ax::mojom::StringAttribute::kChildTreeId, tree_id_2.ToString());
  initial_state.nodes[2].id = 3;
  initial_state.nodes[2].AddStringAttribute(
      ax::mojom::StringAttribute::kChildTreeId, tree_id_3.ToString());
  initial_state.nodes[3].id = 4;
  initial_state.nodes[3].AddStringAttribute(
      ax::mojom::StringAttribute::kChildTreeId, tree_id_3.ToString());
  AXTree tree(initial_state);

  auto child_tree_1_nodes = tree.GetNodeIdsForChildTreeId(tree_id_1);
  EXPECT_EQ(0U, child_tree_1_nodes.size());

  auto child_tree_2_nodes = tree.GetNodeIdsForChildTreeId(tree_id_2);
  EXPECT_EQ(1U, child_tree_2_nodes.size());
  EXPECT_TRUE(base::ContainsKey(child_tree_2_nodes, 2));

  auto child_tree_3_nodes = tree.GetNodeIdsForChildTreeId(tree_id_3);
  EXPECT_EQ(2U, child_tree_3_nodes.size());
  EXPECT_TRUE(base::ContainsKey(child_tree_3_nodes, 3));
  EXPECT_TRUE(base::ContainsKey(child_tree_3_nodes, 4));

  AXTreeUpdate update = initial_state;
  update.nodes[2].string_attributes.clear();
  update.nodes[2].AddStringAttribute(ax::mojom::StringAttribute::kChildTreeId,
                                     tree_id_2.ToString());
  update.nodes[3].string_attributes.clear();

  EXPECT_TRUE(tree.Unserialize(update));

  child_tree_2_nodes = tree.GetNodeIdsForChildTreeId(tree_id_2);
  EXPECT_EQ(2U, child_tree_2_nodes.size());
  EXPECT_TRUE(base::ContainsKey(child_tree_2_nodes, 2));
  EXPECT_TRUE(base::ContainsKey(child_tree_2_nodes, 3));

  child_tree_3_nodes = tree.GetNodeIdsForChildTreeId(tree_id_3);
  EXPECT_EQ(0U, child_tree_3_nodes.size());
}

// Tests GetPosInSet and GetSetSize return the assigned int attribute values.
TEST(AXTreeTest, TestSetSizePosInSetAssigned) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(4);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kList;
  tree_update.nodes[0].child_ids = {2, 3, 4};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kListItem;
  tree_update.nodes[1].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 2);
  tree_update.nodes[1].AddIntAttribute(ax::mojom::IntAttribute::kSetSize, 12);
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kListItem;
  tree_update.nodes[2].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 5);
  tree_update.nodes[2].AddIntAttribute(ax::mojom::IntAttribute::kSetSize, 12);
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kListItem;
  tree_update.nodes[3].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 9);
  tree_update.nodes[3].AddIntAttribute(ax::mojom::IntAttribute::kSetSize, 12);
  AXTree tree(tree_update);

  AXNode* item1 = tree.GetFromId(2);
  EXPECT_EQ(item1->GetPosInSet(), 2);
  EXPECT_EQ(item1->GetSetSize(), 12);
  AXNode* item2 = tree.GetFromId(3);
  EXPECT_EQ(item2->GetPosInSet(), 5);
  EXPECT_EQ(item2->GetSetSize(), 12);
  AXNode* item3 = tree.GetFromId(4);
  EXPECT_EQ(item3->GetPosInSet(), 9);
  EXPECT_EQ(item3->GetSetSize(), 12);
}

// Tests that pos_in_set and set_size can be calculated if not assigned.
TEST(AXTreeTest, TestSetSizePosInSetUnassigned) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(4);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kList;
  tree_update.nodes[0].child_ids = {2, 3, 4};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kListItem;
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kListItem;
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kListItem;
  AXTree tree(tree_update);

  AXNode* item1 = tree.GetFromId(2);
  EXPECT_EQ(item1->GetPosInSet(), 1);
  EXPECT_EQ(item1->GetSetSize(), 3);
  AXNode* item2 = tree.GetFromId(3);
  EXPECT_EQ(item2->GetPosInSet(), 2);
  EXPECT_EQ(item2->GetSetSize(), 3);
  AXNode* item3 = tree.GetFromId(4);
  EXPECT_EQ(item3->GetPosInSet(), 3);
  EXPECT_EQ(item3->GetSetSize(), 3);
}

// Tests pos_in_set can be calculated if unassigned, and set_size can be
// assigned on the outerlying ordered set.
TEST(AXTreeTest, TestSetSizeAssignedInContainer) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(4);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kList;
  tree_update.nodes[0].child_ids = {2, 3, 4};
  tree_update.nodes[0].AddIntAttribute(ax::mojom::IntAttribute::kSetSize, 7);
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kListItem;
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kListItem;
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kListItem;
  AXTree tree(tree_update);

  // Items should inherit set_size from ordered set if not specified.
  AXNode* item1 = tree.GetFromId(2);
  EXPECT_EQ(item1->GetSetSize(), 7);
  AXNode* item2 = tree.GetFromId(3);
  EXPECT_EQ(item2->GetSetSize(), 7);
  AXNode* item3 = tree.GetFromId(4);
  EXPECT_EQ(item3->GetSetSize(), 7);
}

// Tests GetPosInSet and GetSetSize on a list containing various roles.
// Roles for items and associated ordered set should match up.
TEST(AXTreeTest, TestSetSizePosInSetDiverseList) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(6);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kMenu;
  tree_update.nodes[0].child_ids = {2, 3, 4, 5, 6};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kMenuItem;  // 1 of 4
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kMenuItemCheckBox;  // 2 of 4
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kMenuItemRadio;  // 3 of 4
  tree_update.nodes[4].id = 5;
  tree_update.nodes[4].role = ax::mojom::Role::kMenuItem;  // 4 of 4
  tree_update.nodes[5].id = 6;
  tree_update.nodes[5].role = ax::mojom::Role::kTab;  // 0 of 0
  AXTree tree(tree_update);

  // kMenu is allowed to contain: kMenuItem, kMenuItemCheckbox,
  // and kMenuItemRadio. For PosInSet and SetSize purposes, these items
  // are treated as the same role.
  AXNode* item1 = tree.GetFromId(2);
  EXPECT_EQ(item1->GetPosInSet(), 1);
  EXPECT_EQ(item1->GetSetSize(), 4);
  AXNode* checkbox = tree.GetFromId(3);
  EXPECT_EQ(checkbox->GetPosInSet(), 2);
  EXPECT_EQ(checkbox->GetSetSize(), 4);
  AXNode* radio = tree.GetFromId(4);
  EXPECT_EQ(radio->GetPosInSet(), 3);
  EXPECT_EQ(radio->GetSetSize(), 4);
  AXNode* item3 = tree.GetFromId(5);
  EXPECT_EQ(item3->GetPosInSet(), 4);
  EXPECT_EQ(item3->GetSetSize(), 4);
  AXNode* image = tree.GetFromId(6);
  EXPECT_EQ(image->GetPosInSet(), 0);
  EXPECT_EQ(image->GetSetSize(), 0);
}

// Tests GetPosInSet and GetSetSize on a nested list.
TEST(AXTreeTest, TestSetSizePosInSetNestedList) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(7);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kList;
  tree_update.nodes[0].child_ids = {2, 3, 4, 7};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kListItem;
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kListItem;
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kList;
  tree_update.nodes[3].child_ids = {5, 6};
  tree_update.nodes[4].id = 5;
  tree_update.nodes[4].role = ax::mojom::Role::kListItem;
  tree_update.nodes[5].id = 6;
  tree_update.nodes[5].role = ax::mojom::Role::kListItem;
  tree_update.nodes[6].id = 7;
  tree_update.nodes[6].role = ax::mojom::Role::kListItem;
  AXTree tree(tree_update);

  AXNode* outer_item1 = tree.GetFromId(2);
  EXPECT_EQ(outer_item1->GetPosInSet(), 1);
  EXPECT_EQ(outer_item1->GetSetSize(), 3);
  AXNode* outer_item2 = tree.GetFromId(3);
  EXPECT_EQ(outer_item2->GetPosInSet(), 2);
  EXPECT_EQ(outer_item2->GetSetSize(), 3);

  AXNode* inner_item1 = tree.GetFromId(5);
  EXPECT_EQ(inner_item1->GetPosInSet(), 1);
  EXPECT_EQ(inner_item1->GetSetSize(), 2);
  AXNode* inner_item2 = tree.GetFromId(6);
  EXPECT_EQ(inner_item2->GetPosInSet(), 2);
  EXPECT_EQ(inner_item2->GetSetSize(), 2);

  AXNode* outer_item3 = tree.GetFromId(7);
  EXPECT_EQ(outer_item3->GetPosInSet(), 3);
  EXPECT_EQ(outer_item3->GetSetSize(), 3);
}

// Tests pos_in_set can be calculated if one item specifies pos_in_set, but
// other assignments are missing.
TEST(AXTreeTest, TestPosInSetMissing) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(4);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kList;
  tree_update.nodes[0].child_ids = {2, 3, 4};
  tree_update.nodes[0].AddIntAttribute(ax::mojom::IntAttribute::kSetSize, 20);
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kListItem;
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kListItem;
  tree_update.nodes[2].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 13);
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kListItem;
  AXTree tree(tree_update);

  // Item1 should have pos of 12, since item2 is assigned a pos of 13.
  AXNode* item1 = tree.GetFromId(2);
  EXPECT_EQ(item1->GetPosInSet(), 1);
  EXPECT_EQ(item1->GetSetSize(), 20);
  AXNode* item2 = tree.GetFromId(3);
  EXPECT_EQ(item2->GetPosInSet(), 13);
  EXPECT_EQ(item2->GetSetSize(), 20);
  // Item2 should have pos of 14, since item2 is assigned a pos of 13.
  AXNode* item3 = tree.GetFromId(4);
  EXPECT_EQ(item3->GetPosInSet(), 14);
  EXPECT_EQ(item3->GetSetSize(), 20);
}

// A more difficult test that invovles missing pos_in_set and set_size values.
TEST(AXTreeTest, TestSetSizePosInSetMissingDifficult) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(6);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kList;
  tree_update.nodes[0].child_ids = {2, 3, 4, 5, 6};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kListItem;  // 1 of 11
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kListItem;
  tree_update.nodes[2].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet,
                                       5);  // 5 of 11
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kListItem;  // 6 of 11
  tree_update.nodes[4].id = 5;
  tree_update.nodes[4].role = ax::mojom::Role::kListItem;
  tree_update.nodes[4].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet,
                                       10);  // 10 of 11
  tree_update.nodes[5].id = 6;
  tree_update.nodes[5].role = ax::mojom::Role::kListItem;  // 11 of 11
  AXTree tree(tree_update);

  AXNode* item1 = tree.GetFromId(2);
  EXPECT_EQ(item1->GetPosInSet(), 1);
  EXPECT_EQ(item1->GetSetSize(), 11);
  AXNode* item2 = tree.GetFromId(3);
  EXPECT_EQ(item2->GetPosInSet(), 5);
  EXPECT_EQ(item2->GetSetSize(), 11);
  AXNode* item3 = tree.GetFromId(4);
  EXPECT_EQ(item3->GetPosInSet(), 6);
  EXPECT_EQ(item3->GetSetSize(), 11);
  AXNode* item4 = tree.GetFromId(5);
  EXPECT_EQ(item4->GetPosInSet(), 10);
  EXPECT_EQ(item4->GetSetSize(), 11);
  AXNode* item5 = tree.GetFromId(6);
  EXPECT_EQ(item5->GetPosInSet(), 11);
  EXPECT_EQ(item5->GetSetSize(), 11);
}

// Tests that code overwrites decreasing set_size assignments to largest of
// assigned values.
TEST(AXTreeTest, TestSetSizeDecreasing) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(4);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kList;
  tree_update.nodes[0].child_ids = {2, 3, 4};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kListItem;  // 1 of 5
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kListItem;  // 2 of 5
  tree_update.nodes[2].AddIntAttribute(ax::mojom::IntAttribute::kSetSize, 5);
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kListItem;  // 3 of 5
  tree_update.nodes[3].AddIntAttribute(ax::mojom::IntAttribute::kSetSize, 4);
  AXTree tree(tree_update);

  AXNode* item1 = tree.GetFromId(2);
  EXPECT_EQ(item1->GetPosInSet(), 1);
  EXPECT_EQ(item1->GetSetSize(), 5);
  AXNode* item2 = tree.GetFromId(3);
  EXPECT_EQ(item2->GetPosInSet(), 2);
  EXPECT_EQ(item2->GetSetSize(), 5);
  AXNode* item3 = tree.GetFromId(4);
  EXPECT_EQ(item3->GetPosInSet(), 3);
  EXPECT_EQ(item3->GetSetSize(), 5);
}

// Tests that code overwrites decreasing pos_in_set values.
TEST(AXTreeTest, TestPosInSetDecreasing) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(4);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kList;
  tree_update.nodes[0].child_ids = {2, 3, 4};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kListItem;  // 1 of 8
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kListItem;  // 7 of 8
  tree_update.nodes[2].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 7);
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kListItem;  // 8 of 8
  tree_update.nodes[3].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 3);
  AXTree tree(tree_update);

  AXNode* item1 = tree.GetFromId(2);
  EXPECT_EQ(item1->GetPosInSet(), 1);
  EXPECT_EQ(item1->GetSetSize(), 8);
  AXNode* item2 = tree.GetFromId(3);
  EXPECT_EQ(item2->GetPosInSet(), 7);
  EXPECT_EQ(item2->GetSetSize(), 8);
  AXNode* item3 = tree.GetFromId(4);
  EXPECT_EQ(item3->GetPosInSet(), 8);
  EXPECT_EQ(item3->GetSetSize(), 8);
}

// Tests that code overwrites duplicate pos_in_set values. Note this case is
// tricky; an update to the second element causes an update to the third
// element.
TEST(AXTreeTest, TestPosInSetDuplicates) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(4);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kList;
  tree_update.nodes[0].child_ids = {2, 3, 4};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kListItem;  // 6 of 8
  tree_update.nodes[1].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 6);
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kListItem;  // 7 of 8
  tree_update.nodes[2].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 6);
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kListItem;  // 8 of 8
  tree_update.nodes[3].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 7);
  AXTree tree(tree_update);

  AXNode* item1 = tree.GetFromId(2);
  EXPECT_EQ(item1->GetPosInSet(), 6);
  EXPECT_EQ(item1->GetSetSize(), 8);
  AXNode* item2 = tree.GetFromId(3);
  EXPECT_EQ(item2->GetPosInSet(), 7);
  EXPECT_EQ(item2->GetSetSize(), 8);
  AXNode* item3 = tree.GetFromId(4);
  EXPECT_EQ(item3->GetPosInSet(), 8);
  EXPECT_EQ(item3->GetSetSize(), 8);
}

// Tests GetPosInSet and GetSetSize when some list items are nested in a generic
// container.
TEST(AXTreeTest, TestSetSizePosInSetNestedContainer) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(7);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kList;
  tree_update.nodes[0].child_ids = {2, 3, 7};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kListItem;  // 1 of 4
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kGenericContainer;
  tree_update.nodes[2].child_ids = {4, 5};
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kListItem;  // 2 of 4
  tree_update.nodes[4].id = 5;
  tree_update.nodes[4].role = ax::mojom::Role::kIgnored;
  tree_update.nodes[4].child_ids = {6};
  tree_update.nodes[5].id = 6;
  tree_update.nodes[5].role = ax::mojom::Role::kListItem;  // 3 of 4
  tree_update.nodes[6].id = 7;
  tree_update.nodes[6].role = ax::mojom::Role::kListItem;  // 4 of 4
  AXTree tree(tree_update);

  AXNode* item1 = tree.GetFromId(2);
  EXPECT_EQ(item1->GetPosInSet(), 1);
  EXPECT_EQ(item1->GetSetSize(), 4);
  AXNode* g_container = tree.GetFromId(3);
  EXPECT_EQ(g_container->GetPosInSet(), 0);
  EXPECT_EQ(g_container->GetSetSize(), 0);
  AXNode* item2 = tree.GetFromId(4);
  EXPECT_EQ(item2->GetPosInSet(), 2);
  EXPECT_EQ(item2->GetSetSize(), 4);
  AXNode* ignored = tree.GetFromId(5);
  EXPECT_EQ(ignored->GetPosInSet(), 0);
  EXPECT_EQ(ignored->GetSetSize(), 0);
  AXNode* item3 = tree.GetFromId(6);
  EXPECT_EQ(item3->GetPosInSet(), 3);
  EXPECT_EQ(item3->GetSetSize(), 4);
  AXNode* item4 = tree.GetFromId(7);
  EXPECT_EQ(item4->GetPosInSet(), 4);
  EXPECT_EQ(item4->GetSetSize(), 4);
}

// Tests GetSetSize and GetPosInSet are correct, even when list items change.
// This test is directed at the caching functionality of pos_in_set and
// set_size. Tests that previously calculated values are not used after
// tree is updated.
TEST(AXTreeTest, TestSetSizePosInSetDeleteItem) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(4);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].role = ax::mojom::Role::kList;
  initial_state.nodes[0].child_ids = {2, 3, 4};
  initial_state.nodes[1].id = 2;
  initial_state.nodes[1].role = ax::mojom::Role::kListItem;  // 1 of 3
  initial_state.nodes[2].id = 3;
  initial_state.nodes[2].role = ax::mojom::Role::kListItem;  // 2 of 3
  initial_state.nodes[3].id = 4;
  initial_state.nodes[3].role = ax::mojom::Role::kListItem;  // 3 of 3
  AXTree tree(initial_state);

  AXNode* item1 = tree.GetFromId(2);
  EXPECT_EQ(item1->GetPosInSet(), 1);
  EXPECT_EQ(item1->GetSetSize(), 3);
  AXNode* item2 = tree.GetFromId(3);
  EXPECT_EQ(item2->GetPosInSet(), 2);
  EXPECT_EQ(item2->GetSetSize(), 3);
  AXNode* item3 = tree.GetFromId(4);
  EXPECT_EQ(item3->GetPosInSet(), 3);
  EXPECT_EQ(item3->GetSetSize(), 3);

  // TreeUpdates only need to describe what changed in tree.
  AXTreeUpdate update = initial_state;
  update.nodes.resize(1);
  update.nodes[0].child_ids = {2, 4};  // Delete item 2 of 3 from list.
  EXPECT_TRUE(tree.Unserialize(update));

  AXNode* new_item1 = tree.GetFromId(2);
  EXPECT_EQ(new_item1->GetPosInSet(), 1);
  EXPECT_EQ(new_item1->GetSetSize(), 2);
  AXNode* new_item2 = tree.GetFromId(4);
  EXPECT_EQ(new_item2->GetPosInSet(), 2);
  EXPECT_EQ(new_item2->GetSetSize(), 2);
}

// Tests GetSetSize and GetPosInSet are correct, even when list items change.
// This test adds an item to the front of a list, which invalidates previously
// calculated pos_in_set and set_size values. Tests that old values are not
// used after tree is updated.
TEST(AXTreeTest, TestSetSizePosInSetAddItem) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(4);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].role = ax::mojom::Role::kList;
  initial_state.nodes[0].child_ids = {2, 3, 4};
  initial_state.nodes[1].id = 2;
  initial_state.nodes[1].role = ax::mojom::Role::kListItem;  // 1 of 3
  initial_state.nodes[2].id = 3;
  initial_state.nodes[2].role = ax::mojom::Role::kListItem;  // 2 of 3
  initial_state.nodes[3].id = 4;
  initial_state.nodes[3].role = ax::mojom::Role::kListItem;  // 3 of 3
  AXTree tree(initial_state);

  AXNode* item1 = tree.GetFromId(2);
  EXPECT_EQ(item1->GetPosInSet(), 1);
  EXPECT_EQ(item1->GetSetSize(), 3);
  AXNode* item2 = tree.GetFromId(3);
  EXPECT_EQ(item2->GetPosInSet(), 2);
  EXPECT_EQ(item2->GetSetSize(), 3);
  AXNode* item3 = tree.GetFromId(4);
  EXPECT_EQ(item3->GetPosInSet(), 3);
  EXPECT_EQ(item3->GetSetSize(), 3);

  // Insert item at beginning of list
  AXTreeUpdate update = initial_state;
  update.nodes.resize(2);
  update.nodes[0].id = 1;
  update.nodes[0].child_ids = {5, 2, 3, 4};
  update.nodes[1].id = 5;
  update.nodes[1].role = ax::mojom::Role::kListItem;
  EXPECT_TRUE(tree.Unserialize(update));

  AXNode* new_item1 = tree.GetFromId(5);
  EXPECT_EQ(new_item1->GetPosInSet(), 1);
  EXPECT_EQ(new_item1->GetSetSize(), 4);
  AXNode* new_item2 = tree.GetFromId(2);
  EXPECT_EQ(new_item2->GetPosInSet(), 2);
  EXPECT_EQ(new_item2->GetSetSize(), 4);
  AXNode* new_item3 = tree.GetFromId(3);
  EXPECT_EQ(new_item3->GetPosInSet(), 3);
  EXPECT_EQ(new_item3->GetSetSize(), 4);
  AXNode* new_item4 = tree.GetFromId(4);
  EXPECT_EQ(new_item4->GetPosInSet(), 4);
  EXPECT_EQ(new_item4->GetSetSize(), 4);
}

// Tests that the outerlying ordered set reports a set_size. Ordered sets
// should not report a pos_in_set value other than 0, since they are not
// considered to be items within a set (even when nested).
TEST(AXTreeTest, TestOrderedSetReportsSetSize) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(12);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kList;  // set_size = 3
  tree_update.nodes[0].child_ids = {2, 3, 4, 7, 8, 9, 12};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kListItem;  // 1 of 3
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kListItem;  // 2 of 3
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kList;  // set_size = 2
  tree_update.nodes[3].child_ids = {5, 6};
  tree_update.nodes[4].id = 5;
  tree_update.nodes[4].role = ax::mojom::Role::kListItem;  // 1 of 2
  tree_update.nodes[5].id = 6;
  tree_update.nodes[5].role = ax::mojom::Role::kListItem;  // 2 of 2
  tree_update.nodes[6].id = 7;
  tree_update.nodes[6].role = ax::mojom::Role::kListItem;  // 3 of 3
  tree_update.nodes[7].id = 8;
  tree_update.nodes[7].role = ax::mojom::Role::kList;  // set_size = 0
  tree_update.nodes[8].id = 9;
  tree_update.nodes[8].role =
      ax::mojom::Role::kList;  // set_size = 1 because only 1 item whose role
                               // matches
  tree_update.nodes[8].child_ids = {10, 11};
  tree_update.nodes[9].id = 10;
  tree_update.nodes[9].role = ax::mojom::Role::kArticle;
  tree_update.nodes[10].id = 11;
  tree_update.nodes[10].role = ax::mojom::Role::kListItem;
  tree_update.nodes[11].id = 12;
  tree_update.nodes[11].role = ax::mojom::Role::kList;
  tree_update.nodes[11].AddIntAttribute(ax::mojom::IntAttribute::kSetSize, 5);
  AXTree tree(tree_update);

  AXNode* outer_list = tree.GetFromId(1);
  EXPECT_EQ(outer_list->GetPosInSet(), 0);  // Ordered sets have pos of 0
  EXPECT_EQ(outer_list->GetSetSize(), 3);
  AXNode* outer_list_item1 = tree.GetFromId(2);
  EXPECT_EQ(outer_list_item1->GetPosInSet(), 1);
  EXPECT_EQ(outer_list_item1->GetSetSize(), 3);
  AXNode* outer_list_item2 = tree.GetFromId(3);
  EXPECT_EQ(outer_list_item2->GetPosInSet(), 2);
  EXPECT_EQ(outer_list_item2->GetSetSize(), 3);
  AXNode* outer_list_item3 = tree.GetFromId(7);
  EXPECT_EQ(outer_list_item3->GetPosInSet(), 3);
  EXPECT_EQ(outer_list_item3->GetSetSize(), 3);

  AXNode* inner_list1 = tree.GetFromId(4);
  EXPECT_EQ(inner_list1->GetPosInSet(),
            0);  // Ordered sets have pos of 0, even when nested
  EXPECT_EQ(inner_list1->GetSetSize(), 2);
  AXNode* inner_list1_item1 = tree.GetFromId(5);
  EXPECT_EQ(inner_list1_item1->GetPosInSet(), 1);
  EXPECT_EQ(inner_list1_item1->GetSetSize(), 2);
  AXNode* inner_list1_item2 = tree.GetFromId(6);
  EXPECT_EQ(inner_list1_item2->GetPosInSet(), 2);
  EXPECT_EQ(inner_list1_item2->GetSetSize(), 2);

  AXNode* inner_list2 = tree.GetFromId(8);  // Empty list
  EXPECT_EQ(inner_list2->GetPosInSet(), 0);
  EXPECT_EQ(inner_list2->GetSetSize(), 0);

  AXNode* inner_list3 = tree.GetFromId(9);
  EXPECT_EQ(inner_list3->GetPosInSet(), 0);
  EXPECT_EQ(inner_list3->GetSetSize(), 1);  // Only 1 item whose role matches
  AXNode* inner_list3_article1 = tree.GetFromId(10);
  EXPECT_EQ(inner_list3_article1->GetPosInSet(), 0);
  EXPECT_EQ(inner_list3_article1->GetSetSize(), 0);
  AXNode* inner_list3_item1 = tree.GetFromId(11);
  EXPECT_EQ(inner_list3_item1->GetPosInSet(), 1);
  EXPECT_EQ(inner_list3_item1->GetSetSize(), 1);

  AXNode* inner_list4 = tree.GetFromId(12);
  EXPECT_EQ(inner_list4->GetPosInSet(), 0);
  // Even though list is empty, kSetSize attribute was set, so it takes
  // precedence
  EXPECT_EQ(inner_list4->GetSetSize(), 5);
}

// Tests GetPosInSet and GetSetSize code on invalid input.
TEST(AXTreeTest, TestSetSizePosInSetInvalid) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(3);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kListItem;  // 0 of 0
  tree_update.nodes[0].child_ids = {2, 3};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kListItem;
  tree_update.nodes[1].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet,
                                       4);  // 0 of 0
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kListItem;
  AXTree tree(tree_update);

  AXNode* item1 = tree.GetFromId(1);
  EXPECT_EQ(item1->GetPosInSet(), 0);
  EXPECT_EQ(item1->GetSetSize(), 0);
  AXNode* item2 = tree.GetFromId(2);
  EXPECT_EQ(item2->GetPosInSet(), 0);
  EXPECT_EQ(item2->GetSetSize(), 0);
  AXNode* item3 = tree.GetFromId(3);
  EXPECT_EQ(item3->GetPosInSet(), 0);
  EXPECT_EQ(item3->GetSetSize(), 0);
}

// Tests GetPosInSet and GetSetSize code on kRadioButtons. Radio buttons
// behave differently than other item-like elements; most notably, they do not
// need to be contained within an ordered set to report a PosInSet or SetSize.
TEST(AXTreeTest, TestSetSizePosInSetRadioButtons) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(13);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].child_ids = {2, 3, 4, 10, 13};

  // Radio buttons are not required to be contained within an ordered set.
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kRadioButton;  // 1 of 5
  tree_update.nodes[1].AddStringAttribute(ax::mojom::StringAttribute::kName,
                                          "sports");
  tree_update.nodes[1].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 1);
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kRadioButton;  // 2 of 5
  tree_update.nodes[2].AddStringAttribute(ax::mojom::StringAttribute::kName,
                                          "books");
  tree_update.nodes[2].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 2);
  tree_update.nodes[2].AddIntAttribute(ax::mojom::IntAttribute::kSetSize, 5);

  // Radio group with nested generic container.
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kRadioGroup;  // setsize = 4
  tree_update.nodes[3].child_ids = {5, 6, 7};
  tree_update.nodes[4].id = 5;
  tree_update.nodes[4].role = ax::mojom::Role::kRadioButton;
  tree_update.nodes[4].AddStringAttribute(ax::mojom::StringAttribute::kName,
                                          "recipes");  // 1 of 4
  tree_update.nodes[5].id = 6;
  tree_update.nodes[5].role = ax::mojom::Role::kRadioButton;
  tree_update.nodes[5].AddStringAttribute(ax::mojom::StringAttribute::kName,
                                          "recipes");  // 2 of 4
  tree_update.nodes[6].id = 7;
  tree_update.nodes[6].role = ax::mojom::Role::kGenericContainer;
  tree_update.nodes[6].child_ids = {8, 9};
  tree_update.nodes[7].id = 8;
  tree_update.nodes[7].role = ax::mojom::Role::kRadioButton;
  tree_update.nodes[7].AddStringAttribute(ax::mojom::StringAttribute::kName,
                                          "recipes");  // 3 of 4
  tree_update.nodes[8].id = 9;
  tree_update.nodes[8].role = ax::mojom::Role::kRadioButton;
  tree_update.nodes[8].AddStringAttribute(ax::mojom::StringAttribute::kName,
                                          "recipes");  // 4 of 4

  // Radio buttons are allowed to be contained within forms.
  tree_update.nodes[9].id = 10;
  tree_update.nodes[9].role = ax::mojom::Role::kForm;
  tree_update.nodes[9].child_ids = {11, 12};
  tree_update.nodes[10].id = 11;
  tree_update.nodes[10].role = ax::mojom::Role::kRadioButton;
  tree_update.nodes[10].AddStringAttribute(ax::mojom::StringAttribute::kName,
                                           "cities");  // 1 of 2
  tree_update.nodes[11].id = 12;
  tree_update.nodes[11].role = ax::mojom::Role::kRadioButton;
  tree_update.nodes[11].AddStringAttribute(ax::mojom::StringAttribute::kName,
                                           "cities");  // 2 of 2
  tree_update.nodes[12].id = 13;
  tree_update.nodes[12].role = ax::mojom::Role::kRadioButton;  // 4 of 5
  tree_update.nodes[12].AddStringAttribute(ax::mojom::StringAttribute::kName,
                                           "sports");
  tree_update.nodes[12].AddIntAttribute(ax::mojom::IntAttribute::kPosInSet, 4);

  AXTree tree(tree_update);

  AXNode* sports_button1 = tree.GetFromId(2);
  EXPECT_EQ(sports_button1->GetPosInSet(), 1);
  EXPECT_EQ(sports_button1->GetSetSize(), 5);
  AXNode* books_button = tree.GetFromId(3);
  EXPECT_EQ(books_button->GetPosInSet(), 2);
  EXPECT_EQ(books_button->GetSetSize(), 5);

  AXNode* radiogroup1 = tree.GetFromId(4);
  EXPECT_EQ(radiogroup1->GetPosInSet(), 0);
  EXPECT_EQ(radiogroup1->GetSetSize(), 4);
  AXNode* recipes_button1 = tree.GetFromId(5);
  EXPECT_EQ(recipes_button1->GetPosInSet(), 1);
  EXPECT_EQ(recipes_button1->GetSetSize(), 4);
  AXNode* recipes_button2 = tree.GetFromId(6);
  EXPECT_EQ(recipes_button2->GetPosInSet(), 2);
  EXPECT_EQ(recipes_button2->GetSetSize(), 4);

  AXNode* generic_container = tree.GetFromId(7);
  EXPECT_EQ(generic_container->GetPosInSet(), 0);
  EXPECT_EQ(generic_container->GetSetSize(), 0);
  AXNode* recipes_button3 = tree.GetFromId(8);
  EXPECT_EQ(recipes_button3->GetPosInSet(), 3);
  EXPECT_EQ(recipes_button3->GetSetSize(), 4);
  AXNode* recipes_button4 = tree.GetFromId(9);
  EXPECT_EQ(recipes_button4->GetPosInSet(), 4);
  EXPECT_EQ(recipes_button4->GetSetSize(), 4);

  // Elements with role kForm shouldn't report posinset or setsize
  AXNode* form = tree.GetFromId(10);
  EXPECT_EQ(form->GetPosInSet(), 0);
  EXPECT_EQ(form->GetSetSize(), 0);
  AXNode* cities_button1 = tree.GetFromId(11);
  EXPECT_EQ(cities_button1->GetPosInSet(), 1);
  EXPECT_EQ(cities_button1->GetSetSize(), 2);
  AXNode* cities_button2 = tree.GetFromId(12);
  EXPECT_EQ(cities_button2->GetPosInSet(), 2);
  EXPECT_EQ(cities_button2->GetSetSize(), 2);

  AXNode* sports_button2 = tree.GetFromId(13);
  EXPECT_EQ(sports_button2->GetPosInSet(), 4);
  EXPECT_EQ(sports_button2->GetSetSize(), 5);
}

// Tests GetPosInSet and GetSetSize on a list that includes radio buttons.
// Note that radio buttons do not contribute to the SetSize of the outerlying
// list.
TEST(AXTreeTest, TestSetSizePosInSetRadioButtonsInList) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(6);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role =
      ax::mojom::Role::kList;  // set_size = 2, since only contains 2 ListItems
  tree_update.nodes[0].child_ids = {2, 3, 4, 5, 6};

  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kRadioButton;  // 1 of 3
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kListItem;  // 1 of 2
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kRadioButton;  // 2 of 3
  tree_update.nodes[4].id = 5;
  tree_update.nodes[4].role = ax::mojom::Role::kListItem;  // 2 of 2
  tree_update.nodes[5].id = 6;
  tree_update.nodes[5].role = ax::mojom::Role::kRadioButton;  // 3 of 3
  AXTree tree(tree_update);

  AXNode* list = tree.GetFromId(1);
  EXPECT_EQ(list->GetPosInSet(), 0);
  EXPECT_EQ(list->GetSetSize(), 2);

  AXNode* radiobutton1 = tree.GetFromId(2);
  EXPECT_EQ(radiobutton1->GetPosInSet(), 1);
  EXPECT_EQ(radiobutton1->GetSetSize(), 3);
  AXNode* item1 = tree.GetFromId(3);
  EXPECT_EQ(item1->GetPosInSet(), 1);
  EXPECT_EQ(item1->GetSetSize(), 2);
  AXNode* radiobutton2 = tree.GetFromId(4);
  EXPECT_EQ(radiobutton2->GetPosInSet(), 2);
  EXPECT_EQ(radiobutton2->GetSetSize(), 3);
  AXNode* item2 = tree.GetFromId(5);
  EXPECT_EQ(item2->GetPosInSet(), 2);
  EXPECT_EQ(item2->GetSetSize(), 2);
  AXNode* radiobutton3 = tree.GetFromId(6);
  EXPECT_EQ(radiobutton3->GetPosInSet(), 3);
  EXPECT_EQ(radiobutton3->GetSetSize(), 3);

  // Ensure that the setsize of list was not modified after calling GetPosInSet
  // and GetSetSize on kRadioButtons.
  EXPECT_EQ(list->GetPosInSet(), 0);
  EXPECT_EQ(list->GetSetSize(), 2);
}

// Tests GetPosInSet and GetSetSize on a flat tree representation. According
// to the tree representation, the three elements are siblings. However,
// due to the presence of the kHierarchicalLevel attribute, they all belong
// to different sets.
TEST(AXTreeTest, TestSetSizePosInSetFlatTree) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(4);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kTree;
  tree_update.nodes[0].child_ids = {2, 3, 4};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kTreeItem;  // 1 of 1
  tree_update.nodes[1].AddIntAttribute(
      ax::mojom::IntAttribute::kHierarchicalLevel, 1);
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kTreeItem;  // 1 of 1
  tree_update.nodes[2].AddIntAttribute(
      ax::mojom::IntAttribute::kHierarchicalLevel, 2);
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kTreeItem;  // 1 of 1
  tree_update.nodes[3].AddIntAttribute(
      ax::mojom::IntAttribute::kHierarchicalLevel, 3);
  AXTree tree(tree_update);

  AXNode* item1_level1 = tree.GetFromId(2);
  EXPECT_EQ(item1_level1->GetPosInSet(), 1);
  EXPECT_EQ(item1_level1->GetSetSize(), 1);
  AXNode* item1_level2 = tree.GetFromId(3);
  EXPECT_EQ(item1_level2->GetPosInSet(), 1);
  EXPECT_EQ(item1_level2->GetSetSize(), 1);
  AXNode* item1_level3 = tree.GetFromId(4);
  EXPECT_EQ(item1_level3->GetPosInSet(), 1);
  EXPECT_EQ(item1_level3->GetSetSize(), 1);
}

// Tests GetPosInSet and GetSetSize on a flat tree representation, where only
// the level is specified.
TEST(AXTreeTest, TestSetSizePosInSetFlatTreeLevelsOnly) {
  AXTreeUpdate tree_update;
  tree_update.root_id = 1;
  tree_update.nodes.resize(9);
  tree_update.nodes[0].id = 1;
  tree_update.nodes[0].role = ax::mojom::Role::kTree;
  tree_update.nodes[0].child_ids = {2, 3, 4, 5, 6, 7, 8, 9};
  tree_update.nodes[1].id = 2;
  tree_update.nodes[1].role = ax::mojom::Role::kTreeItem;  // 1 of 3
  tree_update.nodes[1].AddIntAttribute(
      ax::mojom::IntAttribute::kHierarchicalLevel, 1);
  tree_update.nodes[2].id = 3;
  tree_update.nodes[2].role = ax::mojom::Role::kTreeItem;  // 1 of 2
  tree_update.nodes[2].AddIntAttribute(
      ax::mojom::IntAttribute::kHierarchicalLevel, 2);
  tree_update.nodes[3].id = 4;
  tree_update.nodes[3].role = ax::mojom::Role::kTreeItem;  // 2 of 2
  tree_update.nodes[3].AddIntAttribute(
      ax::mojom::IntAttribute::kHierarchicalLevel, 2);
  tree_update.nodes[4].id = 5;
  tree_update.nodes[4].role = ax::mojom::Role::kTreeItem;  // 2 of 3
  tree_update.nodes[4].AddIntAttribute(
      ax::mojom::IntAttribute::kHierarchicalLevel, 1);
  tree_update.nodes[5].id = 6;
  tree_update.nodes[5].role = ax::mojom::Role::kTreeItem;  // 1 of 3
  tree_update.nodes[5].AddIntAttribute(
      ax::mojom::IntAttribute::kHierarchicalLevel, 2);
  tree_update.nodes[6].id = 7;
  tree_update.nodes[6].role = ax::mojom::Role::kTreeItem;  // 2 of 3
  tree_update.nodes[6].AddIntAttribute(
      ax::mojom::IntAttribute::kHierarchicalLevel, 2);
  tree_update.nodes[7].id = 8;
  tree_update.nodes[7].role = ax::mojom::Role::kTreeItem;  // 3 of 3
  tree_update.nodes[7].AddIntAttribute(
      ax::mojom::IntAttribute::kHierarchicalLevel, 2);
  tree_update.nodes[8].id = 9;
  tree_update.nodes[8].role = ax::mojom::Role::kTreeItem;  // 3 of 3
  tree_update.nodes[8].AddIntAttribute(
      ax::mojom::IntAttribute::kHierarchicalLevel, 1);
  AXTree tree(tree_update);

  // The order in which we query the nodes should not matter.
  AXNode* item3_level1 = tree.GetFromId(9);
  EXPECT_EQ(item3_level1->GetPosInSet(), 3);
  EXPECT_EQ(item3_level1->GetSetSize(), 3);
  AXNode* item3_level2a = tree.GetFromId(8);
  EXPECT_EQ(item3_level2a->GetPosInSet(), 3);
  EXPECT_EQ(item3_level2a->GetSetSize(), 3);
  AXNode* item2_level2a = tree.GetFromId(7);
  EXPECT_EQ(item2_level2a->GetPosInSet(), 2);
  EXPECT_EQ(item2_level2a->GetSetSize(), 3);
  AXNode* item1_level2a = tree.GetFromId(6);
  EXPECT_EQ(item1_level2a->GetPosInSet(), 1);
  EXPECT_EQ(item1_level2a->GetSetSize(), 3);
  AXNode* item2_level1 = tree.GetFromId(5);
  EXPECT_EQ(item2_level1->GetPosInSet(), 2);
  EXPECT_EQ(item2_level1->GetSetSize(), 3);
  AXNode* item2_level2 = tree.GetFromId(4);
  EXPECT_EQ(item2_level2->GetPosInSet(), 2);
  EXPECT_EQ(item2_level2->GetSetSize(), 2);
  AXNode* item1_level2 = tree.GetFromId(3);
  EXPECT_EQ(item1_level2->GetPosInSet(), 1);
  EXPECT_EQ(item1_level2->GetSetSize(), 2);
  AXNode* item1_level1 = tree.GetFromId(2);
  EXPECT_EQ(item1_level1->GetPosInSet(), 1);
  EXPECT_EQ(item1_level1->GetSetSize(), 3);
  AXNode* ordered_set = tree.GetFromId(1);
  EXPECT_EQ(ordered_set->GetSetSize(), 3);
}

// Tests that GetPosInSet and GetSetSize work while a tree is being
// unserialized.
TEST(AXTreeTest, TestSetSizePosInSetSubtreeDeleted) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(3);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].role = ax::mojom::Role::kTree;
  initial_state.nodes[0].child_ids = {2, 3};
  initial_state.nodes[1].id = 2;
  initial_state.nodes[1].role = ax::mojom::Role::kTreeItem;
  initial_state.nodes[2].id = 3;
  initial_state.nodes[2].role = ax::mojom::Role::kTreeItem;
  AXTree tree(initial_state);

  // This should work normally.
  AXNode* item = tree.GetFromId(3);
  EXPECT_EQ(item->GetPosInSet(), 2);
  EXPECT_EQ(item->GetSetSize(), 2);

  // Use test observer to assert posinset and setsize are 0.
  TestAXTreeObserver test_observer(&tree);
  test_observer.call_posinset_and_setsize = true;
  // Remove item from tree.
  AXTreeUpdate tree_update = initial_state;
  tree_update.nodes.resize(1);
  tree_update.nodes[0].child_ids = {2};

  ASSERT_TRUE(tree.Unserialize(tree_update));
}

}  // namespace ui
