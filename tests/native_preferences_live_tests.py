"""Isolated portal protocol regression; run under dbus-run-session."""
import os
import subprocess
import sys
from gi.repository import Gio, GLib

XML = """<node><interface name="org.freedesktop.portal.Settings">
<method name="ReadAll"><arg direction="in" type="as"/>
<arg direction="out" type="a{sa{sv}}"/></method>
</interface></node>"""


def main():
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                  "RequestName", GLib.Variant("(su)", ("org.freedesktop.portal.Desktop", 0)),
                  GLib.VariantType.new("(u)"), Gio.DBusCallFlags.NONE, 1000, None)
    calls = 0

    def method(connection, sender, path, interface, name, parameters, invocation):
        nonlocal calls
        calls += 1
        if calls >= 4:
            invocation.return_dbus_error("org.freedesktop.portal.Error.Failed", "temporary failure")
            return
        values = {
            "org.gnome.desktop.a11y.interface": {"high-contrast": GLib.Variant("b", True)},
            "org.gnome.desktop.interface": {
                "enable-animations": GLib.Variant("b", True),
                "text-scaling-factor": GLib.Variant("v", GLib.Variant("d", 1.25 if calls == 1 else 1.5)),
            },
        }
        if calls >= 2:
            values["org.freedesktop.appearance"] = {
                "contrast": GLib.Variant("u", 0 if calls == 2 else 99),
                "reduced-motion": GLib.Variant("u", 1 if calls == 2 else 99),
            }
        if calls == 3:
            values["org.gnome.desktop.interface"]["text-scaling-factor"] = GLib.Variant("d", float("nan"))
        reply = GLib.Variant("(a{sa{sv}})", (values,))
        if calls == 2:
            GLib.timeout_add(600, lambda: (invocation.return_value(reply), False)[1])
        else:
            invocation.return_value(reply)

    registration = bus.register_object("/org/freedesktop/portal/desktop",
        Gio.DBusNodeInfo.new_for_xml(XML).interfaces[0], method, None, None)
    environment = {**os.environ, "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"}
    app = subprocess.Popen([sys.argv[1]], env=environment)
    loop = GLib.MainLoop()

    def check():
        if app.poll() is not None:
            loop.quit()
            return False
        return True

    def timeout():
        if app.poll() is None: app.kill()
        loop.quit()
        return False

    GLib.timeout_add(20, check)
    GLib.timeout_add_seconds(15, timeout)
    try:
        loop.run()
        code = app.wait(timeout=2)
        if code or calls < 4:
            raise RuntimeError(f"native preferences: exit={code}, portal calls={calls}")
    finally:
        if app.poll() is None: app.kill()
        app.wait()
        bus.unregister_object(registration)


if __name__ == "__main__":
    main()
