#include "portal.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace ash {

const char PortalDaemon::kBusName[] = "org.freedesktop.impl.portal.desktop.ash";
const char PortalDaemon::kObjectPath[] = "/org/freedesktop/portal/desktop";
const char PortalDaemon::kInterfaceSettings[] = "org.freedesktop.impl.portal.Settings";
const char PortalDaemon::kInterfaceScreenshot[] = "org.freedesktop.impl.portal.Screenshot";
const char PortalDaemon::kInterfaceScreenCast[] = "org.freedesktop.impl.portal.ScreenCast";
const char PortalDaemon::kInterfaceInhibit[] = "org.freedesktop.impl.portal.Inhibit";
const char PortalDaemon::kInterfaceWallpaper[] = "org.freedesktop.impl.portal.Wallpaper";
const char PortalDaemon::kInterfaceGlobalShortcuts[] = "org.freedesktop.impl.portal.GlobalShortcuts";
const char PortalDaemon::kAppearanceNamespace[] = "org.freedesktop.appearance";

Variant Variant::Uint32(dbus_uint32_t value) {
  Variant v;
  v.kind = kUint32;
  v.uint_value = value;
  return v;
}

Variant Variant::String(std::string value) {
  Variant v;
  v.kind = kString;
  v.string_value = std::move(value);
  return v;
}

Variant Variant::Bool(bool value) {
  Variant v;
  v.kind = kBool;
  v.uint_value = value ? 1 : 0;
  return v;
}

Variant Variant::DoubleArray(std::vector<double> value) {
  Variant v;
  v.kind = kDoubleArray;
  v.double_array = std::move(value);
  return v;
}

Variant Variant::StringArray(std::vector<std::string> value) {
  Variant v;
  v.kind = kStringArray;
  v.string_array = std::move(value);
  return v;
}

std::string Variant::ToDebugString() const {
  char buf[64];
  switch (kind) {
    case kEmpty:
      return "(empty)";
    case kUint32:
      std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned int>(uint_value));
      return buf;
    case kString:
      return "'" + string_value + "'";
    case kBool:
      return uint_value ? "true" : "false";
    case kDoubleArray: {
      std::string out = "[";
      for (size_t i = 0; i < double_array.size(); ++i) {
        if (i != 0) out += ", ";
        std::snprintf(buf, sizeof(buf), "%.2f", double_array[i]);
        out += buf;
      }
      return out + "]";
    }
    case kStringArray: {
      std::string out = "[";
      for (size_t i = 0; i < string_array.size(); ++i) {
        if (i != 0) out += ", ";
        out += "'" + string_array[i] + "'";
      }
      return out + "]";
    }
  }
  return "(unknown)";
}

const char* VariantTypeSignature(const Variant& value) {
  switch (value.kind) {
    case Variant::kEmpty:
      return nullptr;
    case Variant::kUint32:
      return "u";
    case Variant::kString:
      return "s";
    case Variant::kBool:
      return "b";
    case Variant::kDoubleArray:
      return "ad";
    case Variant::kStringArray:
      return "as";
  }
  return nullptr;
}

void AppendVariantValue(DBusMessageIter* parent, const Variant& value) {
  switch (value.kind) {
    case Variant::kEmpty:
      break;
    case Variant::kUint32:
      dbus_message_iter_append_basic(parent, DBUS_TYPE_UINT32, &value.uint_value);
      break;
    case Variant::kString: {
      const char* s = value.string_value.c_str();
      dbus_message_iter_append_basic(parent, DBUS_TYPE_STRING, &s);
      break;
    }
    case Variant::kBool: {
      dbus_bool_t b = (value.uint_value != 0);
      dbus_message_iter_append_basic(parent, DBUS_TYPE_BOOLEAN, &b);
      break;
    }
    case Variant::kDoubleArray: {
      DBusMessageIter arr;
      dbus_message_iter_open_container(parent, DBUS_TYPE_ARRAY, "d", &arr);
      for (double d : value.double_array) dbus_message_iter_append_basic(&arr, DBUS_TYPE_DOUBLE, &d);
      dbus_message_iter_close_container(parent, &arr);
      break;
    }
    case Variant::kStringArray: {
      DBusMessageIter arr;
      dbus_message_iter_open_container(parent, DBUS_TYPE_ARRAY, "s", &arr);
      for (const std::string& s : value.string_array) {
        const char* p = s.c_str();
        dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &p);
      }
      dbus_message_iter_close_container(parent, &arr);
      break;
    }
  }
}

// ---- appearance.json ----

std::string PortalDaemon::ReadFileToString(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string PortalDaemon::AppearanceFilePath() {
  const char* home = std::getenv("HOME");
  return std::string(home ? home : "") + "/.config/ash-de/appearance.json";
}

std::string PortalDaemon::JsonValueText(const std::string& json, const std::string& key) {
  const std::string pattern = "\"" + key + "\"";
  size_t pos = json.find(pattern);
  if (pos == std::string::npos) return "";
  size_t colon = json.find(':', pos + pattern.size());
  if (colon == std::string::npos) return "";
  size_t v = colon + 1;
  while (v < json.size() && std::isspace(static_cast<unsigned char>(json[v]))) ++v;
  if (v >= json.size()) return "";
  if (json[v] == '\"') {
    size_t e = v + 1;
    while (e < json.size()) {
      if (json[e] == '\\' && e + 1 < json.size()) {
        e += 2;
        continue;
      }
      if (json[e] == '\"') break;
      ++e;
    }
    if (e >= json.size()) return "";
    std::string out;
    for (size_t i = v + 1; i < e; ++i) {
      if (json[i] == '\\' && i + 1 < e) {
        ++i;
        out.push_back(json[i]);
      } else {
        out.push_back(json[i]);
      }
    }
    return out;
  }
  size_t e = v;
  while (e < json.size() && std::isspace(static_cast<unsigned char>(json[e])) == 0 &&
         std::strchr(",}]", json[e]) == nullptr) {
    ++e;
  }
  return json.substr(v, e - v);
}

void PortalDaemon::LoadAppearance() {
  std::string json = ReadFileToString(AppearanceFilePath());

  std::string scheme_text;
  for (const char* key : {"color-scheme", "colorScheme"}) {
    scheme_text = JsonValueText(json, key);
    if (!scheme_text.empty()) break;
  }
  int scheme = 0;
  if (scheme_text == "dark") scheme = 2;
  else if (scheme_text == "light") scheme = 1;
  else if (!scheme_text.empty()) scheme = std::atoi(scheme_text.c_str());

  std::string accent_text;
  for (const char* key : {"accent-color", "accentColor"}) {
    accent_text = JsonValueText(json, key);
    if (!accent_text.empty()) break;
  }

  std::string contrast_text = JsonValueText(json, "contrast");

  color_scheme_ = (scheme == 1 || scheme == 2) ? scheme : 2;
  accent_color_ = accent_text.empty() ? std::string("blue") : accent_text;
  int contrast = 0;
  if (contrast_text.empty() || contrast_text == "none") contrast = 0;
  else if (contrast_text == "high") contrast = 1;
  else if (contrast_text == "low") contrast = 2;
  else contrast = std::atoi(contrast_text.c_str());
  contrast_ = (contrast >= 0 && contrast <= 2) ? contrast : 0;
}

void PortalDaemon::Initialize() { LoadAppearance(); }

void PortalDaemon::PollAppearance(std::vector<std::string>* changed) {
  if (changed != nullptr) changed->clear();
  int old_scheme = color_scheme_;
  std::string old_accent = accent_color_;
  int old_contrast = contrast_;
  LoadAppearance();
  if (changed != nullptr) {
    if (color_scheme_ != old_scheme) changed->push_back("color-scheme");
    if (accent_color_ != old_accent) changed->push_back("accent-color");
    if (contrast_ != old_contrast) changed->push_back("contrast");
  }
}

void PortalDaemon::SetConnection(DBusConnection* connection) { connection_ = connection; }

void PortalDaemon::EmitSettingChanged(const std::string& namespace_name, const std::string& key,
                                       const Variant& value) {
  if (connection_ == nullptr) return;
  DBusMessage* msg = dbus_message_new_signal(kObjectPath, kInterfaceSettings, "SettingChanged");
  if (msg == nullptr) return;
  DBusMessageIter iter;
  dbus_message_iter_init_append(msg, &iter);
  const char* ns = namespace_name.c_str();
  const char* k = key.c_str();
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &ns);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &k);
  DBusMessageIter var;
  dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, VariantTypeSignature(value), &var);
  AppendVariantValue(&var, value);
  dbus_message_iter_close_container(&iter, &var);
  dbus_connection_send(connection_, msg, nullptr);
  dbus_message_unref(msg);
  dbus_connection_flush(connection_);
}

// ---- Settings ----

Variant PortalDaemon::SettingsRead(const std::string& ns, const std::string& key) {
  if (ns != kAppearanceNamespace) return Variant();
  if (key == "color-scheme") return Variant::Uint32(color_scheme_);
  if (key == "accent-color") return Variant::String(accent_color_);
  if (key == "contrast") return Variant::Uint32(contrast_);
  return Variant();
}

Dict PortalDaemon::SettingsReadAll(const std::string& ns) {
  Dict dict;
  if (ns == kAppearanceNamespace) {
    dict["color-scheme"] = Variant::Uint32(color_scheme_);
    dict["accent-color"] = Variant::String(accent_color_);
    dict["contrast"] = Variant::Uint32(contrast_);
  }
  return dict;
}

// ---- Screenshot ----

Dict PortalDaemon::Screenshot() { return Dict(); }

Dict PortalDaemon::PickColor() {
  Dict dict;
  dict["color"] = Variant::DoubleArray({0.5, 0.5, 0.5});
  return dict;
}

// ---- ScreenCast ----

std::string PortalDaemon::ScreenCastCreateSession() {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "/org/freedesktop/portal/screencast/session/%u", next_screencast_session_id_++);
  return buf;
}

Dict PortalDaemon::ScreenCastSelectSources(const std::string& session_id) {
  (void)session_id;
  return Dict();
}

Dict PortalDaemon::ScreenCastStart(const std::string& session_id, const std::string& parent_window_id) {
  (void)session_id;
  (void)parent_window_id;
  Dict dict;
  dict["pipe_wire_node_id"] = Variant::Uint32(next_pipe_wire_node_id_++);
  return dict;
}

// ---- Inhibit ----

dbus_uint32_t PortalDaemon::Inhibit(const std::string& app_id, const std::string& reason,
                                    const Dict& options) {
  (void)app_id;
  (void)reason;
  dbus_uint32_t id = next_inhibit_id_++;
  static const char* kFlagKeys[] = {"idle", "suspend", "lock"};
  int flags = 0;
  for (const char* key : kFlagKeys) {
    auto it = options.find(key);
    if (it != options.end() && it->second.kind == Variant::kBool && it->second.uint_value != 0) ++flags;
  }
  if (flags > 0) ++active_inhibitors_;
  return id;
}

// ---- Wallpaper ----

void PortalDaemon::SetWallpaperURI(const std::string& parent_window_id, const std::string& uri,
                                   const Dict& options) {
  (void)parent_window_id;
  (void)options;
  wallpaper_uri_ = uri;
}

// ---- GlobalShortcuts ----

std::string PortalDaemon::GlobalShortcutsCreateSession(const std::string& app_id) {
  (void)app_id;
  char buf[96];
  std::snprintf(buf, sizeof(buf), "/org/freedesktop/portal/global-shortcut/session/%u",
                next_globalshortcuts_session_id_++);
  return buf;
}

dbus_uint32_t PortalDaemon::GlobalShortcutsBindShortcuts(const std::string& session_id,
                                                          const std::string& app_id,
                                                          const std::vector<Shortcut>& shortcuts,
                                                          dbus_uint32_t flags) {
  (void)session_id;
  (void)flags;
  dbus_uint32_t bound = 0;
  for (const Shortcut& s : shortcuts) {
    Shortcut entry = s;
    entry.app_id = app_id;
    globalshortcuts_shortcuts_.push_back(entry);
    ++bound;
  }
  return bound;
}

std::vector<Shortcut> PortalDaemon::GlobalShortcutsListShortcuts(const std::string& app_id) {
  std::vector<Shortcut> out;
  for (const Shortcut& s : globalshortcuts_shortcuts_) {
    if (s.app_id == app_id) out.push_back(s);
  }
  return out;
}

// ---- Introspection ----

std::string PortalDaemon::GenerateIntrospectionXML() const {
  std::string xml = "<node>\n";
  xml += "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n";
  xml += "    <method name=\"Introspect\">\n";
  xml += "      <arg name=\"xml_data\" type=\"s\" direction=\"out\"/>\n";
  xml += "    </method>\n";
  xml += "  </interface>\n";
  xml += "  <interface name=\"org.freedesktop.impl.portal.Settings\">\n";
  xml += "    <method name=\"ReadAll\">\n";
  xml += "      <arg name=\"namespace_name\" type=\"s\" direction=\"in\"/>\n";
  xml += "      <arg name=\"settings\" type=\"a{sv}\" direction=\"out\"/>\n";
  xml += "    </method>\n";
  xml += "    <method name=\"Read\">\n";
  xml += "      <arg name=\"namespace_name\" type=\"s\" direction=\"in\"/>\n";
  xml += "      <arg name=\"key\" type=\"s\" direction=\"in\"/>\n";
  xml += "      <arg name=\"value\" type=\"v\" direction=\"out\"/>\n";
  xml += "    </method>\n";
  xml += "    <signal name=\"SettingChanged\">\n";
  xml += "      <arg name=\"namespace_name\" type=\"s\"/>\n";
  xml += "      <arg name=\"key\" type=\"s\"/>\n";
  xml += "      <arg name=\"value\" type=\"v\"/>\n";
  xml += "    </signal>\n";
  xml += "  </interface>\n";
  xml += "  <interface name=\"org.freedesktop.impl.portal.Screenshot\">\n";
  xml += "    <method name=\"Screenshot\">\n";
  xml += "      <arg name=\"parent_window_id\" type=\"s\" direction=\"in\"/>\n";
  xml += "      <arg name=\"options\" type=\"a{sv}\" direction=\"in\"/>\n";
  xml += "      <arg name=\"screenshot\" type=\"a{sv}\" direction=\"out\"/>\n";
  xml += "    </method>\n";
  xml += "    <method name=\"PickColor\">\n";
  xml += "      <arg name=\"parent_window_id\" type=\"s\" direction=\"in\"/>\n";
  xml += "      <arg name=\"options\" type=\"a{sv}\" direction=\"in\"/>\n";
  xml += "      <arg name=\"color\" type=\"a{sv}\" direction=\"out\"/>\n";
  xml += "    </method>\n";
  xml += "  </interface>\n";
  xml += "  <interface name=\"org.freedesktop.impl.portal.ScreenCast\">\n";
  xml += "    <method name=\"CreateSession\">\n";
  xml += "      <arg name=\"options\" type=\"a{sv}\" direction=\"in\"/>\n";
  xml += "      <arg name=\"session_id\" type=\"o\" direction=\"out\"/>\n";
  xml += "    </method>\n";
  xml += "    <method name=\"SelectSources\">\n";
  xml += "      <arg name=\"session_id\" type=\"o\" direction=\"in\"/>\n";
  xml += "      <arg name=\"sources\" type=\"a{sv}\" direction=\"out\"/>\n";
  xml += "    </method>\n";
  xml += "    <method name=\"Start\">\n";
  xml += "      <arg name=\"session_id\" type=\"o\" direction=\"in\"/>\n";
  xml += "      <arg name=\"parent_window_id\" type=\"o\" direction=\"in\"/>\n";
  xml += "      <arg name=\"streams\" type=\"a{sv}\" direction=\"out\"/>\n";
  xml += "    </method>\n";
  xml += "  </interface>\n";
  xml += "  <interface name=\"org.freedesktop.impl.portal.Inhibit\">\n";
  xml += "    <method name=\"Inhibit\">\n";
  xml += "      <arg name=\"app_id\" type=\"s\" direction=\"in\"/>\n";
  xml += "      <arg name=\"reason\" type=\"s\" direction=\"in\"/>\n";
  xml += "      <arg name=\"options\" type=\"a{sv}\" direction=\"in\"/>\n";
  xml += "      <arg name=\"inhibit_id\" type=\"u\" direction=\"out\"/>\n";
  xml += "    </method>\n";
  xml += "  </interface>\n";
  xml += "  <interface name=\"org.freedesktop.impl.portal.Wallpaper\">\n";
  xml += "    <method name=\"SetWallpaperURI\">\n";
  xml += "      <arg name=\"parent_window_id\" type=\"s\" direction=\"in\"/>\n";
  xml += "      <arg name=\"uri\" type=\"s\" direction=\"in\"/>\n";
  xml += "      <arg name=\"options\" type=\"a{sv}\" direction=\"in\"/>\n";
  xml += "    </method>\n";
  xml += "  </interface>\n";
  xml += "  <interface name=\"org.freedesktop.impl.portal.GlobalShortcuts\">\n";
  xml += "    <method name=\"CreateSession\">\n";
  xml += "      <arg name=\"app_id\" type=\"s\" direction=\"in\"/>\n";
  xml += "      <arg name=\"session_id\" type=\"o\" direction=\"out\"/>\n";
  xml += "    </method>\n";
  xml += "    <method name=\"BindShortcuts\">\n";
  xml += "      <arg name=\"session_id\" type=\"o\" direction=\"in\"/>\n";
  xml += "      <arg name=\"app_id\" type=\"s\" direction=\"in\"/>\n";
  xml += "      <arg name=\"shortcuts\" type=\"a(ss)\" direction=\"in\"/>\n";
  xml += "      <arg name=\"flags\" type=\"u\" direction=\"in\"/>\n";
  xml += "      <arg name=\"bound\" type=\"u\" direction=\"out\"/>\n";
  xml += "    </method>\n";
  xml += "    <method name=\"ListShortcuts\">\n";
  xml += "      <arg name=\"app_id\" type=\"s\" direction=\"in\"/>\n";
  xml += "      <arg name=\"shortcuts\" type=\"a(ss)\" direction=\"out\"/>\n";
  xml += "    </method>\n";
  xml += "  </interface>\n";
  xml += "</node>\n";
  return xml;
}

}  // namespace ash
