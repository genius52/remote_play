// Copyright (c) 2012 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "tests/cefclient/renderer/client_renderer.h"

#include <sstream>
#include <string>

#include "include/cef_crash_util.h"
#include "include/cef_dom.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_message_router.h"
//#include "pca_v8handler.h"

#include "include/cef_v8.h"

class pca_v8handler : public CefV8Handler
{
public:
    pca_v8handler();
    bool Execute(const CefString& name, CefRefPtr<CefV8Value> object, const CefV8ValueList& arguments, CefRefPtr<CefV8Value>& retval, CefString& exception) override;

private:
    IMPLEMENT_REFCOUNTING(pca_v8handler);
};


pca_v8handler::pca_v8handler() {
}

bool pca_v8handler::Execute(const CefString& name, CefRefPtr<CefV8Value> object, const CefV8ValueList& arguments, CefRefPtr<CefV8Value>& retval, CefString& exception)
{
    if (name == "pca_openwebbrowser") {
        if (arguments.size() == 1 && arguments[0]->IsString()) {
            CefString cefurl = arguments[0]->GetStringValue();
            auto msg = CefProcessMessage::Create("openwebbrowser");
            auto args = msg->GetArgumentList();
            args->SetString(0, cefurl);

            CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
        }
    }
    else if (name == "pca_opennewtab") {
        CefString cefurl = arguments[0]->GetStringValue();
        auto msg = CefProcessMessage::Create("opennewtab");
        auto args = msg->GetArgumentList();
        args->SetString(0, cefurl);
        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    }
    else if (name == "pca_mute") {
        bool ismute = arguments[0]->GetBoolValue();
        auto msg = CefProcessMessage::Create("mute");
        auto args = msg->GetArgumentList();
        args->SetBool(0, ismute);
        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    }
    else if (name == "pca_min") {
        auto msg = CefProcessMessage::Create("min_window");
        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    }
    else if (name == "pca_max") {
        auto msg = CefProcessMessage::Create("max_window");
        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    }
    else if (name == "pca_restore") {
        auto msg = CefProcessMessage::Create("restore_window");
        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    }
    else if (name == "pca_fullscreen") {
        auto msg = CefProcessMessage::Create("fullscreen");
        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    }
    else if (name == "pca_close") {
        auto msg = CefProcessMessage::Create("close_window");
        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    }
    else if (name == "pca_force_close") {
        auto msg = CefProcessMessage::Create("force_close_window");
        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    }
    else if (name == "pca_createshortcut") {
        CefString lnkname = arguments[0]->GetStringValue();
        CefString url = arguments[1]->GetStringValue();
        auto msg = CefProcessMessage::Create("createshortcut");
        auto args = msg->GetArgumentList();
        args->SetString(0, lnkname);
        args->SetString(1, url);
        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    }
    else if (name == "pca_savesystemsetting") {
        CefString setting = arguments[0]->GetStringValue();
        //app_config::getInstance()->save(setting.ToString());

        auto msg = CefProcessMessage::Create("savesystemsetting");
        auto args = msg->GetArgumentList();
        args->SetString(0, setting);
        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    }
    else if (name == "pca_getsystemsetting") {
        //auto content = app_config::getInstance()->load_systemsetting();
        //retval = CefV8Value::CreateString(content);
        //auto msg = CefProcessMessage::Create("systemsetting");
        //CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    }
    else if (name == "pca_getinfo") {
        retval = CefV8Value::CreateString("");
    }
    else if (name == "pca_shortcutexist") {
        //CefString shortcut = arguments[0]->GetStringValue();
        //if (shortcut_exist(shortcut.c_str())) {
        //    retval = CefV8Value::CreateBool(true);
        //}
        //else {
        //    retval = CefV8Value::CreateBool(false);
        //}
    }
    else if (name == "pca_saveuserdata") {
        //CefString data = arguments[0]->GetStringValue();
        //app_config::getInstance()->save_userdata(data);
    }
    else if (name == "pca_loaduserdata") {
        //auto data = app_config::getInstance()->load_userdata();
        //retval = CefV8Value::CreateString(data);
    }
    else if (name == "pca_mouseevent") {
        CefString data = arguments[0]->GetStringValue();

        auto msg = CefProcessMessage::Create("pca_mouseevent");
        auto args = msg->GetArgumentList();
        args->SetString(0, data);
        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
    }
    return true;
}

namespace client {
namespace renderer {

namespace {

// Must match the value in client_handler.cc.
const char kFocusedNodeChangedMessage[] = "ClientRenderer.FocusedNodeChanged";

class ClientRenderDelegate : public ClientAppRenderer::Delegate {
 public:
  ClientRenderDelegate() : last_node_is_editable_(false) {}

  void OnRenderThreadCreated(CefRefPtr<ClientAppRenderer> app,
                             CefRefPtr<CefListValue> extra_info) OVERRIDE {
    if (CefCrashReportingEnabled()) {
      // Set some crash keys for testing purposes. Keys must be defined in the
      // "crash_reporter.cfg" file. See cef_crash_util.h for details.

        CefSetCrashKeyValue("code", "9527");
        CefSetCrashKeyValue("desc", "pcacrash");
        CefSetCrashKeyValue("id", "3");
        CefSetCrashKeyValue("project", "g");
        CefSetCrashKeyValue("msg", R"({"version":"10.11.6.0","process":"cef"})");
        CefSetCrashKeyValue("token", "83f30181e862eb75fb266b522b51ceb6");
    }
  }

  void OnWebKitInitialized(CefRefPtr<ClientAppRenderer> app) OVERRIDE {
    // Create the renderer-side router for query handling.
    CefMessageRouterConfig config;
    message_router_ = CefMessageRouterRendererSide::Create(config);

  }

  void OnContextCreated(CefRefPtr<ClientAppRenderer> app,
                        CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) OVERRIDE {
    message_router_->OnContextCreated(browser, frame, context);

    CefRefPtr<CefV8Value> object = context->GetGlobal();

    CefRefPtr<pca_v8handler> handler = new pca_v8handler();
    CefRefPtr<CefV8Value> func_mouseevent = CefV8Value::CreateFunction("pca_mouseevent", handler);
    CefRefPtr<CefV8Value> func_close = CefV8Value::CreateFunction("pca_close", handler);
    CefRefPtr<CefV8Value> func_force_close = CefV8Value::CreateFunction("pca_force_close", handler);
    CefRefPtr<CefV8Value> func_min = CefV8Value::CreateFunction("pca_min", handler);
    CefRefPtr<CefV8Value> func_max = CefV8Value::CreateFunction("pca_max", handler);
    CefRefPtr<CefV8Value> func_restore = CefV8Value::CreateFunction("pca_restore", handler);
    CefRefPtr<CefV8Value> func_fullscreen = CefV8Value::CreateFunction("pca_fullscreen", handler);
    CefRefPtr<CefV8Value> func_mute = CefV8Value::CreateFunction("pca_mute", handler);
    CefRefPtr<CefV8Value> func_createshortcut = CefV8Value::CreateFunction("pca_createshortcut", handler);
    CefRefPtr<CefV8Value> func_openwebbrowser = CefV8Value::CreateFunction("pca_openwebbrowser", handler);
    CefRefPtr<CefV8Value> func_opennewtab = CefV8Value::CreateFunction("pca_opennewtab", handler);
    CefRefPtr<CefV8Value> func_savesystemsetting = CefV8Value::CreateFunction("pca_savesystemsetting", handler);
    CefRefPtr<CefV8Value> func_getsystemsetting = CefV8Value::CreateFunction("pca_getsystemsetting", handler);
    CefRefPtr<CefV8Value> func_getinfo = CefV8Value::CreateFunction("pca_getinfo", handler);
    CefRefPtr<CefV8Value> func_shortcutexist = CefV8Value::CreateFunction("pca_shortcutexist", handler);
    CefRefPtr<CefV8Value> func_saveuserdata = CefV8Value::CreateFunction("pca_saveuserdata", handler);
    CefRefPtr<CefV8Value> func_loaduserdata = CefV8Value::CreateFunction("pca_loaduserdata", handler);

    // Add the "pca_close" function to the "window" object.
    object->SetValue("pca_mouseevent", func_mouseevent, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_close", func_close, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_force_close", func_force_close, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_min", func_min, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_max", func_max, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_restore", func_restore, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_fullscreen", func_fullscreen, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_mute", func_mute, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_createshortcut", func_createshortcut, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_openwebbrowser", func_openwebbrowser, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_opennewtab", func_opennewtab, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_savesystemsetting", func_savesystemsetting, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_getsystemsetting", func_getsystemsetting, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_getinfo", func_getinfo, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_shortcutexist", func_shortcutexist, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_saveuserdata", func_saveuserdata, V8_PROPERTY_ATTRIBUTE_NONE);
    object->SetValue("pca_loaduserdata", func_loaduserdata, V8_PROPERTY_ATTRIBUTE_NONE);
  }

  void OnContextReleased(CefRefPtr<ClientAppRenderer> app,
                         CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) OVERRIDE {
    message_router_->OnContextReleased(browser, frame, context);
  }

  void OnFocusedNodeChanged(CefRefPtr<ClientAppRenderer> app,
                            CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefDOMNode> node) OVERRIDE {
    bool is_editable = (node.get() && node->IsEditable());
    if (is_editable != last_node_is_editable_) {
      // Notify the browser of the change in focused element type.
      last_node_is_editable_ = is_editable;
      CefRefPtr<CefProcessMessage> message =
          CefProcessMessage::Create(kFocusedNodeChangedMessage);
      message->GetArgumentList()->SetBool(0, is_editable);
      frame->SendProcessMessage(PID_BROWSER, message);
    }
  }

  bool OnProcessMessageReceived(CefRefPtr<ClientAppRenderer> app,
                                CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) OVERRIDE {
    return message_router_->OnProcessMessageReceived(browser, frame,
                                                     source_process, message);
  }

 private:
  bool last_node_is_editable_;

  // Handles the renderer side of query routing.
  CefRefPtr<CefMessageRouterRendererSide> message_router_;

  DISALLOW_COPY_AND_ASSIGN(ClientRenderDelegate);
  IMPLEMENT_REFCOUNTING(ClientRenderDelegate);
};

}  // namespace

void CreateDelegates(ClientAppRenderer::DelegateSet& delegates) {
  delegates.insert(new ClientRenderDelegate);
}

}  // namespace renderer
}  // namespace client
