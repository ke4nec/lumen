"""Provider design §6 / Designer prerequisites §4.18 protocol regression.

Uses native AT-SPI; no Orca/desktop sign-off.
"""
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib


def descendants(node):
    yield node
    for index in range(node.get_child_count()):
        child = node.get_child_at_index(index)
        if child is not None:
            yield from descendants(child)


def eventually(predicate):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        for _ in range(20):
            if not GLib.MainContext.default().pending():
                break
            GLib.MainContext.default().iteration(False)
        result = predicate()
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError("Designer AT-SPI round-trip timed out")


def main():
    Atspi.init()
    Atspi.set_timeout(1000, 1000)
    events = []
    listener = Atspi.EventListener.new(lambda event: events.append(
        (event.source, event.type, event.detail1, event.detail2, event.any_data)))
    assert listener.register("object:text-changed")
    with tempfile.TemporaryDirectory(prefix="lumen-designer-atspi-") as directory:
        output = Path(directory) / "edited.design"
        app = subprocess.Popen([sys.argv[1], str(output)], stdout=subprocess.PIPE,
                               text=True)
        try:
            desktop = Atspi.get_desktop(0)
            root = eventually(lambda: next(
                (node for node in descendants(desktop)
                 if node.get_name() == "Lumen Designer AT-SPI test"), None))

            def find(name, role):
                return next((node for node in descendants(root)
                             if node.get_name() == name and node.get_role() == role), None)

            item = eventually(lambda: find("Text  [title]", Atspi.Role.TREE_ITEM))
            action = item.get_action_iface()
            assert action is not None
            activate = next(index for index in range(action.get_n_actions())
                            if action.get_action_name(index) == "activate")
            assert action.do_action(activate)
            eventually(lambda: item.get_state_set().contains(Atspi.StateType.SELECTED))

            field = eventually(lambda: find("text", Atspi.Role.ENTRY))
            assert field.get_text_iface() is not None
            assert field.get_editable_text_iface() is not None
            # Accessible.get_text() is the deprecated interface getter; use
            # Text explicitly for the actual D-Bus string method.
            get_text = lambda start=0, end=-1: Atspi.Text.get_text(field, start, end)

            def changed(operation, text):
                return (field, f"object:text-changed:{operation}", 0,
                        len(text), text) in events

            assert get_text() == "Title"
            assert Atspi.Text.get_character_count(field) == 5
            assert Atspi.EditableText.set_text_contents(field, "Renamed")
            eventually(lambda: get_text() == "Renamed")
            eventually(lambda: changed("delete", "Title"))
            eventually(lambda: changed("insert", "Renamed"))
            eventually(lambda: field.get_state_set().contains(Atspi.StateType.FOCUSED))

            label = eventually(lambda: find("text", Atspi.Role.TEXT))
            assert label.get_editable_text_iface() is None
            assert not Atspi.EditableText.set_text_contents(label, "Wrong target")
            assert get_text() == "Renamed"

            blocked = eventually(lambda: find("Blocked preview", Atspi.Role.ENTRY))
            assert not blocked.get_state_set().contains(Atspi.StateType.ENABLED)
            assert not Atspi.EditableText.set_text_contents(blocked, "Wrong target")
            assert Atspi.Text.get_text(blocked, 0, -1) == ""
            assert get_text() == "Renamed"

            assert Atspi.EditableText.set_text_contents(field, "")
            eventually(lambda: get_text() == "")
            eventually(lambda: changed("delete", "Renamed"))
            assert Atspi.Text.get_character_count(field) == 0
            renamed = "重命名🙂e\u0301"
            assert Atspi.EditableText.set_text_contents(field, renamed)
            eventually(lambda: get_text() == renamed)
            eventually(lambda: changed("insert", renamed))
            assert Atspi.Text.get_character_count(field) == len(renamed)
            assert get_text(0, 3) == "重命名"
            assert get_text(3, 4) == "🙂"
            assert get_text(4, 6) == "e\u0301"
            assert get_text(50, 60) == ""
            assert get_text(2, 2) == ""
            for start, end in ((-1, 1), (3, 2), (0, -2)):
                try:
                    get_text(start, end)
                except GLib.GError:
                    pass
                else:
                    raise AssertionError(f"invalid text range accepted: {start}, {end}")
            assert app.poll() is None
            # Wait for the application-side history/save/reopen assertions to
            # finish before terminating its D-Bus event pump.
            eventually(lambda: select.select([app.stdout], [], [], 0)[0])
            assert app.stdout.readline().strip() == "designer_atspi_transaction pass"
        finally:
            if app.poll() is None:
                app.terminate()
            try:
                app.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                app.kill()
                app.communicate()
                raise
        assert output.is_file()
        print("AT-SPI Designer selection / Unicode text and events / "
              "document history / save-reopen passed")


if __name__ == "__main__":
    main()
