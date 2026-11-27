#include "daemon.h"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace ash {
namespace {

volatile sig_atomic_t g_running = 1;

void HandleSignal(int) { g_running = 0; }

// ---- D-Bus plumbing ----

std::string ParseStringArg(DBusMessageIter* iter) {
  const char* s = nullptr;
  dbus_message_iter_get_basic(iter, &s);
  return s != nullptr ? std::string(s) : std::string();
}

// Parses the "a{sv}" argument at "iter".
Dict ParseDictArg(DBusMessageIter* iter) {
  Dict dict;
  if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY) return dict;
  DBusMessageIter arr;
  dbus_message_iter_recurse(iter, &arr);
  while (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_INVALID) {
    DBusMessageIter entry;
    dbus_message_iter_recurse(&arr, &entry);
    const char* key = nullptr;
    dbus_message_iter_get_basic(&entry, &key);
    dbus_message_iter_next(&entry);
    if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
      DBusMessageIter var;
      dbus_message_iter_recurse(&entry, &var);
      Variant v;
      switch (dbus_message_iter_get_arg_type(&var)) {
        case DBUS_TYPE_UINT32: {
          dbus_uint32_t x = 0;
          dbus_message_iter_get_basic(&var, &x);
          v = Variant::Uint32(x);
          break;
        }
        case DBUS_TYPE_STRING: {
          const char* s = nullptr;
          dbus_message_iter_get_basic(&var, &s);
          if (s != nullptr) v = Variant::String(s);
          break;
        }
        case DBUS_TYPE_BOOLEAN: {
          dbus_bool_t b = 0;
          dbus_message_iter_get_basic(&var, &b);
          v = Variant::Bool(b != 0);
          break;
        }
        default:
          break;
      }
      if (key != nullptr) dict[std::string(key)] = v;
    }
    dbus_message_iter_next(&arr);
  }
  return dict;
}

// Appends a "key"/value pair as an a{sv} entry of the open array "arr".
void AppendDictEntry(DBusMessageIter* arr, const char* key, const Variant& value) {
  DBusMessageIter entry;
  dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
  const char* k = key;
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &k);
  DBusMessageIter var;
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, VariantTypeSignature(value), &var);
  AppendVariantValue(&var, value);
  dbus_message_iter_close_container(&entry, &var);
  dbus_message_iter_close_container(arr, &entry);
}

// Replies with an "a{sv}" dict.
void AppendDictReply(DBusMessage* reply, const Dict& dict) {
  DBusMessageIter iter;
  dbus_message_iter_init_append(reply, &iter);
  DBusMessageIter arr;
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &arr);
  for (const auto& kv : dict) AppendDictEntry(&arr, kv.first.c_str(), kv.second);
  dbus_message_iter_close_container(&iter, &arr);
}

// Replies with a single out argument of type "v" (the Variant).
void AppendReadReply(DBusMessage* reply, const Variant& value_in) {
  Variant value = value_in;
  const char* sig = VariantTypeSignature(value);
  if (sig == nullptr) {
    value = Variant::String("");
    sig = VariantTypeSignature(value);
  }
  DBusMessageIter iter;
  dbus_message_iter_init_append(reply, &iter);
  DBusMessageIter var;
  dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, sig, &var);
  AppendVariantValue(&var, value);
  dbus_message_iter_close_container(&iter, &var);
}

void SendReply(DBusConnection* conn, DBusMessage* reply) {
  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
}

void ReplyError(DBusConnection* conn, DBusMessage* msg) {
  std::string text = "no such method: " + std::string(dbus_message_get_member(msg) ? dbus_message_get_member(msg) : "");
  DBusMessage* err = dbus_message_new_error(msg, "org.freedesktop.DBus.Error.UnknownMethod", text.c_str());
  if (err != nullptr) {
    dbus_connection_send(conn, err, nullptr);
    dbus_message_unref(err);
  }
}

DBusHandlerResult OnMessage(DBusConnection* conn, DBusMessage* msg, void* user_data) {
  PortalDaemon* daemon = static_cast<PortalDaemon*>(user_data);
  const std::string iface = dbus_message_get_interface(msg) ? dbus_message_get_interface(msg) : "";
  const std::string member = dbus_message_get_member(msg) ? dbus_message_get_member(msg) : "";

  // ---- org.freedesktop.DBus.Peer.Ping ----
  if (iface == "org.freedesktop.DBus.Peer" && member == "Ping") {
    SendReply(conn, dbus_message_new_method_return(msg));
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  // ---- org.freedesktop.DBus.Introspectable.Introspect ----
  if (iface == "org.freedesktop.DBus.Introspectable" && member == "Introspect") {
    DBusMessage* reply = dbus_message_new_method_return(msg);
    std::string xml = daemon->GenerateIntrospectionXML();
    const char* data = xml.c_str();
    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &data);
    SendReply(conn, reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  // ---- org.freedesktop.impl.portal.Settings ----
  if (iface == PortalDaemon::kInterfaceSettings && (member == "ReadAll" || member == "Read")) {
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusMessageIter iter;
    dbus_message_iter_init(msg, &iter);
    if (member == "ReadAll") {
      AppendDictReply(reply, daemon->SettingsReadAll(ParseStringArg(&iter)));
    } else {
      std::string ns = ParseStringArg(&iter);
      dbus_message_iter_next(&iter);
      AppendReadReply(reply, daemon->SettingsRead(ns, ParseStringArg(&iter)));
    }
    SendReply(conn, reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  // ---- org.freedesktop.impl.portal.Screenshot ----
  if (iface == PortalDaemon::kInterfaceScreenshot && (member == "Screenshot" || member == "PickColor")) {
    DBusMessage* reply = dbus_message_new_method_return(msg);
    if (member == "Screenshot") {
      AppendDictReply(reply, daemon->Screenshot());
    } else {
      AppendDictReply(reply, daemon->PickColor());
    }
    SendReply(conn, reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  // ---- org.freedesktop.impl.portal.ScreenCast ----
  if (iface == PortalDaemon::kInterfaceScreenCast) {
    if (member == "CreateSession") {
      std::string sid = daemon->ScreenCastCreateSession();
      DBusMessage* reply = dbus_message_new_method_return(msg);
      DBusMessageIter iter;
      dbus_message_iter_init_append(reply, &iter);
      const char* s = sid.c_str();
      dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &s);
      SendReply(conn, reply);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (member == "SelectSources") {
      DBusMessageIter iter;
      dbus_message_iter_init(msg, &iter);
      std::string sid = ParseStringArg(&iter);
      DBusMessage* reply = dbus_message_new_method_return(msg);
      AppendDictReply(reply, daemon->ScreenCastSelectSources(sid));
      SendReply(conn, reply);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (member == "Start") {
      DBusMessageIter iter;
      dbus_message_iter_init(msg, &iter);
      std::string sid = ParseStringArg(&iter);
      dbus_message_iter_next(&iter);
      std::string parent = ParseStringArg(&iter);
      DBusMessage* reply = dbus_message_new_method_return(msg);
      AppendDictReply(reply, daemon->ScreenCastStart(sid, parent));
      SendReply(conn, reply);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  }

  // ---- org.freedesktop.impl.portal.Inhibit ----
  if (iface == PortalDaemon::kInterfaceInhibit && member == "Inhibit") {
    DBusMessageIter iter;
    dbus_message_iter_init(msg, &iter);
    std::string app_id = ParseStringArg(&iter);
    dbus_message_iter_next(&iter);
    std::string reason = ParseStringArg(&iter);
    dbus_message_iter_next(&iter);
    Dict options = ParseDictArg(&iter);
    dbus_uint32_t id = daemon->Inhibit(app_id, reason, options);
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusMessageIter riter;
    dbus_message_iter_init_append(reply, &riter);
    dbus_message_iter_append_basic(&riter, DBUS_TYPE_UINT32, &id);
    SendReply(conn, reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  // ---- org.freedesktop.impl.portal.Wallpaper ----
  if (iface == PortalDaemon::kInterfaceWallpaper && member == "SetWallpaperURI") {
    DBusMessageIter iter;
    dbus_message_iter_init(msg, &iter);
    std::string parent = ParseStringArg(&iter);
    dbus_message_iter_next(&iter);
    std::string uri = ParseStringArg(&iter);
    dbus_message_iter_next(&iter);
    Dict options = ParseDictArg(&iter);
    daemon->SetWallpaperURI(parent, uri, options);
    SendReply(conn, dbus_message_new_method_return(msg));
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  // ---- org.freedesktop.impl.portal.GlobalShortcuts ----
  if (iface == PortalDaemon::kInterfaceGlobalShortcuts) {
    if (member == "CreateSession") {
      DBusMessageIter iter;
      dbus_message_iter_init(msg, &iter);
      std::string app_id = ParseStringArg(&iter);
      std::string sid = daemon->GlobalShortcutsCreateSession(app_id);
      DBusMessage* reply = dbus_message_new_method_return(msg);
      DBusMessageIter riter;
      dbus_message_iter_init_append(reply, &riter);
      const char* s = sid.c_str();
      dbus_message_iter_append_basic(&riter, DBUS_TYPE_OBJECT_PATH, &s);
      SendReply(conn, reply);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (member == "BindShortcuts") {
      DBusMessageIter iter;
      dbus_message_iter_init(msg, &iter);
      std::string sid = ParseStringArg(&iter);
      dbus_message_iter_next(&iter);
      std::string app_id = ParseStringArg(&iter);
      dbus_message_iter_next(&iter);
      std::vector<Shortcut> shortcuts;
      if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
        DBusMessageIter arr;
        dbus_message_iter_recurse(&iter, &arr);
        while (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_INVALID) {
          DBusMessageIter st;
          dbus_message_iter_recurse(&arr, &st);
          const char* id = nullptr;
          dbus_message_iter_get_basic(&st, &id);
          dbus_message_iter_next(&st);
          const char* desc = nullptr;
          if (dbus_message_iter_get_arg_type(&st) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&st, &desc);
          }
          Shortcut sc;
          sc.id = id != nullptr ? id : "";
          sc.description = desc != nullptr ? desc : "";
          shortcuts.push_back(sc);
          dbus_message_iter_next(&arr);
        }
      }
      dbus_message_iter_next(&iter);
      dbus_uint32_t flags = 0;
      if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_UINT32) {
        dbus_message_iter_get_basic(&iter, &flags);
      }
      dbus_uint32_t bound = daemon->GlobalShortcutsBindShortcuts(sid, app_id, shortcuts, flags);
      DBusMessage* reply = dbus_message_new_method_return(msg);
      DBusMessageIter riter;
      dbus_message_iter_init_append(reply, &riter);
      dbus_message_iter_append_basic(&riter, DBUS_TYPE_UINT32, &bound);
      SendReply(conn, reply);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (member == "ListShortcuts") {
      DBusMessageIter iter;
      dbus_message_iter_init(msg, &iter);
      std::string app_id = ParseStringArg(&iter);
      std::vector<Shortcut> list = daemon->GlobalShortcutsListShortcuts(app_id);
      DBusMessage* reply = dbus_message_new_method_return(msg);
      DBusMessageIter riter;
      dbus_message_iter_init_append(reply, &riter);
      DBusMessageIter arr;
      dbus_message_iter_open_container(&riter, DBUS_TYPE_ARRAY, "(ss)", &arr);
      for (const Shortcut& sc : list) {
        DBusMessageIter st;
        dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, nullptr, &st);
        const char* id = sc.id.c_str();
        const char* desc = sc.description.c_str();
        dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &id);
        dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &desc);
        dbus_message_iter_close_container(&arr, &st);
      }
      dbus_message_iter_close_container(&riter, &arr);
      SendReply(conn, reply);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  }

  ReplyError(conn, msg);
  return DBUS_HANDLER_RESULT_HANDLED;
}

}  // namespace

int RunDaemon(PortalDaemon* daemon) {
  DBusError err;
  dbus_error_init(&err);
  DBusConnection* conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
  if (conn == nullptr) {
    std::fprintf(stderr, "xdg-desktop-portal-ash: cannot connect to the session bus: %s\n",
                 err.message != nullptr ? err.message : "unknown error");
    dbus_error_free(&err);
    return 1;
  }
  dbus_connection_set_exit_on_disconnect(conn, 0);

  DBusObjectPathVTable vtable;
  std::memset(&vtable, 0, sizeof(vtable));
  vtable.message_function = &OnMessage;
  if (!dbus_connection_register_object_path(conn, PortalDaemon::kObjectPath, &vtable, daemon)) {
    std::fprintf(stderr, "xdg-desktop-portal-ash: failed to register %s\n", PortalDaemon::kObjectPath);
    dbus_connection_close(conn);
    dbus_connection_unref(conn);
    dbus_error_free(&err);
    return 1;
  }
  daemon->SetConnection(conn);

  DBusError name_err;
  dbus_error_init(&name_err);
  dbus_bus_request_name(conn, PortalDaemon::kBusName, DBUS_NAME_FLAG_DO_NOT_QUEUE, &name_err);
  if (dbus_error_is_set(&name_err)) {
    std::fprintf(stderr, "xdg-desktop-portal-ash: warning: %s\n",
                 name_err.message != nullptr ? name_err.message : "could not become the owner of the name");
    dbus_error_free(&name_err);
  }

  daemon->EmitSettingChanged(PortalDaemon::kAppearanceNamespace, "color-scheme",
                             Variant::Uint32(daemon->color_scheme()));
  daemon->EmitSettingChanged(PortalDaemon::kAppearanceNamespace, "accent-color",
                             Variant::String(daemon->accent_color()));
  daemon->EmitSettingChanged(PortalDaemon::kAppearanceNamespace, "contrast",
                             Variant::Uint32(daemon->contrast()));

  std::fprintf(stderr, "xdg-desktop-portal-ash: serving %s on %s\n",
               PortalDaemon::kBusName, PortalDaemon::kObjectPath);

  std::signal(SIGTERM, HandleSignal);
  std::signal(SIGINT, HandleSignal);

  time_t last_poll = std::time(nullptr);
  while (g_running) {
    dbus_connection_read_write(conn, 20);
    while (dbus_connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS) {
    }
    time_t now = std::time(nullptr);
    if (now - last_poll >= 5) {
      last_poll = now;
      std::vector<std::string> changed;
      daemon->PollAppearance(&changed);
      for (const std::string& key : changed) {
        if (key == "color-scheme") {
          daemon->EmitSettingChanged(PortalDaemon::kAppearanceNamespace, key,
                                     Variant::Uint32(daemon->color_scheme()));
        } else if (key == "accent-color") {
          daemon->EmitSettingChanged(PortalDaemon::kAppearanceNamespace, key,
                                     Variant::String(daemon->accent_color()));
        } else if (key == "contrast") {
          daemon->EmitSettingChanged(PortalDaemon::kAppearanceNamespace, key,
                                     Variant::Uint32(daemon->contrast()));
        }
      }
    }
  }

  dbus_bus_release_name(conn, PortalDaemon::kBusName, nullptr);
  dbus_connection_close(conn);
  dbus_connection_unref(conn);
  dbus_error_free(&err);
  return 0;
}

}  // namespace ash
