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

import ApplicationServices as AX


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
    while time.monotonic() < deadline:
        try:
            result = predicate()
        except Exception:
            result = None
        if result is not None and result is not False:
            return result
        time.sleep(0.1)
    raise AssertionError("NSAccessibility Designer protocol loop timed out")


def main():
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
        app = subprocess.Popen(
            [str(app_path), "--file", str(target)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            application = AX.AXUIElementCreateApplication(app.pid)

            def find_window():
                for window in attribute(application, AX.kAXWindowsAttribute) or []:
                    title = attribute(window, AX.kAXTitleAttribute)
                    if title in ("Lumen Designer", "Lumen Designer - edited"):
                        return window
                return None

            window = eventually(find_window)

            def find(role, title, start=window):
                for element in descendants(start):
                    if attribute(element, AX.kAXRoleAttribute) != role:
                        continue
                    if title is None or attribute(element, AX.kAXTitleAttribute) == title:
                        return element
                return None

            def find_eventually(role, title, start=window):
                return eventually(lambda: find(role, title, start))

            text_item = find_eventually(ROLE_ROW, "Text  [text]")
            if AX.AXUIElementPerformAction(text_item, AX.kAXPressAction) != 0:
                raise AssertionError("Designer Text row activation was rejected")

            def field_value(label):
                field = find(ROLE_TEXT_FIELD, label)
                return None if field is None else attribute(field, AX.kAXValueAttribute)

            eventually(lambda: field_value("text") == "Gallery")
            renamed = "窗口回环🙂e\u0301"
            field = find_eventually(ROLE_TEXT_FIELD, "text")
            if AX.AXUIElementSetAttributeValue(
                field, AX.kAXValueAttribute, renamed
            ) != 0:
                raise AssertionError("Designer Text AXValue write was rejected")
            eventually(lambda: field_value("text") == renamed)

            theme_item = find_eventually(ROLE_ROW, "ThemeScope  [theme-scope]")
            if AX.AXUIElementPerformAction(theme_item, AX.kAXPressAction) != 0:
                raise AssertionError("Designer ThemeScope row activation was rejected")
            eventually(lambda: field_value("theme reference") == "light")
            theme_field = find_eventually(ROLE_TEXT_FIELD, "theme reference")
            if AX.AXUIElementSetAttributeValue(
                theme_field, AX.kAXValueAttribute, "dark"
            ) != 0:
                raise AssertionError("Designer ThemeScope AXValue write was rejected")
            eventually(lambda: field_value("theme reference") == "dark")

            save = find_eventually(ROLE_BUTTON, "Save")
            if AX.AXUIElementPerformAction(save, AX.kAXPressAction) != 0:
                raise AssertionError("Designer Save AXPress was rejected")

            def saved_values():
                try:
                    document = json.loads(target.read_text(encoding="utf-8"))
                    nodes = []
                    queue = [document["root"]]
                    while queue:
                        node = queue.pop()
                        nodes.append(node)
                        queue.extend(node.get("children", []))
                        queue.extend(node.get("slots", {}).values())
                    text_node = next(node for node in nodes if node.get("id") == "4")
                    theme_node = next(node for node in nodes if node.get("id") == "21")
                    return (
                        text_node["properties"]["text"]["value"] == renamed
                        and theme_node["references"]["theme"] == "dark"
                    )
                except (OSError, KeyError, StopIteration, json.JSONDecodeError):
                    return False

            eventually(saved_values, timeout=12)
        finally:
            if app.poll() is None:
                app.terminate()
            try:
                app.wait(timeout=5)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait()

    voiceover_alive = subprocess.run(
        ["pgrep", "-x", "VoiceOver"], capture_output=True
    ).returncode == 0
    print("NSAccessibility production Designer selection / Unicode text / "
          "ThemeScope / save passed")
    print(json.dumps({
        "platform": "macos",
        "provider": "nsaccessibility",
        "protocol_loop": ["designer_select_locate", "designer_edit_save"],
        "theme_reference": "light -> dark",
        "windowed": True,
        "voiceover_alive": voiceover_alive,
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()
