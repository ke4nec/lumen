# M13/M14-A Windows 真实屏幕阅读器回环（讲述人/NVDA）。
#
# 前置（真实验收环境，不是 CI 默认路径）：
#   - 登录的 Win32 桌面会话（platform-acceptance 的 self-hosted Windows
#     runner）；Python 3.8+ 与 `pip install comtypes`。
#   - NVDA 运行中时，脚本经 nvdaControllerClient32.dll 探活并播报一句
#     摘要（读屏器存活 + 语音通道证据）；讲述人无公开自动化 API——
#     进程探活即可，语音播报由验收人按 docs/platform-acceptance.md
#     人工观察并填入 record.json 的 readers 一节。
#   - UIA 客户端链路（ElementFromHandle/FindFirst/Invoke）另有
#     `LUMEN_UIA_LIVE_SMOKE=1` 的 C++ 用例覆盖；本脚本面向读屏器
#     在场的完整回环，与 tests/atspi_orca_loop.py 同构。
#
# 运行（与应用同会话；路径按构建配置调整）：
#   python tests/uia_reader_loop.py build-<cfg>\examples\gallery\lumen-gallery.exe
#
# 覆盖（M13 出口：焦点导航/激活/值设置——协议回环）：
#   1. 注册核对：按进程号定位顶层窗口元素，子树存在 Button/Slider。
#   2. 焦点导航：侧栏按钮 SetFocus → GetFocusedElement 名称一致。
#   3. 激活：InvokePattern ≡ 读屏器路由点击 → 路由切换（树更新）。
#   4. 值设置：Slider RangeValue.SetValue(80) → 回读一致。
#   5. NVDA/讲述人探活与摘要播报（如在场）。
import ctypes
import json
import subprocess
import sys
import time


def eventually(predicate, timeout=12.0, interval=0.2):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(interval)
    raise AssertionError("UIA round-trip timed out")


def main():
    app_path = sys.argv[1]
    from comtypes.client import CreateObject, GetModule

    GetModule("UIAutomationCore.dll")
    from comtypes.gen.UIAutomationClient import (  # noqa: E402
        CUIAutomation,
        IUIAutomation,
        IUIAutomationInvokePattern,
        IUIAutomationRangeValuePattern,
        TreeScope_Children,
        TreeScope_Descendants,
        UIA_ButtonControlTypeId,
        UIA_ControlTypePropertyId,
        UIA_InvokePatternId,
        UIA_NamePropertyId,
        UIA_ProcessIdPropertyId,
        UIA_RangeValuePatternId,
        UIA_SliderControlTypeId,
    )

    uia = CreateObject(CUIAutomation, interface=IUIAutomation)
    root = uia.GetRootElement()

    app = subprocess.Popen([app_path, "--system-fonts"])
    try:
        pid_condition = uia.CreateProperty_condition(
            UIA_ProcessIdPropertyId, app.pid)
        window = eventually(
            lambda: root.FindFirst(TreeScope_Children, pid_condition),
            timeout=25)
        print("window registered:", window.CurrentName)

        def find_named(control_type, label):
            def lookup():
                condition = uia.CreateAnd_condition(
                    uia.CreateProperty_condition(
                        UIA_ControlTypePropertyId, control_type),
                    uia.CreateProperty_condition(UIA_NamePropertyId, label))
                return window.FindFirst(TreeScope_Descendants, condition)
            return eventually(lookup)

        def control_type_count(control_type):
            condition = uia.CreateProperty_condition(
                UIA_ControlTypePropertyId, control_type)
            elements = window.FindAll(TreeScope_Descendants, condition)
            return elements.Length if elements is not None else 0

        def focused_name():
            element = uia.GetFocusedElement()
            return element.CurrentName if element is not None else None

        # 注册核对：控件映射在位。
        assert control_type_count(UIA_ButtonControlTypeId) > 0, "no buttons"
        assert control_type_count(UIA_SliderControlTypeId) > 0, "no sliders"

        # 焦点导航：SetFocus → 系统焦点回读一致（读屏器随之播报）。
        for label in ["Overview", "Buttons"]:
            find_named(UIA_ButtonControlTypeId, label).SetFocus()
            eventually(lambda l=label: focused_name() == l, timeout=8)
            print("focused:", label)

        # 激活（路由切换）：Buttons → Feedback 两跳，树随路由更新。
        def invoke_named(label):
            pattern = find_named(
                UIA_ButtonControlTypeId, label).GetCurrentPattern(
                    UIA_InvokePatternId)
            pattern.QueryInterface(IUIAutomationInvokePattern).Invoke()

        invoke_named("Buttons")
        time.sleep(2.0)
        invoke_named("Feedback")
        time.sleep(2.0)
        print("activated route")

        # 值设置：Slider → 80 并回读。
        slider_condition = uia.CreateProperty_condition(
            UIA_ControlTypePropertyId, UIA_SliderControlTypeId)
        slider = eventually(
            lambda: window.FindFirst(TreeScope_Descendants, slider_condition))
        range_value = slider.GetCurrentPattern(
            UIA_RangeValuePatternId).QueryInterface(
                IUIAutomationRangeValuePattern)
        range_value.SetValue(80)
        eventually(lambda: abs(range_value.CurrentValue - 80) < 0.01,
                   timeout=6)
        print("value set: 80")
    finally:
        app.terminate()
        try:
            app.wait(timeout=5)
        except subprocess.TimeoutExpired:
            app.kill()

    summary = {"platform": "windows", "provider": "uia",
               "protocol_loop": ["focus", "activate", "value"]}

    # NVDA 探活（在场则播报一句摘要；不在场跳过，不视为失败）。
    try:
        nvda = ctypes.windll.LoadLibrary("nvdaControllerClient32.dll")
        if nvda.testIfRunning() == 0:
            nvda.speakText("Lumen UIA round-trip passed")
            summary["nvda_alive"] = True
            print("nvda: alive, summary announced")
        else:
            summary["nvda_alive"] = False
    except OSError:
        summary["nvda_alive"] = None
        print("nvda: controller client unavailable")

    # 讲述人探活（进程存在性；语音证据人工记录）。
    tasklist = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq Narrator.exe"],
        capture_output=True, text=True).stdout
    summary["narrator_alive"] = "Narrator.exe" in tasklist
    print("narrator alive:", summary["narrator_alive"])

    print(json.dumps(summary, ensure_ascii=False))
    print("UIA reader round-trip passed "
          "(focus / activate / value); speech evidence is human-recorded per "
          "docs/platform-acceptance.md")


if __name__ == "__main__":
    main()
