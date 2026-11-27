#ifndef XDG_DESKTOP_PORTAL_ASH_DAEMON_H
#define XDG_DESKTOP_PORTAL_ASH_DAEMON_H

#include "portal.h"

namespace ash {

// Exports the portal interfaces on the session bus and serves them until
// SIGTERM/SIGINT.  Returns the process exit code.
int RunDaemon(PortalDaemon* daemon);

}  // namespace ash

#endif  // XDG_DESKTOP_PORTAL_ASH_DAEMON_H
