// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing/renderer/threat_dom_details.h"

#include <algorithm>
#include <map>
#include <string>
#include <unordered_set>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "components/safe_browsing/features.h"
#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_element_collection.h"
#include "third_party/blink/public/web/web_frame.h"
#include "third_party/blink/public/web/web_local_frame.h"

namespace safe_browsing {

// A map for keeping track of the identity of DOM Elements, used to generate
// unique IDs for each element and lookup elements IDs by parent Element, to
// maintain proper parent/child relationships.
// They key is a WebNode from the DOM, which is basically a pointer so can be
// copied into the map when inserting new elements.
// The values are indices into the resource vector, and are used to retrieve IPC
// messages generated by ThreatDOMDetails.
using ElementToNodeMap = std::map<blink::WebNode, size_t>;

// The name of the param containing the tags and attributes list.
const char kTagAndAttributeParamName[] = "tag_attribute_csv";

namespace {

// Predicate used to search |tag_and_attributes_list_| by tag_name.
class TagNameIs {
 public:
  explicit TagNameIs(const std::string& tag) : tag_(tag) {}
  bool operator()(const TagAndAttributesItem& tag_and_attribute) {
    return tag_ == tag_and_attribute.tag_name;
  }

 private:
  std::string tag_;
};

void GetDefaultTagAndAttributeList(
    std::vector<TagAndAttributesItem>* tag_and_attributes_list) {
  tag_and_attributes_list->clear();

  // These entries must be sorted by tag name.
  // These tags are related to identifying Google ads.
  tag_and_attributes_list->push_back(
      TagAndAttributesItem("div", {"data-google-query-id", "id"}));
  tag_and_attributes_list->push_back(TagAndAttributesItem("iframe", {"id"}));
}

void ParseTagAndAttributeParams(
    std::vector<TagAndAttributesItem>* tag_and_attributes_list) {
  DCHECK(tag_and_attributes_list);
  // If the feature is disabled we just use the default list. Otherwise the list
  // from the Finch param will be the one used.
  if (!base::FeatureList::IsEnabled(kThreatDomDetailsTagAndAttributeFeature)) {
    GetDefaultTagAndAttributeList(tag_and_attributes_list);
    return;
  }
  tag_and_attributes_list->clear();
  const std::string& tag_attribute_csv_param =
      base::GetFieldTrialParamValueByFeature(
          kThreatDomDetailsTagAndAttributeFeature, kTagAndAttributeParamName);
  if (tag_attribute_csv_param.empty()) {
    return;
  }

  std::vector<std::string> split =
      base::SplitString(tag_attribute_csv_param, ",", base::TRIM_WHITESPACE,
                        base::SPLIT_WANT_NONEMPTY);
  // If we don't have the right number of pairs in the csv then don't bother
  // parsing further.
  if (split.size() % 2 != 0) {
    return;
  }
  for (size_t i = 0; i < split.size(); i += 2) {
    const std::string& tag_name = split[i];
    const std::string& attribute = split[i + 1];
    auto item_iter =
        std::find_if(tag_and_attributes_list->begin(),
                     tag_and_attributes_list->end(), TagNameIs(tag_name));
    if (item_iter == tag_and_attributes_list->end()) {
      TagAndAttributesItem item;
      item.tag_name = tag_name;
      item.attributes.push_back(attribute);
      tag_and_attributes_list->push_back(item);
    } else {
      item_iter->attributes.push_back(attribute);
    }
  }

  std::sort(tag_and_attributes_list->begin(), tag_and_attributes_list->end(),
            [](const TagAndAttributesItem& a, const TagAndAttributesItem& b) {
              return a.tag_name < b.tag_name;
            });
}

mojom::ThreatDOMDetailsNode* GetNodeForElement(
    const blink::WebNode& element,
    const safe_browsing::ElementToNodeMap& element_to_node_map,
    std::vector<mojom::ThreatDOMDetailsNodePtr>* resources) {
  DCHECK_GT(element_to_node_map.count(element), 0u);
  size_t resource_index = element_to_node_map.at(element);
  return (*resources)[resource_index].get();
}

std::string TruncateAttributeString(const std::string& input) {
  if (input.length() <= ThreatDOMDetails::kMaxAttributeStringLength) {
    return input;
  }

  std::string truncated;
  base::TruncateUTF8ToByteSize(
      input, ThreatDOMDetails::kMaxAttributeStringLength - 3, &truncated);
  truncated.append("...");
  return truncated;
}

// Handler for the various HTML elements that we extract URLs from.
void HandleElement(
    const blink::WebElement& element,
    const std::vector<TagAndAttributesItem>& tag_and_attributes_list,
    mojom::ThreatDOMDetailsNode* summary_node,
    std::vector<mojom::ThreatDOMDetailsNodePtr>* resources,
    safe_browsing::ElementToNodeMap* element_to_node_map) {
  // Retrieve the link and resolve the link in case it's relative.
  blink::WebURL full_url =
      element.GetDocument().CompleteURL(element.GetAttribute("src"));

  const GURL& child_url = GURL(full_url);
  if (!child_url.is_empty() && child_url.is_valid()) {
    summary_node->children.push_back(child_url);
  }

  mojom::ThreatDOMDetailsNodePtr child_node =
      mojom::ThreatDOMDetailsNode::New();
  child_node->url = child_url;
  child_node->tag_name = element.TagName().Utf8();
  child_node->parent = summary_node->url;

  // The body of an iframe may be in a different renderer. Look up the routing
  // ID of the local or remote frame and store it with the iframe node. If this
  // element is not a frame then the result of the lookup will be null.
  blink::WebFrame* subframe = blink::WebFrame::FromFrameOwnerElement(element);
  if (subframe) {
    child_node->child_frame_routing_id =
        content::RenderFrame::GetRoutingIdForWebFrame(subframe);
  }

  // Populate the element's attributes, but only collect the ones that are
  // configured in the finch study.
  const auto& tag_attribute_iter = std::find_if(
      tag_and_attributes_list.begin(), tag_and_attributes_list.end(),
      TagNameIs(base::ToLowerASCII(child_node->tag_name)));
  if (tag_attribute_iter != tag_and_attributes_list.end()) {
    const std::vector<std::string> attributes_to_collect =
        tag_attribute_iter->attributes;
    for (const std::string& attribute : attributes_to_collect) {
      blink::WebString attr_webstring = blink::WebString::FromASCII(attribute);
      if (!element.HasAttribute(attr_webstring)) {
        continue;
      }
      mojom::AttributeNameValuePtr attribute_name_value =
          mojom::AttributeNameValue::New(
              attribute, TruncateAttributeString(
                             element.GetAttribute(attr_webstring).Ascii()));
      child_node->attributes.push_back(std::move(attribute_name_value));
      if (child_node->attributes.size() == ThreatDOMDetails::kMaxAttributes) {
        break;
      }
    }
  }

  // Update the ID mapping. First generate the ID for the current node.
  // Then, if its parent is available, set the current node's parent ID, and
  // also update the parent's children with the current node's ID.
  const int child_id = static_cast<int>(element_to_node_map->size()) + 1;
  child_node->node_id = child_id;
  blink::WebNode cur_parent_element = element.ParentNode();
  while (!cur_parent_element.IsNull()) {
    if (element_to_node_map->count(cur_parent_element) > 0) {
      mojom::ThreatDOMDetailsNode* parent_node = GetNodeForElement(
          cur_parent_element, *element_to_node_map, resources);
      child_node->parent_node_id = parent_node->node_id;
      parent_node->child_node_ids.push_back(child_id);

      // TODO(lpz): Consider also updating the URL-level parent/child mapping
      // here. Eg: child_node.parent=parent_node.url, and
      // parent_node.children.push_back(child_url).
      break;
    } else {
      // It's possible that the direct parent of this node wasn't handled, so it
      // isn't represented in |element_to_node_map|. Try walking up the
      // hierarchy to see if a parent further up was handled.
      cur_parent_element = cur_parent_element.ParentNode();
    }
  }
  // Add the child node to the list of resources.
  resources->push_back(std::move(child_node));
  // .. and remember which index it was inserted at so we can look it up later.
  (*element_to_node_map)[element] = resources->size() - 1;
}

bool ShouldHandleElement(
    const blink::WebElement& element,
    const std::vector<TagAndAttributesItem>& tag_and_attributes_list) {
  // Resources with a SRC are always handled.
  if ((element.HasHTMLTagName("iframe") || element.HasHTMLTagName("frame") ||
       element.HasHTMLTagName("embed") || element.HasHTMLTagName("script")) &&
      element.HasAttribute("src")) {
    return true;
  }

  std::string tag_name_lower = base::ToLowerASCII(element.TagName().Ascii());
  const auto& tag_attribute_iter =
      std::find_if(tag_and_attributes_list.begin(),
                   tag_and_attributes_list.end(), TagNameIs(tag_name_lower));
  if (tag_attribute_iter == tag_and_attributes_list.end()) {
    return false;
  }

  const std::vector<std::string>& valid_attributes =
      tag_attribute_iter->attributes;
  for (const std::string& attribute : valid_attributes) {
    if (element.HasAttribute(blink::WebString::FromASCII(attribute))) {
      return true;
    }
  }
  return false;
}

}  // namespace

TagAndAttributesItem::TagAndAttributesItem() {}

TagAndAttributesItem::TagAndAttributesItem(
    const std::string& tag_name_param,
    const std::vector<std::string>& attributes_param)
    : tag_name(tag_name_param), attributes(attributes_param) {}

TagAndAttributesItem::TagAndAttributesItem(const TagAndAttributesItem& item)
    : tag_name(item.tag_name), attributes(item.attributes) {}

TagAndAttributesItem::~TagAndAttributesItem() {}

uint32_t ThreatDOMDetails::kMaxNodes = 500;
uint32_t ThreatDOMDetails::kMaxAttributes = 100;
uint32_t ThreatDOMDetails::kMaxAttributeStringLength = 100;

// static
ThreatDOMDetails* ThreatDOMDetails::Create(
    content::RenderFrame* render_frame,
    service_manager::BinderRegistry* registry) {
  // Private constructor and public static Create() method to facilitate
  // stubbing out this class for binary-size reduction purposes.
  return new ThreatDOMDetails(render_frame, registry);
}

void ThreatDOMDetails::OnThreatReporterRequest(
    mojom::ThreatReporterRequest request) {
  threat_reporter_bindings_.AddBinding(this, std::move(request));
}

ThreatDOMDetails::ThreatDOMDetails(content::RenderFrame* render_frame,
                                   service_manager::BinderRegistry* registry)
    : content::RenderFrameObserver(render_frame) {
  ParseTagAndAttributeParams(&tag_and_attributes_list_);
  // Base::Unretained() is safe here because both the registry and the
  // ThreatDOMDetails are scoped to the same render frame.
  registry->AddInterface(base::BindRepeating(
      &ThreatDOMDetails::OnThreatReporterRequest, base::Unretained(this)));
}

ThreatDOMDetails::~ThreatDOMDetails() {}

void ThreatDOMDetails::GetThreatDOMDetails(
    GetThreatDOMDetailsCallback callback) {
  std::vector<mojom::ThreatDOMDetailsNodePtr> resources;
  ExtractResources(&resources);
  // Notify the browser.
  std::move(callback).Run(std::move(resources));
}

void ThreatDOMDetails::ExtractResources(
    std::vector<mojom::ThreatDOMDetailsNodePtr>* resources) {
  blink::WebLocalFrame* frame = render_frame()->GetWebFrame();
  if (!frame)
    return;
  mojom::ThreatDOMDetailsNodePtr details_node =
      mojom::ThreatDOMDetailsNode::New();
  blink::WebDocument document = frame->GetDocument();
  details_node->url = GURL(document.Url());
  if (document.IsNull()) {
    // Nothing in this frame. Just report its URL.
    resources->push_back(std::move(details_node));
    return;
  }

  ElementToNodeMap element_to_node_map;
  blink::WebElementCollection elements = document.All();
  blink::WebElement element = elements.FirstItem();
  bool max_nodes_exceeded = false;
  for (; !element.IsNull(); element = elements.NextItem()) {
    if (ShouldHandleElement(element, tag_and_attributes_list_)) {
      HandleElement(element, tag_and_attributes_list_, details_node.get(),
                    resources, &element_to_node_map);
      if (resources->size() >= kMaxNodes) {
        // We have reached kMaxNodes, exit early.
        max_nodes_exceeded = true;
        break;
      }
    }
  }
  UMA_HISTOGRAM_BOOLEAN("SafeBrowsing.ThreatReport.MaxNodesExceededInFrame",
                        max_nodes_exceeded);
  resources->push_back(std::move(details_node));
}

void ThreatDOMDetails::OnDestruct() {
  delete this;
}

}  // namespace safe_browsing
