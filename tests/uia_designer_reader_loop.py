"""Windows UIA Designer protocol loop for the real reader acceptance.

This drives the same Designer window and UIA provider used by Narrator/NVDA.
It proves the protocol transactions and reports reader process liveness; speech
output remains an operator observation recorded in the platform evidence.

Usage (logged-in Win32 desktop, Python 3.8+ and ``comtypes``):

    python tests/uia_designer_reader_loop.py \
        build-live/examples/designer/lumen-designer.exe \
        examples/designer/gallery.design
"""

from pathlib import Path
import ctypes
import json
import shutil
import subprocess
import sys
import tempfile
import time


def eventually(predicate, timeout=12.0, interval=0.2):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(interval)
    raise AssertionError("UIA Designer round-trip timed out")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: uia_designer_reader_loop.py DESIGNER DESIGN_FILE")

    app_path = Path(sys.argv[1]).resolve()
    source_path = Path(sys.argv[2]).resolve()
    if not app_path.is_file() or not source_path.is_file():
        raise SystemExit("Designer executable and .design file must exist")

    from comtypes.client import CreateObject, GetModule

    GetModule("UIAutomationCore.dll")
    from comtypes.gen.UIAutomationClient import (  # noqa: E402
        CUIAutomation,
        IUIAutomation,
        IUIAutomationInvokePattern,
        IUIAutomationValuePattern,
        TreeScope_Children,
        TreeScope_Descendants,
        UIA_ButtonControlTypeId,
        UIA_ControlTypePropertyId,
        UIA_EditControlTypeId,
        UIA_InvokePatternId,
        UIA_NamePropertyId,
        UIA_ProcessIdPropertyId,
        UIA_TreeItemControlTypeId,
        UIA_ValuePatternId,
    )

    automation = CreateObject(CUIAutomation, interface=IUIAutomation)
    desktop = automation.GetRootElement()

    def launch(filename):
        app = subprocess.Popen(
            [str(app_path), "--file", str(filename)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        pid_condition = automation.CreateProperty_condition(
            UIA_ProcessIdPropertyId, app.pid)
        window = eventually(
            lambda: desktop.FindFirst(TreeScope_Children, pid_condition),
            timeout=25,
        )
        return app, window

    def close(app):
        if app.poll() is None:
            app.terminate()
        try:
            app.wait(timeout=5)
        except subprocess.TimeoutExpired:
            app.kill()
            app.wait(timeout=5)

    def condition(control_type, name=None):
        type_condition = automation.CreateProperty_condition(
            UIA_ControlTypePropertyId, control_type)
        if name is None:
            return type_condition
        return automation.CreateAnd_condition(
            type_condition,
            automation.CreateProperty_condition(UIA_NamePropertyId, name),
        )

    def find(window, control_type, name):
        return window.FindFirst(
            TreeScope_Descendants, condition(control_type, name))

    def find_prefix(window, control_type, prefix):
        elements = window.FindAll(
            TreeScope_Descendants, condition(control_type))
        if elements is None:
            return None
        for index in range(elements.Length):
            element = elements.GetElement(index)
            name = element.CurrentName if element is not None else None
            if name is not None and name.startswith(prefix):
                return element
        return None

    def count(window, control_type):
        elements = window.FindAll(
            TreeScope_Descendants, condition(control_type))
        return 0 if elements is None else elements.Length

    def invoke(element):
        pattern = element.GetCurrentPattern(UIA_InvokePatternId)
        pattern.QueryInterface(IUIAutomationInvokePattern).Invoke()

    def invoke_named(window, name):
        element = eventually(
            lambda: find(window, UIA_ButtonControlTypeId, name))
        invoke(element)

    def focus_and_invoke(element):
        assert element.SetFocus() == 0
        invoke(element)

    edited = "读屏 Designer 编辑🙂e\u0301"
    with tempfile.TemporaryDirectory(prefix="lumen-uia-designer-") as directory:
        target = Path(directory) / "reader.design"
        shutil.copyfile(source_path, target)
        original_bytes = target.read_bytes()

        app, window = launch(target)
        try:
            text_item = eventually(
                lambda: find_prefix(window, UIA_TreeItemControlTypeId, "Text  [")
            )
            focus_and_invoke(text_item)
            field = eventually(
                lambda: find(window, UIA_EditControlTypeId, "text")
            )
            value = field.GetCurrentPattern(UIA_ValuePatternId).QueryInterface(
                IUIAutomationValuePattern)
            original_value = value.CurrentValue
            assert original_value
            value.SetValue(edited)
            eventually(lambda: value.CurrentValue == edited)
            print("designer selection/edit: passed")

            root_item = eventually(
                lambda: find_prefix(window, UIA_TreeItemControlTypeId, "Column  [")
            )
            focus_and_invoke(root_item)
            initial_tree_items = count(window, UIA_TreeItemControlTypeId)
            invoke_named(window, "Add Text")
            eventually(
                lambda: count(window, UIA_TreeItemControlTypeId)
                == initial_tree_items + 1
            )
            invoke_named(window, "Duplicate")
            eventually(
                lambda: count(window, UIA_TreeItemControlTypeId)
                == initial_tree_items + 2
            )
            invoke_named(window, "Move up")
            invoke_named(window, "Move down")
            invoke_named(window, "Delete")
            eventually(
                lambda: count(window, UIA_TreeItemControlTypeId)
                == initial_tree_items + 1
            )
            print("designer structure commands: passed")

            invoke_named(window, "Save")
            eventually(lambda: target.read_bytes() != original_bytes)
            print("designer save: passed")
        finally:
            close(app)

        reopened, reopened_window = launch(target)
        try:
            reopened_item = eventually(
                lambda: find_prefix(
                    reopened_window, UIA_TreeItemControlTypeId, "Text  ["
                )
            )
            focus_and_invoke(reopened_item)
            reopened_field = eventually(
                lambda: find(reopened_window, UIA_EditControlTypeId, "text")
            )
            reopened_value = reopened_field.GetCurrentPattern(
                UIA_ValuePatternId).QueryInterface(IUIAutomationValuePattern)
            eventually(lambda: reopened_value.CurrentValue == edited)
            print("designer reopen: passed")
        finally:
            close(reopened)

    # Probe both reader processes after the protocol loop.  Their speech
    # output is intentionally left to the operator's record.
    try:
        nvda = ctypes.windll.LoadLibrary("nvdaControllerClient32.dll")
        nvda_alive = nvda.testIfRunning() == 0
        if nvda_alive:
            nvda.speakText("Lumen Designer UIA round-trip passed")
    except OSError:
        nvda_alive = None
    narrator = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq Narrator.exe"],
        capture_output=True,
        text=True,
    ).stdout
    narrator_alive = "Narrator.exe" in narrator
    print(json.dumps({
        "platform": "windows",
        "provider": "uia",
        "nvda_alive": nvda_alive,
        "narrator_alive": narrator_alive,
        "protocol_loop": ["designer_select_locate", "designer_edit_save"],
    }, ensure_ascii=False))
    print(
        "UIA Designer reader round-trip passed; speech evidence remains "
        "operator-recorded per docs/platform-acceptance.md"
    )


if __name__ == "__main__":
    main()
