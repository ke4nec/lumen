"""Run under dbus-run-session; no desktop or Orca sign-off is implied."""
import subprocess
import sys
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
        while GLib.MainContext.default().pending():
            GLib.MainContext.default().iteration(False)
        result = predicate()
        if result:
            return result
        time.sleep(.05)
    raise AssertionError("native AT-SPI round-trip timed out")


Atspi.init()
Atspi.set_timeout(1000, 1000)
events = []
listener = Atspi.EventListener.new(lambda event: events.append(event.type))
listener.register("object:property-change")
listener.register("object:state-changed")
listener.register("window:activate")
with subprocess.Popen([sys.argv[1]]) as app:
    try:
        desktop = Atspi.get_desktop(0)
        def find(name):
            return next((node for node in descendants(desktop) if node.get_name() == name), None)
        button = eventually(lambda: find("Apply"))
        # GrabFocus → activateWindow 钩子 → window:activate（真实窗口路径
        # 经 SDL 抬升 + WindowFocusGained 到达同一入口）；屏幕阅读器以
        # window:activate 切换“当前应用”。libatspi 客户端按事件类型到达。
        assert button.get_component_iface().grab_focus()
        eventually(lambda: "window:activate" in events)
        eventually(lambda: button.get_state_set().contains(Atspi.StateType.FOCUSED))
        assert button.get_action_iface().do_action(0)
        eventually(lambda: find("Applied"))
        slider = eventually(lambda: find("Volume"))
        assert slider.get_component_iface().grab_focus()
        eventually(lambda: not button.get_state_set().contains(Atspi.StateType.FOCUSED))
        assert slider.get_value_iface().set_current_value(75)
        eventually(lambda: slider.get_value_iface().get_current_value() == 75)
        assert app.poll() is None
        print("AT-SPI native registry / window activation / focus / activate / value round-trip passed")
    finally:
        app.terminate()
        app.wait(timeout=5)
