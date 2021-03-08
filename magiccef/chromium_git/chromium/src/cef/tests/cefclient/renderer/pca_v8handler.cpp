#include "pca_v8handler.h"
#include <iostream>
#include <shellapi.h>
//#include "app_config.h"
//#include "shortcut_util.h"

//pca_v8handler::pca_v8handler() {
//}
//
//bool pca_v8handler::Execute(const CefString& name, CefRefPtr<CefV8Value> object, const CefV8ValueList& arguments, CefRefPtr<CefV8Value>& retval, CefString& exception)
//{
//    if (name == "pca_openwebbrowser") {
//        if (arguments.size() == 1 && arguments[0]->IsString()) {
//            CefString cefurl = arguments[0]->GetStringValue();
//            auto msg = CefProcessMessage::Create("openwebbrowser");
//            auto args = msg->GetArgumentList();
//            args->SetString(0, cefurl);
//
//            CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//        }
//    }
//    else if (name == "pca_opennewtab") {
//        CefString cefurl = arguments[0]->GetStringValue();
//        auto msg = CefProcessMessage::Create("opennewtab");
//        auto args = msg->GetArgumentList();
//        args->SetString(0, cefurl);
//        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//    }
//    else if (name == "pca_mute") {
//        bool ismute = arguments[0]->GetBoolValue();
//        auto msg = CefProcessMessage::Create("mute");
//        auto args = msg->GetArgumentList();
//        args->SetBool(0, ismute);
//        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//    }
//    else if (name == "pca_min") {
//        auto msg = CefProcessMessage::Create("min_window");
//        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//    }
//    else if (name == "pca_max") {
//        auto msg = CefProcessMessage::Create("max_window");
//        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//    }
//    else if (name == "pca_restore") {
//        auto msg = CefProcessMessage::Create("restore_window");
//        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//    }
//    else if (name == "pca_fullscreen") {
//        auto msg = CefProcessMessage::Create("fullscreen");
//        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//    }
//    else if (name == "pca_close") {
//        auto msg = CefProcessMessage::Create("close_window");
//        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//    }
//    else if (name == "pca_force_close") {
//        auto msg = CefProcessMessage::Create("force_close_window");
//        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//    }
//    else if (name == "pca_createshortcut") {
//        CefString lnkname = arguments[0]->GetStringValue();
//        CefString url = arguments[1]->GetStringValue();
//        auto msg = CefProcessMessage::Create("createshortcut");
//        auto args = msg->GetArgumentList();
//        args->SetString(0, lnkname);
//        args->SetString(1, url);
//        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//    }
//    else if (name == "pca_savesystemsetting") {
//        CefString setting = arguments[0]->GetStringValue();
//        //app_config::getInstance()->save(setting.ToString());
//
//        auto msg = CefProcessMessage::Create("savesystemsetting");
//        auto args = msg->GetArgumentList();
//        args->SetString(0, setting);
//        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//    }
//    else if (name == "pca_getsystemsetting") {
//        //auto content = app_config::getInstance()->load_systemsetting();
//        //retval = CefV8Value::CreateString(content);
//        //auto msg = CefProcessMessage::Create("systemsetting");
//        //CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//    }
//    else if (name == "pca_getinfo") {
//        retval = CefV8Value::CreateString("");
//    }
//    else if (name == "pca_shortcutexist") {
//        //CefString shortcut = arguments[0]->GetStringValue();
//        //if (shortcut_exist(shortcut.c_str())) {
//        //    retval = CefV8Value::CreateBool(true);
//        //}
//        //else {
//        //    retval = CefV8Value::CreateBool(false);
//        //}
//    }
//    else if (name == "pca_saveuserdata") {
//        //CefString data = arguments[0]->GetStringValue();
//        //app_config::getInstance()->save_userdata(data);
//    }
//    else if (name == "pca_loaduserdata") {
//        //auto data = app_config::getInstance()->load_userdata();
//        //retval = CefV8Value::CreateString(data);
//    }
//    else if (name == "pca_mousemove") {
//        CefString data = arguments[0]->GetStringValue();
//
//        auto msg = CefProcessMessage::Create("pca_mousemove");
//        auto args = msg->GetArgumentList();
//        args->SetString(0, data);
//        CefV8Context::GetCurrentContext()->GetBrowser()->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
//    }
//    return true;
//}