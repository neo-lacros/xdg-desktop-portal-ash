#include <cstdio>
#include <string>
#include <vector>

#include "daemon.h"
#include "portal.h"

namespace {

int RunSelfTest() {
  ash::PortalDaemon daemon;
  daemon.Initialize();

  std::printf("xdg-desktop-portal-ash self-test\n");

  // ---- Settings.Read ----
  ash::Variant color_scheme = daemon.SettingsRead("org.freedesktop.appearance", "color-scheme");
  std::printf("Settings.Read(\"org.freedesktop.appearance\", \"color-scheme\") = %s\n",
              color_scheme.ToDebugString().c_str());
  if (color_scheme.kind != ash::Variant::kUint32 ||
      (color_scheme.uint_value != 1 && color_scheme.uint_value != 2)) {
    std::printf("FAIL: color-scheme must be 1 (prefer-dark) or 2 (prefer-light)\n");
    return 1;
  }

  // ---- Settings.ReadAll ----
  ash::Dict appearance = daemon.SettingsReadAll("org.freedesktop.appearance");
  std::printf("Settings.ReadAll(\"org.freedesktop.appearance\") = {color-scheme: %u, accent-color: '%s', contrast: %u}\n",
              daemon.color_scheme(), daemon.accent_color().c_str(), daemon.contrast());
  if (appearance.count("color-scheme") != 1 || appearance.count("accent-color") != 1 ||
      appearance.count("contrast") != 1) {
    std::printf("FAIL: Settings.ReadAll must return color-scheme, accent-color and contrast\n");
    return 1;
  }

  // ---- Screenshot ----
  ash::Dict screenshot = daemon.Screenshot();
  std::printf("Screenshot() -> a{sv} with %zu entries\n", screenshot.size());

  // ---- PickColor ----
  ash::Dict pick_color = daemon.PickColor();
  std::printf("PickColor() -> {color: %s}\n", pick_color["color"].ToDebugString().c_str());

  // ---- ScreenCast ----
  std::string session = daemon.ScreenCastCreateSession();
  std::printf("ScreenCast.CreateSession() -> %s\n", session.c_str());
  ash::Dict sources = daemon.ScreenCastSelectSources(session);
  std::printf("ScreenCast.SelectSources(%s) -> a{sv} with %zu entries\n", session.c_str(), sources.size());
  ash::Dict streams = daemon.ScreenCastStart(session, "");
  auto it = streams.find("pipe_wire_node_id");
  dbus_uint32_t node_id = (it != streams.end()) ? it->second.uint_value : 0;
  std::printf("ScreenCast.Start(%s, \"\") -> {pipe_wire_node_id: %u}\n", session.c_str(), node_id);
  if (it == streams.end() || it->second.kind != ash::Variant::kUint32 || node_id == 0) {
    std::printf("FAIL: ScreenCast.Start must return a PipeWire node id\n");
    return 1;
  }

  // ---- Inhibit ----
  dbus_uint32_t inhibit_id = daemon.Inhibit("org.freedesktop.test.self-test", "self-test", ash::Dict());
  std::printf("Inhibit(\"org.freedesktop.test.self-test\", \"self-test\", {}) -> %u\n", inhibit_id);
  if (inhibit_id == 0) {
    std::printf("FAIL: Inhibit must return an inhibitor id\n");
    return 1;
  }

  // ---- GlobalShortcuts ----
  std::string gs_session = daemon.GlobalShortcutsCreateSession("org.freedesktop.test.self-test");
  std::printf("GlobalShortcuts.CreateSession(\"org.freedesktop.test.self-test\") -> %s\n", gs_session.c_str());
  ash::Shortcut sc;
  sc.app_id = "org.freedesktop.test.self-test";
  sc.id = "test-shortcut";
  sc.description = "Save the current document";
  std::vector<ash::Shortcut> bound;
  bound.push_back(sc);
  dbus_uint32_t bound_count =
      daemon.GlobalShortcutsBindShortcuts(gs_session, "org.freedesktop.test.self-test", bound, 0);
  std::printf("GlobalShortcuts.BindShortcuts(%s, ...) -> %u\n", gs_session.c_str(), bound_count);
  if (bound_count != 1) {
    std::printf("FAIL: GlobalShortcuts.BindShortcuts must return the number of bound shortcuts\n");
    return 1;
  }
  std::vector<ash::Shortcut> listed = daemon.GlobalShortcutsListShortcuts("org.freedesktop.test.self-test");
  std::printf("GlobalShortcuts.ListShortcuts(\"org.freedesktop.test.self-test\") -> %zu shortcuts\n", listed.size());
  if (listed.size() != 1 || listed[0].id != sc.id || listed[0].description != sc.description) {
    std::printf("FAIL: GlobalShortcuts.ListShortcuts must return the bound shortcut\n");
    return 1;
  }

  // ---- Introspection ----
  std::string xml = daemon.GenerateIntrospectionXML();
  static const char* kInterfaces[] = {
      "org.freedesktop.impl.portal.Settings",      "org.freedesktop.impl.portal.Screenshot",
      "org.freedesktop.impl.portal.ScreenCast",   "org.freedesktop.impl.portal.Inhibit",
      "org.freedesktop.impl.portal.Wallpaper",     "org.freedesktop.impl.portal.GlobalShortcuts",
  };
  bool xml_ok = xml.rfind("<node>", 0) == 0;
  for (const char* iface : kInterfaces) {
    if (xml.find(iface) == std::string::npos) xml_ok = false;
  }
  std::printf("Introspection XML: %zu bytes; interfaces present: %s\n", xml.size(), xml_ok ? "yes" : "no");
  if (!xml_ok) {
    std::printf("FAIL: introspection XML is missing interfaces\n");
    return 1;
  }

  std::printf("self-test: OK\n");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) {
    const std::string arg = argv[1];
    if (arg == "--self-test") return RunSelfTest();
    if (arg == "--help" || arg == "-h") {
      std::printf("Usage: %s [--self-test]\n", argv[0]);
      return 0;
    }
  }
  ash::PortalDaemon daemon;
  return ash::RunDaemon(&daemon);
}
