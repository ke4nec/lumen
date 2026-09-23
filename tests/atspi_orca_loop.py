# M13 Linux 真实屏幕阅读器回环（Orca）。
#
# 前置（真实验收环境，不是 CI 默认路径）：
#   - 桌面会话（本文件在 GNOME/Wayland + XWayland 验证；AT-SPI 栈由会话
#     提供），python3-gi + Atspi 2.0。
#   - 以隔离会话运行避免其他应用的事件风暴拖慢 Orca 队列（共享会话下
#     队列积压可达分钟级，事件在处理时源已 DEAD，无法取证语音路径）：
#
#     dbus-run-session -- bash -c '
#       orca --replace --debug --debug-file=/tmp/orca-loop.log & sleep 6
#       SDL_VIDEODRIVER=x11 python3 tests/atspi_orca_loop.py \
#         build-<cfg>/examples/gallery/lumen-gallery /tmp/orca-loop.log'
#
#   - SDL_VIDEODRIVER=x11：X11 允许应用主动置前（AT-SPI Component.GrabFocus
#     → activateWindow → SDL_RaiseWindow → 窗口焦点）；Wayland 焦点授予由
#     合成器策略决定，脚本无法假设。
#
# 覆盖（M13 出口：焦点导航/激活/值设置）：
#   1. 注册核对：根对象 role=window、名称 = 应用名（宿主窗口标题）。
#   2. 窗口激活：根 GrabFocus → activateWindow → window:activate 事件。
#   3. 焦点导航：侧栏项 grab_focus → state-changed:focused + FOCUSED 状态。
#   4. 激活：Action.do_action ≡ 屏幕阅读器路由点击 → 路由切换（树更新）。
#   5. 值设置：Slider Value.set_current_value → 回读一致。
#   6. Orca 消费核对：debug 日志出现本应用专属 script 与事件处理记录。
#      语音播报依赖队列在应用存活期间排空且焦点事件源未过期——共享
#     （非隔离）会话事件风暴下无法稳定取证，属已知限制，不影响回环
#      功能判定。
import re
import subprocess
import sys
import time

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib

APP = sys.argv[1]
ORCA_LOG = sys.argv[2] if len(sys.argv) > 2 else "/tmp/orca-loop.log"


def pump(seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        # 事件流可能持续不断（其他应用），内循环设限保证按时返回。
        for _ in range(20):
            if not GLib.MainContext.default().pending():
                break
            GLib.MainContext.default().iteration(False)
        time.sleep(0.03)


def eventually(predicate, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for _ in range(20):
            if not GLib.MainContext.default().pending():
                break
            GLib.MainContext.default().iteration(False)
        result = predicate()
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError("native AT-SPI round-trip timed out")


def descendants(node):
    try:
        yield node
        count = node.get_child_count()
    except Exception:
        return
    for index in range(count):
        try:
            child = node.get_child_at_index(index)
        except Exception:
            continue
        if child is not None:
            yield from descendants(child)


def main():
    with open(ORCA_LOG, "ab") as mark:
        pass
    import os
    log_mark = os.path.getsize(ORCA_LOG)

    Atspi.init()
    Atspi.set_timeout(800, 800)
    events = []

    def on_event(event):
        try:
            name = str(event.source.get_name()) if event.source else "?"
        except Exception:
            name = "<gone>"
        events.append((event.type, name))

    listener = Atspi.EventListener.new(on_event)
    for name in ("object:state-changed", "object:property-change", "window:"):
        listener.register(name)

    app = subprocess.Popen([APP, "--system-fonts"],
                           stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
    try:
        desktop = Atspi.get_desktop(0)

        def find_window_root():
            # 注册表占位（application/0 子节点）不可导航；真正根对象是
            # role=window 且带子树的桌面子项（根名 = 应用名）。
            for index in range(desktop.get_child_count()):
                try:
                    child = desktop.get_child_at_index(index)
                except Exception:
                    continue
                if (child is not None
                        and child.get_role_name() == "window"
                        and child.get_child_count() > 0
                        and child.get_name() == "Lumen Gallery"):
                    return child
            return None

        root = eventually(find_window_root, timeout=25)
        print("window root registered:", root.get_name(),
              root.get_child_count())

        # 窗口激活：GrabFocus → activateWindow 钩子 → window:activate。
        assert root.get_component_iface().grab_focus()
        pump(2.0)

        def find(label):
            return next((n for n in descendants(desktop)
                         if n.get_name() == label), None)

        # 焦点导航。
        for label in ["Overview", "Buttons"]:
            node = eventually(lambda l=label: find(l), timeout=12)
            assert node.get_component_iface().grab_focus(), label
            eventually(lambda n=node: n.get_state_set().contains(
                Atspi.StateType.FOCUSED), 12)
            print("focused:", label)

        # 激活（路由切换）。
        nav = eventually(lambda: find("Buttons"), timeout=8)
        assert nav.get_action_iface().do_action(0)
        pump(2.5)
        print("activated route")

        # 值设置。
        to_feedback = eventually(lambda: find("Feedback"), timeout=8)
        assert to_feedback.get_action_iface().do_action(0)
        pump(2.5)

        def find_slider():
            return next((n for n in descendants(desktop)
                         if n.get_role_name() == "slider"), None)

        slider = eventually(find_slider, timeout=8)
        assert slider.get_value_iface().set_current_value(80)
        eventually(lambda: slider.get_value_iface().get_current_value() == 80, 6)
        print("value set: 80")

        # 给 Orca 队列留排空时间（隔离会话内为亚秒级）。
        pump(8.0)
        assert app.poll() is None
    finally:
        app.terminate()
        try:
            app.wait(timeout=5)
        except subprocess.TimeoutExpired:
            app.kill()

    # Orca 消费核对：本应用专属 script + 事件处理记录。
    with open(ORCA_LOG, "rb") as handle:
        handle.seek(log_mark)
        text = handle.read().decode("utf-8", "replace")
    assert re.search(r"Script is app script Lumen Gallery", text), \
        "Orca did not track the Lumen Gallery application"
    consumed = [
        key for key in ("window:activate for [window: 'Lumen Gallery'",
                        "state-changed:focused", "state-changed:active",
                        "property-change:accessible-value")
        if key.split(" for ")[0] in text
    ]
    assert "window:activate for [window: 'Lumen Gallery'" in text, \
        "Orca did not consume our window activation"
    print("orca consumed:", consumed)
    spoken = re.findall(r"SPEECH OUTPUT: '([^']*)'", text)
    print("orca spoke:", spoken[-8:])
    print("AT-SPI Orca round-trip passed "
          "(focus / activate / value + screen-reader consumption)")


if __name__ == "__main__":
    main()
