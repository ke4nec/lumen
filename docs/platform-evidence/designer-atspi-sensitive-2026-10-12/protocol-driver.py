import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import dbus


"""Production X11 AT-SPI protocol loop for the Designer window.

This talks to the native AT-SPI D-Bus provider directly. The existing
``designer_atspi_live_tests.py`` covers the headless provider fixture; this
loop additionally starts the real SDL Designer window and drives selection,
Unicode editing, ThemeScope editing, and save/reopen through the production
semantic tree. It is protocol evidence, not an Orca speech or desktop sign-off.
"""

ROLE_TREE_ITEM = 91
ROLE_ENTRY = 79
ROLE_BUTTON = 43
PLATFORMS = {"linux-x11", "linux-wayland"}


def main():
    if len(sys.argv) not in (3, 4):
        raise SystemExit(
            f"usage: {sys.argv[0]} <lumen-designer> <fixture.design> [platform]"
        )
    app_path = Path(sys.argv[1])
    source = Path(sys.argv[2])
    platform = sys.argv[3] if len(sys.argv) == 4 else "linux-x11"
    if platform not in PLATFORMS:
        raise SystemExit(f"unsupported Linux AT-SPI platform: {platform}")
    if not app_path.is_file() or not source.is_file():
        raise SystemExit("Designer production loop requires an app and fixture")

    with tempfile.TemporaryDirectory(prefix="lumen-designer-window-atspi-") as directory:
        target = Path(directory) / "edited.design"
        shutil.copyfile(source, target)

        def launch():
            return subprocess.Popen(
                [str(app_path), "--file", str(target)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )

        def close(process):
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

        app = launch()
        try:
            session = dbus.SessionBus()
            address = session.get_object(
                "org.a11y.Bus", "/org/a11y/bus"
            ).GetAddress(dbus_interface="org.a11y.Bus")
            bus = dbus.bus.BusConnection(str(address))
            registry = bus.get_object(
                "org.a11y.atspi.Registry", "/org/a11y/atspi/accessible/root",
                introspect=False,
            )

            def proxy(ref):
                return bus.get_object(str(ref[0]), str(ref[1]), introspect=False)

            def get(ref, interface, property_name):
                return proxy(ref).Get(
                    interface,
                    property_name,
                    dbus_interface="org.freedesktop.DBus.Properties",
                    timeout=1,
                )

            def name(ref):
                return str(get(ref, "org.a11y.atspi.Accessible", "Name"))

            def role(ref):
                return int(get(ref, "org.a11y.atspi.Accessible", "Role"))

            def children(ref):
                return [
                    (str(owner), str(path))
                    for owner, path in proxy(ref).GetChildren(
                        dbus_interface="org.a11y.atspi.Accessible", timeout=1
                    )
                ]

            def roots():
                return [
                    (str(owner), str(path))
                    for owner, path in registry.GetChildren(
                        dbus_interface="org.a11y.atspi.Accessible", timeout=1
                    )
                ]

            def wait_for_root():
                deadline = time.monotonic() + 25
                while time.monotonic() < deadline:
                    for ref in roots():
                        try:
                            if name(ref) == "Lumen Designer":
                                owner_pid = bus.get_object(
                                    "org.freedesktop.DBus", "/org/freedesktop/DBus"
                                ).GetConnectionUnixProcessID(
                                    str(ref[0]),
                                    dbus_interface="org.freedesktop.DBus", timeout=1,
                                )
                                if int(owner_pid) == app.pid:
                                    return ref
                        except dbus.DBusException:
                            continue
                    time.sleep(0.05)
                raise AssertionError("production Designer AT-SPI window was not registered")

            root = wait_for_root()
            proxy(root).GrabFocus(dbus_interface="org.a11y.atspi.Component", timeout=2)
            print("root focus state", list(proxy(root).GetState(dbus_interface="org.a11y.atspi.Accessible",timeout=2)),flush=True)
            time.sleep(120)

            def find(predicate, start=None, limit=300):
                queue = [root if start is None else start]
                seen = set()
                while queue and len(seen) < limit:
                    ref = queue.pop(0)
                    if ref in seen:
                        continue
                    seen.add(ref)
                    try:
                        if predicate(ref):
                            return ref
                        queue.extend(children(ref))
                    except dbus.DBusException:
                        continue
                return None

            def wait_find(predicate, start=None, timeout=20):
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    found = find(predicate, start=start)
                    if found is not None:
                        return found
                    time.sleep(0.1)
                raise AssertionError("production Designer AT-SPI node timed out")

            def wait_action(predicate, start=None, timeout=20):
                """Invoke a current node, retrying across semantic-tree rebuilds."""
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    try:
                        ref = find(predicate, start=start)
                        if ref is not None:
                            proxy(ref).GrabFocus(dbus_interface="org.a11y.atspi.Component", timeout=2)
                            time.sleep(15)
                        if ref is not None and proxy(ref).DoAction(
                            dbus.Int32(0),
                            dbus_interface="org.a11y.atspi.Action",
                            timeout=2,
                        ):
                            return
                    except dbus.DBusException:
                        pass
                    time.sleep(0.1)
                raise AssertionError("production Designer AT-SPI action timed out")

            def property_panel():
                # Designer body is the first child of the content panel; its
                # third child is the stable Properties panel.
                content = children(root)[1]
                body = children(content)[0]
                panel = children(body)[2]
                return panel

            def property_field(label):
                panel = property_panel()
                return wait_find(
                    lambda ref: name(ref) == label and role(ref) == ROLE_ENTRY,
                    start=panel,
                    timeout=20,
                )

            text_predicate = lambda ref: (
                name(ref) == "Text  [text]" and role(ref) == ROLE_TREE_ITEM
            )
            wait_action(text_predicate)

            def get_text(ref):
                return str(
                    proxy(ref).GetText(
                        dbus.Int32(0), dbus.Int32(-1),
                        dbus_interface="org.a11y.atspi.Text", timeout=1,
                    )
                )

            def read_field(label, getter, expected):
                """Read a current field until its semantic value settles."""
                deadline = time.monotonic() + 20
                last_value = None
                while time.monotonic() < deadline:
                    try:
                        last_value = getter(property_field(label))
                        if last_value == expected:
                            return last_value
                    except dbus.DBusException:
                        pass
                    time.sleep(0.1)
                raise AssertionError(
                    "production Designer property read did not settle: "
                    f"{label!r} expected {expected!r}, got {last_value!r}"
                )

            def wait_field_value(label, getter, expected):
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    try:
                        if getter(property_field(label)) == expected:
                            return
                    except dbus.DBusException:
                        pass
                    time.sleep(0.1)
                raise AssertionError("production Designer property value did not update")

            def set_field_value(label, value):
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    try:
                        field = property_field(label)
                        if proxy(field).SetTextContents(
                            value,
                            dbus_interface="org.a11y.atspi.EditableText",
                            timeout=2,
                        ):
                            return
                    except dbus.DBusException:
                        pass
                    time.sleep(0.1)
                raise AssertionError("production Designer property write timed out")

            read_field("text", get_text, "Gallery")
            renamed = "窗口回环🙂e\u0301"
            set_field_value("text", renamed)
            wait_field_value("text", get_text, renamed)

            theme_predicate = lambda ref: (
                name(ref) == "ThemeScope  [theme-scope]"
                and role(ref) == ROLE_TREE_ITEM
            )
            wait_action(theme_predicate)

            def get_theme(ref):
                return str(
                    proxy(ref).GetText(
                        dbus.Int32(0), dbus.Int32(-1),
                        dbus_interface="org.a11y.atspi.Text", timeout=1,
                    )
                )

            read_field("theme reference", get_theme, "light")
            set_field_value("theme reference", "dark")
            wait_field_value("theme reference", get_theme, "dark")

            wait_action(
                lambda ref: name(ref) == "Save" and role(ref) == ROLE_BUTTON,
                start=root,
            )
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                try:
                    document = json.loads(target.read_text(encoding="utf-8"))
                    root_node = document["root"]
                    nodes = []
                    queue = [root_node]
                    while queue:
                        node = queue.pop()
                        nodes.append(node)
                        queue.extend(node.get("children", []))
                        for slot in node.get("slots", {}).values():
                            queue.extend(slot)
                    text_node = next(n for n in nodes if n.get("id") == "4")
                    theme_node = next(n for n in nodes if n.get("id") == "21")
                    if (
                        text_node["properties"]["text"]["value"] == renamed
                        and theme_node["references"]["theme"] == "dark"
                    ):
                        break
                except (OSError, KeyError, StopIteration, json.JSONDecodeError):
                    pass
                time.sleep(0.1)
            else:
                raise AssertionError("production Designer Save did not persist edits")

            close(app)
            app = launch()
            root = wait_for_root()
            proxy(root).GrabFocus(dbus_interface="org.a11y.atspi.Component", timeout=2)
            print("root focus state", list(proxy(root).GetState(dbus_interface="org.a11y.atspi.Accessible",timeout=2)),flush=True)
            time.sleep(120)
            wait_action(text_predicate)
            read_field("text", get_text, renamed)
            wait_action(theme_predicate)
            read_field("theme reference", get_theme, "dark")
            time.sleep(15)
            print(
                "AT-SPI production Designer window selection / Unicode text / "
                "ThemeScope / save / reopen passed"
            )
            print(json.dumps({
                "platform": platform,
                "provider": "atspi-dbus",
                "protocol_loop": ["designer_select_locate", "designer_edit_save"],
                "theme_reference": "light -> dark",
                "windowed": True,
                "reopened": True,
            }, ensure_ascii=False))
        finally:
            close(app)


if __name__ == "__main__":
    main()
