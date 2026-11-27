// PortalDaemon: implementation of the xdg-desktop-portal backends for the
// ChromiumOS Ash DE, exported on the session bus as
// org.freedesktop.impl.portal.desktop.ash at /org/freedesktop/portal/desktop.

#ifndef XDG_DESKTOP_PORTAL_ASH_PORTAL_H
#define XDG_DESKTOP_PORTAL_ASH_PORTAL_H

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <dbus/dbus.h>

namespace ash {

// A D-Bus independent value used to describe portal method replies.
struct Variant {
  enum Kind {
    kEmpty,
    kUint32,
    kString,
    kBool,
    kDoubleArray,
    kStringArray,
  };

  Kind kind = kEmpty;
  dbus_uint32_t uint_value = 0;
  std::string string_value;
  std::vector<double> double_array;
  std::vector<std::string> string_array;

  static Variant Uint32(dbus_uint32_t value);
  static Variant String(std::string value);
  static Variant Bool(bool value);
  static Variant DoubleArray(std::vector<double> value);
  static Variant StringArray(std::vector<std::string> value);

  // Short text form used by the self-test output.
  std::string ToDebugString() const;
};

// An "a{sv}" dictionary mapping strings to Variants.
using Dict = std::map<std::string, Variant>;

// One global shortcut entry, as returned by ListShortcuts().
struct Shortcut {
  std::string app_id;
  std::string id;
  std::string description;
};

// Returns the D-Bus signature of the value inside a Variant ("u", "s", "ad",
// ...).  Returns NULL for an empty Variant.
const char* VariantTypeSignature(const Variant& value);

// Appends the value held by "value" to the open container "parent".
void AppendVariantValue(DBusMessageIter* parent, const Variant& value);

class PortalDaemon {
 public:
  static const char kBusName[];              // org.freedesktop.impl.portal.desktop.ash
  static const char kObjectPath[];           // /org/freedesktop/portal/desktop
  static const char kInterfaceSettings[];    // org.freedesktop.impl.portal.Settings
  static const char kInterfaceScreenshot[];  // org.freedesktop.impl.portal.Screenshot
  static const char kInterfaceScreenCast[];  // org.freedesktop.impl.portal.ScreenCast
  static const char kInterfaceInhibit[];     // org.freedesktop.impl.portal.Inhibit
  static const char kInterfaceWallpaper[];   // org.freedesktop.impl.portal.Wallpaper
  static const char kInterfaceGlobalShortcuts[];  // org.freedesktop.impl.portal.GlobalShortcuts
  static const char kAppearanceNamespace[];  // org.freedesktop.appearance

  PortalDaemon() = default;

  // Loads the appearance settings (from ~/.config/ash-de/appearance.json).
  void Initialize();

  // Re-reads the appearance file; appends the keys whose values changed to
  // "changed" (may be NULL).
  void PollAppearance(std::vector<std::string>* changed);

  void SetConnection(DBusConnection* connection);

  // Emits the SettingChanged signal on the Settings interface.
  void EmitSettingChanged(const std::string& namespace_name, const std::string& key, const Variant& value);

  // ---- Settings ----
  Variant SettingsRead(const std::string& namespace_name, const std::string& key);
  Dict SettingsReadAll(const std::string& namespace_name);

  // ---- Screenshot ----
  Dict Screenshot();
  Dict PickColor();

  // ---- ScreenCast ----
  std::string ScreenCastCreateSession();
  Dict ScreenCastSelectSources(const std::string& session_id);
  Dict ScreenCastStart(const std::string& session_id, const std::string& parent_window_id);

  // ---- Inhibit ----
  dbus_uint32_t Inhibit(const std::string& app_id, const std::string& reason, const Dict& options);

  // ---- Wallpaper ----
  void SetWallpaperURI(const std::string& parent_window_id, const std::string& uri, const Dict& options);

  // ---- GlobalShortcuts ----
  std::string GlobalShortcutsCreateSession(const std::string& app_id);
  dbus_uint32_t GlobalShortcutsBindShortcuts(const std::string& session_id, const std::string& app_id,
                                             const std::vector<Shortcut>& shortcuts, dbus_uint32_t flags);
  std::vector<Shortcut> GlobalShortcutsListShortcuts(const std::string& app_id);

  // Introspection XML served by org.freedesktop.DBus.Introspectable.Introspect.
  std::string GenerateIntrospectionXML() const;

  dbus_uint32_t color_scheme() const { return color_scheme_; }
  const std::string& accent_color() const { return accent_color_; }
  dbus_uint32_t contrast() const { return contrast_; }
  dbus_uint32_t active_inhibitors() const { return active_inhibitors_; }

 private:
  void LoadAppearance();
  static std::string ReadFileToString(const std::string& path);
  static std::string JsonValueText(const std::string& json, const std::string& key);
  static std::string AppearanceFilePath();

  DBusConnection* connection_ = nullptr;

  int color_scheme_ = 2;             // 1 = prefer-dark, 2 = prefer-light
  std::string accent_color_ = "blue";
  int contrast_ = 0;                 // 0 = none, 1 = high, 2 = low

  dbus_uint32_t next_screencast_session_id_ = 1;
  dbus_uint32_t next_pipe_wire_node_id_ = 100;
  dbus_uint32_t next_inhibit_id_ = 1;
  dbus_uint32_t next_globalshortcuts_session_id_ = 1;
  dbus_uint32_t active_inhibitors_ = 0;
  std::vector<Shortcut> globalshortcuts_shortcuts_;
  std::string wallpaper_uri_;
};

}  // namespace ash

#endif  // XDG_DESKTOP_PORTAL_ASH_PORTAL_H
