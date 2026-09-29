# M13/M14-A macOS 真实屏幕阅读器回环（VoiceOver）。
#
# 前置（真实验收环境，不是 CI 默认路径）：
#   - 登录的 Aqua 会话（platform-acceptance 的 self-hosted macOS runner）；
#     Python 3 + `pip install pyobjc-framework-ApplicationServices`。
#   - 辅助功能权限：终端（或运行本脚本的进程）需在「系统设置 → 隐私
#     与安全性 → 辅助功能」中获准，否则 AXAPI 调用返回
#     kAXErrorAPIDisabled/-25211。
#   - VoiceOver 无可编程播报接口：脚本经 AXAPI 完成协议回环（焦点/
#     激活/值设置——与 tests/atspi_orca_loop.py 同构）并对 VoiceOver 进程
#     探活；语音播报由验收人按 docs/platform-acceptance.md 人工观察并
#     填入 record.json 的 readers 一节。
#   - headless NSAccessibility 树投影已有 C++ 回归；本脚本面向真实
#     NSWindow + VoiceOver 在场的完整回环。
#
# 运行（与应用同会话；路径按构建配置调整）：
#   python3 tests/voiceover_loop.py build-<cfg>/examples/gallery/lumen-gallery
#
# 覆盖（M13 出口：焦点导航/激活/值设置——协议回环）：
#   1. 注册核对：应用元素含窗口，子树存在 AXButton/AXSlider。
#   2. 焦点导航：侧栏按钮 kAXFocusedAttribute=true → 回读一致。
#   3. 激活：kAXPressAction ≡ 读屏器路由点击 → 路由切换（树更新）。
#   4. 值设置：AXSlider AXValue 置 80 → 回读一致。
#   5. VoiceOver 进程探活。
import json
import subprocess
import sys
import time

import ApplicationServices as AX


def eventually(predicate, timeout=12.0, interval=0.2):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(interval)
    raise AssertionError("AXAPI round-trip timed out")


def attribute(element, name):
    # pyobjc 出参约定：传入 None 占位，返回 (error, value)。
    error, value = AX.AXUIElementCopyAttributeValue(element, name, None)
    if error != 0:
        return None
    return value


def children(element):
    kids = attribute(element, AX.kAXChildrenAttribute)
    if kids is None:
        return []
    return list(kids)


def descendants(element):
    yield element
    for child in children(element):
        yield from descendants(child)


def press(element):
    return AX.AXUIElementPerformAction(element, AX.kAXPressAction) == 0


def main():
    app_path = sys.argv[1]
    app = subprocess.Popen([app_path, "--system-fonts"],
                           stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
    try:
        app_element = AX.AXUIElementCreateApplication(app.pid)

        def first_window():
            windows = attribute(app_element, AX.kAXWindowsAttribute)
            return windows[0] if windows else None

        window = eventually(first_window, timeout=25)
        print("window registered")

        def find(role, title=None):
            def lookup():
                for node in descendants(window):
                    if attribute(node, AX.kAXRoleAttribute) != role:
                        continue
                    if title is None:
                        return node
                    if attribute(node, AX.kAXTitleAttribute) == title:
                        return node
                return None
            return eventually(lookup)

        # 注册核对：控件映射在位。
        assert find(AX.kAXButtonRole) is not None, "no buttons"
        assert find(AX.kAXSliderRole) is not None, "no sliders"

        # 焦点导航：kAXFocusedAttribute=true → 回读一致。
        for title in ["Overview", "Buttons"]:
            node = find(AX.kAXButtonRole, title)
            AX.AXUIElementSetAttributeValue(
                node, AX.kAXFocusedAttribute, True)
            eventually(
                lambda n=node: attribute(n, AX.kAXFocusedAttribute) is True,
                timeout=8)
            print("focused:", title)

        # 激活（路由切换）：Buttons → Feedback 两跳，树随路由更新。
        assert press(find(AX.kAXButtonRole, "Buttons"))
        time.sleep(2.0)
        assert press(find(AX.kAXButtonRole, "Feedback"))
        time.sleep(2.0)
        print("activated route")

        # 值设置：AXSlider AXValue 置 80 并回读（值以字符串承载）。
        slider = find(AX.kAXSliderRole)
        AX.AXUIElementSetAttributeValue(slider, AX.kAXValueAttribute, "80")
        eventually(
            lambda: "80" in str(
                attribute(slider, AX.kAXValueAttribute) or ""),
            timeout=6)
        print("value set: 80")
    finally:
        app.terminate()
        try:
            app.wait(timeout=5)
        except subprocess.TimeoutExpired:
            app.kill()

    voiceover = subprocess.run(["pgrep", "-x", "VoiceOver"],
                               capture_output=True).returncode == 0
    print("voiceover alive:", voiceover)
    print(json.dumps({"platform": "macos", "provider": "nsaccessibility",
                      "voiceover_alive": voiceover,
                      "protocol_loop": ["focus", "activate", "value"]},
                     ensure_ascii=False))
    print("VoiceOver round-trip passed "
          "(focus / activate / value); speech evidence is human-recorded "
          "per docs/platform-acceptance.md")


if __name__ == "__main__":
    main()
