"""Drive the production Designer window through NSAccessibility.

The AXAPI calls below are the protocol loop used by a VoiceOver session. A
successful loop proves registration, selection, text/reference editing and
save/reopen through the native Designer window; VoiceOver speech remains an
operator observation recorded separately in ``record.json``.
"""

import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROLE_BUTTON = "AXButton"
ROLE_ROW = "AXRow"
ROLE_TEXT_FIELD = "AXTextField"


def attribute(element, name):
    error, value = AX.AXUIElementCopyAttributeValue(element, name, None)
    if error != 0:
        return None
    return value


def children(element):
    try:
        return list(attribute(element, AX.kAXChildrenAttribute) or [])
    except Exception:
        return []


def descendants(element):
    yield element
    for child in children(element):
        try:
            yield from descendants(child)
        except Exception:
            continue


def eventually(predicate, timeout=25.0):
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            result = predicate()
        except Exception as error:
            last_error = error
            result = None
        if result is not None and result is not False:
            return result
        time.sleep(0.1)
    if last_error is not None:
        raise AssertionError(
            "NSAccessibility Designer protocol loop timed out after transient object errors"
        ) from last_error
    raise AssertionError("NSAccessibility Designer protocol loop timed out")


def saved_values(target, renamed):
    try:
        document = json.loads(target.read_text(encoding="utf-8"))
        nodes = []
        queue = [document["root"]]
        while queue:
            node = queue.pop()
            nodes.append(node)
            queue.extend(node.get("children", []))
            for slot in node.get("slots", {}).values():
                queue.extend(slot)
        text_node = next(node for node in nodes if node.get("id") == "4")
        theme_node = next(node for node in nodes if node.get("id") == "21")
        return (
            text_node["properties"]["text"]["value"] == renamed
            and theme_node["references"]["theme"] == "dark"
        )
    except (OSError, KeyError, StopIteration, json.JSONDecodeError):
        return False


def main():
    global AX
    import ApplicationServices as AX

    if len(sys.argv) != 3:
        raise SystemExit(
            f"usage: {sys.argv[0]} <lumen-designer> <fixture.design>"
        )
    app_path = Path(sys.argv[1])
    source = Path(sys.argv[2])
    if not app_path.is_file() or not source.is_file():
        raise SystemExit("Designer AX loop requires an app and fixture")

    with tempfile.TemporaryDirectory(prefix="lumen-designer-ax-") as directory:
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
            application = AX.AXUIElementCreateApplication(app.pid)

            def find_window():
                for window in attribute(application, AX.kAXWindowsAttribute) or []:
                    title = attribute(window, AX.kAXTitleAttribute)
                    if title in ("Lumen Designer", "Lumen Designer - edited"):
                        return window
                return None

            window = eventually(find_window)

            def find(role, title, start=None):
                for element in descendants(window if start is None else start):
                    if attribute(element, AX.kAXRoleAttribute) != role:
                        continue
                    if title is None or attribute(element, AX.kAXTitleAttribute) == title:
                        return element
                return None

            def press_current(role, title, start=None):
                def action():
                    element = find(role, title, start)
                    return element is not None and AX.AXUIElementPerformAction(
                        element, AX.kAXPressAction
                    ) == 0

                eventually(action)

            press_current(ROLE_ROW, "Text  [text]")

            def field_value(label):
                field = find(ROLE_TEXT_FIELD, label)
                return None if field is None else attribute(field, AX.kAXValueAttribute)

            eventually(lambda: field_value("text") == "Gallery")
            renamed = "窗口回环🙂e\u0301"

            def set_value(label, value):
                def write():
                    field = find(ROLE_TEXT_FIELD, label)
                    return field is not None and AX.AXUIElementSetAttributeValue(
                        field, AX.kAXValueAttribute, value
                    ) == 0

                eventually(write)

            set_value("text", renamed)
            eventually(lambda: field_value("text") == renamed)

            press_current(ROLE_ROW, "ThemeScope  [theme-scope]")
            eventually(lambda: field_value("theme reference") == "light")
            set_value("theme reference", "dark")
            eventually(lambda: field_value("theme reference") == "dark")

            press_current(ROLE_BUTTON, "Save")

            eventually(lambda: saved_values(target, renamed), timeout=12)

            close(app)
            app = launch()
            application = AX.AXUIElementCreateApplication(app.pid)
            window = eventually(find_window)
            press_current(ROLE_ROW, "Text  [text]")
            eventually(lambda: field_value("text") == renamed)
            press_current(ROLE_ROW, "ThemeScope  [theme-scope]")
            eventually(lambda: field_value("theme reference") == "dark")
        finally:
            close(app)

    voiceover_alive = subprocess.run(
        ["pgrep", "-x", "VoiceOver"], capture_output=True
    ).returncode == 0
    print("NSAccessibility production Designer selection / Unicode text / "
          "ThemeScope / save / reopen passed")
    print(json.dumps({
        "platform": "macos",
        "provider": "nsaccessibility",
        "protocol_loop": ["designer_select_locate", "designer_edit_save"],
        "theme_reference": "light -> dark",
        "windowed": True,
        "reopened": True,
        "voiceover_alive": voiceover_alive,
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
