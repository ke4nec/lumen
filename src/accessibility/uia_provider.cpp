// M13（自用路线图）：Windows UIA provider 实现。
//
// 结构：SharedState（语义树快照 + provider 表；provider 经 shared_ptr
// 共享——桥析构断开后残留 AT 引用安全降级 NotAvailable）→ UiaRootProvider
//（fragment root，对应语义根节点）→ UiaNodeProvider（每节点一个，
// Simple/Fragment + Invoke/Toggle/Value/RangeValue pattern 同对象）。
// 事件经 UiaEventSink 出口（默认实现 UiaRaise*；测试注入记录器）。
//
// 线程模型：全部调用发生在 UI 线程（WM_GETOBJECT 应答、pattern 执行、
// updateTree 推送），无锁；refcount 用 Interlocked 仅作 COM 纪律。
// dispatch（= AppShell::performAccessibilityAction）可能同步触发重建/
// 推送，因此 pattern 方法先拷贝自身 id，dispatch 后不再读树状态。

#if defined(_WIN32) && defined(LUMEN_ACCESSIBILITY_PROVIDER_UIA)

#include "uia_provider.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>

#include <commctrl.h>
#include <windows.h>

namespace lumen::accessibility::uia {
namespace {

constexpr ProviderOptions kProviderOptions = ProviderOptions_ServerSideProvider;

// ---- 引用计数基类（COM 纪律；实际调用单线程 UI） ----
class RefCounted {
  public:
    virtual ~RefCounted() = default;
    ULONG addRef() { return InterlockedIncrement(&refCount_); }
    ULONG release() {
        const ULONG remaining = InterlockedDecrement(&refCount_);
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

  private:
    ULONG refCount_{1};
};

// ---- 字符串/VARIANT 工具 ----

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), count);
    return wide;
}

std::string toUtf8(const wchar_t* wide) {
    if (wide == nullptr || *wide == L'\0') {
        return {};
    }
    const int count =
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(count - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), count, nullptr,
                        nullptr);
    return utf8;
}

BSTR wideBstr(const std::string& utf8) {
    const std::wstring wide = toWide(utf8);
    return SysAllocStringLen(wide.c_str(), static_cast<UINT>(wide.size()));
}

VARIANT variantI4(LONG value) {
    VARIANT var;
    VariantInit(&var);
    var.vt = VT_I4;
    var.lVal = value;
    return var;
}

VARIANT variantBool(bool value) {
    VARIANT var;
    VariantInit(&var);
    var.vt = VT_BOOL;
    var.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    return var;
}

VARIANT variantBstr(const std::string& utf8) {
    VARIANT var;
    VariantInit(&var);
    var.vt = VT_BSTR;
    var.bstrVal = wideBstr(utf8);
    return var;
}

// ---- 语义 → UIA 映射 ----

// 根（role Window）映射为 Pane：HWND 已由 UIA 提供窗口框架元素，根
// provider 代表应用内容面。Switch 无专用类型（CheckBox 近似，勾选语义
// 一致）；Splitter 无专用类型（Slider 近似，可调 0..100 值）。
CONTROLTYPEID controlTypeFor(SemanticsRole role) {
    switch (role) {
        case SemanticsRole::Button:
            return UIA_ButtonControlTypeId;
        case SemanticsRole::MenuItem:
            return UIA_MenuItemControlTypeId;
        case SemanticsRole::Menu:
            return UIA_MenuControlTypeId;
        case SemanticsRole::Text:
            return UIA_TextControlTypeId;
        case SemanticsRole::TextField:
            return UIA_EditControlTypeId;
        case SemanticsRole::Checkbox:
        case SemanticsRole::Switch:
            return UIA_CheckBoxControlTypeId;
        case SemanticsRole::Radio:
            return UIA_RadioButtonControlTypeId;
        case SemanticsRole::List:
            return UIA_ListControlTypeId;
        case SemanticsRole::ListItem:
            return UIA_ListItemControlTypeId;
        case SemanticsRole::Tree:
            return UIA_TreeControlTypeId;
        case SemanticsRole::TreeItem:
            return UIA_TreeItemControlTypeId;
        case SemanticsRole::Image:
            return UIA_ImageControlTypeId;
        case SemanticsRole::Slider:
        case SemanticsRole::Splitter:
            return UIA_SliderControlTypeId;
        case SemanticsRole::ProgressBar:
            return UIA_ProgressBarControlTypeId;
        case SemanticsRole::Group:
            return UIA_GroupControlTypeId;
        case SemanticsRole::Window:
        case SemanticsRole::Dialog:
        default:
            return UIA_PaneControlTypeId;
    }
}

bool supportsInvoke(const SemanticsNode& node) {
    return (node.actions & kActionActivate) != 0;
}

bool supportsToggle(const SemanticsNode& node) {
    return node.role == SemanticsRole::Checkbox ||
           node.role == SemanticsRole::Switch ||
           node.role == SemanticsRole::Radio;
}

bool supportsValue(const SemanticsNode& node) {
    return node.role == SemanticsRole::TextField &&
           (node.actions & kActionSetValue) != 0;
}

bool supportsRangeValue(const SemanticsNode& node) {
    return node.role == SemanticsRole::Slider ||
           node.role == SemanticsRole::ProgressBar ||
           node.role == SemanticsRole::Splitter;
}

// identity 稳定哈希（FNV-1a 64）→ RuntimeId 双字；跨重建稳定，AT 焦点
// 不漂移。
std::uint64_t stableIdHash(const std::string& identity) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : identity) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

SAFEARRAY* runtimeIdOf(const std::string& id) {
    const std::uint64_t hash = stableIdHash(id);
    SAFEARRAY* array = SafeArrayCreateVector(VT_I4, 0, 3);
    if (array == nullptr) {
        return nullptr;
    }
    LONG items[3] = {
        static_cast<LONG>(UiaAppendRuntimeId),
        static_cast<LONG>(hash & 0xFFFFFFFFULL),
        static_cast<LONG>((hash >> 32) & 0xFFFFFFFFULL)};
    for (LONG index = 0; index < 3; ++index) {
        if (FAILED(SafeArrayPutElement(array, &index, &items[index]))) {
            SafeArrayDestroy(array);
            return nullptr;
        }
    }
    return array;
}

// 动态解析 GetDpiForWindow（Win10 1607+；更旧系统返回 96 → 物理倍率 1）。
UINT windowDpi(HWND hwnd) {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return 96;
    }
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    const auto getDpi = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(user32, "GetDpiForWindow"));
    return getDpi != nullptr ? getDpi(hwnd) : 96;
}

// ---- 共享状态（provider 持有；桥析构置 detached 断开） ----

class UiaRootProvider;
class UiaNodeProvider;

struct SharedState {
    std::atomic<bool> detached{false};
    HWND hwnd{nullptr};
    bool hwndConnected{false};  // 子类化成功（UIA 应答/事件通道有效）
    float deviceScale{1.0F};
    std::string applicationName;
    SemanticsActionDispatch dispatch;

    // 当前语义树快照与派生索引（UI 线程独占）。
    std::string rootId;
    std::string focusedId;
    std::map<std::string, SemanticsNode> nodes;
    std::map<std::string, std::string> parentOf;
    // provider 缓存（表持有一份引用；桥析构统一断开/释放）。根 provider
    // 单独持有（rootId 复用其身份）。
    UiaRootProvider* rootProvider{nullptr};
    std::map<std::string, UiaNodeProvider*> providers;
};

// provider 获取（AddRef 后返回；空 id/缺失/已断开返回 null）。
// 定义在两类 provider 之后（需要完整类型）；此处声明供成员体内使用。
[[nodiscard]] IRawElementProviderFragment* acquireFragment(
    const SharedState& state, const std::string& id);
[[nodiscard]] IRawElementProviderFragmentRoot* acquireRootFragment(
    const SharedState& state);

// 逻辑（窗口坐标，语义 bounds）→ 物理屏幕像素。
UiaRect screenRectFor(const SharedState& state, const SemanticsNode& node) {
    POINT origin{0, 0};
    float scale = state.deviceScale;
    if (state.hwnd != nullptr) {
        ClientToScreen(state.hwnd, &origin);
        scale = static_cast<float>(windowDpi(state.hwnd)) / 96.0F;
    }
    return UiaRect{origin.x + node.bounds.origin.x * scale,
                   origin.y + node.bounds.origin.y * scale,
                   node.bounds.size.width * scale,
                   node.bounds.size.height * scale};
}

// 命中最深节点：子树逆序（后声明在上层——overlay 追加于根末尾）；
// 隐藏节点整体不可命中。返回 null = 不在任何节点内。
const SemanticsNode* deepestNodeAt(const SharedState& state,
                                   const std::string& id, double x, double y) {
    const auto it = state.nodes.find(id);
    if (it == state.nodes.end()) {
        return nullptr;
    }
    const SemanticsNode& node = it->second;
    if ((node.flags & kSemanticsHidden) != 0) {
        return nullptr;
    }
    for (auto child = node.children.rbegin(); child != node.children.rend();
         ++child) {
        if (const SemanticsNode* hit = deepestNodeAt(state, *child, x, y)) {
            return hit;
        }
    }
    const bool contains = x >= node.bounds.left() && x < node.bounds.right() &&
                          y >= node.bounds.top() && y < node.bounds.bottom();
    return contains ? &node : nullptr;
}

// ---- 节点 provider（Simple + Fragment + 四 pattern 同对象） ----

class UiaNodeProvider final : public RefCounted,
                              public IRawElementProviderSimple,
                              public IRawElementProviderFragment,
                              public IInvokeProvider,
                              public IToggleProvider,
                              public IValueProvider,
                              public IRangeValueProvider {
  public:
    UiaNodeProvider(std::shared_ptr<SharedState> state, std::string nodeId)
        : state_(std::move(state)), nodeId_(std::move(nodeId)) {}

    // --- IUnknown ---
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                             void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        const SemanticsNode* node = lookup();
        if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {
            *out = static_cast<IRawElementProviderSimple*>(this);
        } else if (riid == IID_IRawElementProviderFragment) {
            *out = static_cast<IRawElementProviderFragment*>(this);
        } else if (node != nullptr && riid == IID_IInvokeProvider &&
                   supportsInvoke(*node)) {
            *out = static_cast<IInvokeProvider*>(this);
        } else if (node != nullptr && riid == IID_IToggleProvider &&
                   supportsToggle(*node)) {
            *out = static_cast<IToggleProvider*>(this);
        } else if (node != nullptr && riid == IID_IValueProvider &&
                   supportsValue(*node)) {
            *out = static_cast<IValueProvider*>(this);
        } else if (node != nullptr && riid == IID_IRangeValueProvider &&
                   supportsRangeValue(*node)) {
            *out = static_cast<IRangeValueProvider*>(this);
        } else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        addRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return addRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return release(); }

    // --- IRawElementProviderSimple ---
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(
        ProviderOptions* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = kProviderOptions;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern,
                                                 IUnknown** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        const SemanticsNode* node = lookup();
        if (node == nullptr) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        IUnknown* provider = nullptr;
        if (pattern == UIA_InvokePatternId && supportsInvoke(*node)) {
            provider = static_cast<IInvokeProvider*>(this);
        } else if (pattern == UIA_TogglePatternId && supportsToggle(*node)) {
            provider = static_cast<IToggleProvider*>(this);
        } else if (pattern == UIA_ValuePatternId && supportsValue(*node)) {
            provider = static_cast<IValueProvider*>(this);
        } else if (pattern == UIA_RangeValuePatternId &&
                   supportsRangeValue(*node)) {
            provider = static_cast<IRangeValueProvider*>(this);
        }
        if (provider != nullptr) {
            *out = provider;
            addRef();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;  // 非根节点无宿主元素（根由 UiaRootProvider 应答）。
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id,
                                               VARIANT* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        VariantInit(out);
        const SemanticsNode* node = lookup();
        if (node == nullptr) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        switch (id) {
            case UIA_ControlTypePropertyId:
                *out = variantI4(static_cast<LONG>(controlTypeFor(node->role)));
                break;
            case UIA_LocalizedControlTypePropertyId:
                *out = variantBstr(semanticsRoleName(node->role));
                break;
            case UIA_NamePropertyId:
                *out = variantBstr(node->label);
                break;
            case UIA_IsEnabledPropertyId:
                *out = variantBool((node->flags & kSemanticsEnabled) != 0);
                break;
            case UIA_IsKeyboardFocusablePropertyId:
                *out = variantBool((node->actions & kActionFocus) != 0 &&
                                   (node->flags & kSemanticsEnabled) != 0);
                break;
            case UIA_HasKeyboardFocusPropertyId:
                // 语义 flag 与当前焦点 id 双源（setFocusedNode 可先于
                // 下一帧 flag 推送到达）。
                *out = variantBool(
                    (node->flags & kSemanticsFocused) != 0 ||
                    nodeId_ == state_->focusedId);
                break;
            case UIA_IsOffscreenPropertyId:
                *out = variantBool((node->flags & kSemanticsHidden) != 0);
                break;
            case UIA_IsPasswordPropertyId:
                *out = variantBool(false);
                break;
            case UIA_IsDataValidForFormPropertyId:
                *out = variantBool((node->flags & kSemanticsInvalid) == 0);
                break;
            case UIA_IsDialogPropertyId:
                *out = variantBool(node->role == SemanticsRole::Dialog);
                break;
            default:
                break;  // VT_EMPTY：UIA 回退默认。
        }
        return S_OK;
    }

    // --- IRawElementProviderFragment ---
    HRESULT STDMETHODCALLTYPE Navigate(
        NavigateDirection direction,
        IRawElementProviderFragment** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        const SemanticsNode* node = lookup();
        if (node == nullptr) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        std::string target;
        switch (direction) {
            case NavigateDirection_Parent: {
                const auto parent = state_->parentOf.find(nodeId_);
                if (parent != state_->parentOf.end()) {
                    target = parent->second;
                }
                break;
            }
            case NavigateDirection_FirstChild:
                if (!node->children.empty()) {
                    target = node->children.front();
                }
                break;
            case NavigateDirection_LastChild:
                if (!node->children.empty()) {
                    target = node->children.back();
                }
                break;
            case NavigateDirection_NextSibling:
            case NavigateDirection_PreviousSibling: {
                const auto parent = state_->parentOf.find(nodeId_);
                if (parent == state_->parentOf.end()) {
                    break;
                }
                const auto parentIt = state_->nodes.find(parent->second);
                if (parentIt == state_->nodes.end()) {
                    break;
                }
                const std::vector<std::string>& siblings =
                    parentIt->second.children;
                for (std::size_t index = 0; index < siblings.size(); ++index) {
                    if (siblings[index] != nodeId_) {
                        continue;
                    }
                    if (direction == NavigateDirection_NextSibling &&
                        index + 1 < siblings.size()) {
                        target = siblings[index + 1];
                    } else if (direction == NavigateDirection_PreviousSibling &&
                               index > 0) {
                        target = siblings[index - 1];
                    }
                    break;
                }
                break;
            }
            default:
                break;
        }
        if (!target.empty()) {
            *out = acquireFragment(*state_, target);  // AddRef；无则 null。
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = runtimeIdOf(nodeId_);
        return *out != nullptr ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
        // 单窗口片段根承载（UiaReturnRawElementProvider）；无嵌入根。
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        // AT 请求聚焦 → 语义 Focus action（FocusManager 路径）。
        dispatchAction(kActionFocus, {});
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        const SemanticsNode* node = lookup();
        if (node == nullptr) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        *out = screenRectFor(*state_, *node);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(
        IRawElementProviderFragmentRoot** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = acquireRootFragment(*state_);
        return S_OK;
    }

    // --- IInvokeProvider ---
    HRESULT STDMETHODCALLTYPE Invoke() override {
        dispatchAction(kActionActivate, {});
        return S_OK;
    }

    // --- IToggleProvider（语义勾选状态；Toggle 即 Activate 同路径） ---
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        const SemanticsNode* node = lookup();
        if (node == nullptr) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        *out = (node->flags & kSemanticsChecked) != 0 ? ToggleState_On
                                                      : ToggleState_Off;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Toggle() override { return Invoke(); }

    // --- IValueProvider ---
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        const SemanticsNode* node = lookup();
        if (node == nullptr) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        *out = wideBstr(node->value);
        return *out != nullptr || node->value.empty() ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
        dispatchAction(kActionSetValue, toUtf8(value));
        return S_OK;
    }

    // get_IsReadOnly 由 IValueProvider/IRangeValueProvider 共用（C++ 单
    // 覆写同时填两个 vtable 槽位）：按语义 action 声明判定——只读字段/
    // 进度条不声明 kActionSetValue。
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        const SemanticsNode* node = lookup();
        if (node == nullptr) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        *out = (node->actions & kActionSetValue) == 0 ? TRUE : FALSE;
        return S_OK;
    }

    // --- IRangeValueProvider（数值控件 0..100 百分比，与语义契约一致） ---
    HRESULT STDMETHODCALLTYPE get_Value(double* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        const SemanticsNode* node = lookup();
        if (node == nullptr) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        *out = parsePercent(node->value);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = 100.0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = 0.0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = 10.0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = 1.0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetValue(double value) override {
        // 语义 SetValue 为字符串（strtof 解析）；固定三位小数无残尾。
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.3f", value);
        dispatchAction(kActionSetValue, buffer);
        return S_OK;
    }

  private:
    [[nodiscard]] const SemanticsNode* lookup() const {
        if (state_->detached.load()) {
            return nullptr;
        }
        const auto it = state_->nodes.find(nodeId_);
        return it == state_->nodes.end() ? nullptr : &it->second;
    }

    void dispatchAction(std::uint32_t action, const std::string& value) {
        // dispatch 可同步触发重建/推送：先拷贝 id，之后不再读树状态。
        const std::string id = nodeId_;
        if (!state_->detached.load() && state_->dispatch) {
            state_->dispatch(id, action, value, 0.0F);
        }
    }

    static double parsePercent(const std::string& raw) {
        char* end = nullptr;
        const double parsed = std::strtod(raw.c_str(), &end);
        if (end == raw.c_str() || !std::isfinite(parsed)) {
            return 0.0;
        }
        return parsed < 0.0 ? 0.0 : (parsed > 100.0 ? 100.0 : parsed);
    }

    std::shared_ptr<SharedState> state_;
    std::string nodeId_;
};

// ---- 根 provider（fragment root = 语义根节点） ----

class UiaRootProvider final : public RefCounted,
                              public IRawElementProviderSimple,
                              public IRawElementProviderFragment,
                              public IRawElementProviderFragmentRoot {
  public:
    explicit UiaRootProvider(std::shared_ptr<SharedState> state)
        : state_(std::move(state)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                             void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IRawElementProviderSimple) {
            *out = static_cast<IRawElementProviderSimple*>(this);
        } else if (riid == IID_IRawElementProviderFragment) {
            *out = static_cast<IRawElementProviderFragment*>(this);
        } else if (riid == IID_IRawElementProviderFragmentRoot) {
            *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        } else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        addRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return addRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return release(); }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(
        ProviderOptions* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = kProviderOptions;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID,
                                                 IUnknown** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;  // 窗口容器无 pattern；子节点按角色提供。
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        if (state_->hwnd == nullptr) {
            return S_OK;  // headless 测试：无宿主元素。
        }
        const HRESULT result = UiaHostProviderFromHwnd(state_->hwnd, out);
        return SUCCEEDED(result) ? S_OK : result;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id,
                                               VARIANT* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        VariantInit(out);
        const SemanticsNode* node = lookup();
        if (node == nullptr) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (id == UIA_NamePropertyId && node->label.empty()) {
            // 窗口标题兜底（AT 读窗口名）。
            *out = variantBstr(state_->applicationName);
            return S_OK;
        }
        switch (id) {
            case UIA_ControlTypePropertyId:
                // HWND 已提供窗口框架元素；内容根固定为 Pane。
                *out = variantI4(static_cast<LONG>(UIA_PaneControlTypeId));
                break;
            case UIA_NamePropertyId:
                *out = variantBstr(node->label);
                break;
            case UIA_IsEnabledPropertyId:
                *out = variantBool((node->flags & kSemanticsEnabled) != 0);
                break;
            case UIA_IsKeyboardFocusablePropertyId:
            case UIA_HasKeyboardFocusPropertyId:
                *out = variantBool(false);  // 窗口容器自身不聚焦。
                break;
            default:
                break;  // 其余属性无值（VT_EMPTY）。
        }
        return S_OK;
    }

    // --- IRawElementProviderFragment ---
    HRESULT STDMETHODCALLTYPE Navigate(
        NavigateDirection direction,
        IRawElementProviderFragment** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        const SemanticsNode* node = lookup();
        if (node == nullptr) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (direction == NavigateDirection_FirstChild &&
            !node->children.empty()) {
            *out = acquireFragment(*state_, node->children.front());
        } else if (direction == NavigateDirection_LastChild &&
                   !node->children.empty()) {
            *out = acquireFragment(*state_, node->children.back());
        }
        return S_OK;  // 根无父/无兄弟。
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        // HWND 根由 UIA 提供运行时 id（官方建议：根返回 null）。
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        return S_OK;  // 窗口容器自身不聚焦（子节点可）。
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        const SemanticsNode* node = lookup();
        if (node == nullptr) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        *out = screenRectFor(*state_, *node);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(
        IRawElementProviderFragmentRoot** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (state_->detached.load()) {
            *out = nullptr;
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        *out = this;
        addRef();
        return S_OK;
    }

    // --- IRawElementProviderFragmentRoot ---
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(
        double x, double y, IRawElementProviderFragment** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = nullptr;
        const SemanticsNode* root = lookup();
        if (root == nullptr) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        // 物理 → 逻辑（窗口坐标）。
        POINT origin{0, 0};
        float scale = state_->deviceScale;
        if (state_->hwnd != nullptr) {
            ClientToScreen(state_->hwnd, &origin);
            scale = static_cast<float>(windowDpi(state_->hwnd)) / 96.0F;
        }
        const double logicalX = (x - origin.x) / scale;
        const double logicalY = (y - origin.y) / scale;
        const SemanticsNode* hit =
            deepestNodeAt(*state_, state_->rootId, logicalX, logicalY);
        // 命不中任何节点（点在窗外/空白）时归属根。
        *out = acquireFragment(*state_,
                               hit != nullptr ? hit->id : state_->rootId);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFocus(
        IRawElementProviderFragment** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        *out = state_->focusedId.empty()
                   ? nullptr
                   : acquireFragment(*state_, state_->focusedId);
        return S_OK;
    }

  private:
    [[nodiscard]] const SemanticsNode* lookup() const {
        if (state_->detached.load()) {
            return nullptr;
        }
        const auto it = state_->nodes.find(state_->rootId);
        return it == state_->nodes.end() ? nullptr : &it->second;
    }

    std::shared_ptr<SharedState> state_;
};

IRawElementProviderFragment* acquireFragment(const SharedState& state,
                                             const std::string& id) {
    if (state.detached.load() || id.empty()) {
        return nullptr;
    }
    if (id == state.rootId) {
        // FragmentRoot 与 Fragment 是平行接口（各自仅继承 IUnknown），
        // 根对象两者皆实现——static_cast 走 Fragment 基子对象。
        if (state.rootProvider == nullptr) {
            return nullptr;
        }
        state.rootProvider->AddRef();
        return static_cast<IRawElementProviderFragment*>(state.rootProvider);
    }
    const auto it = state.providers.find(id);
    if (it == state.providers.end()) {
        return nullptr;
    }
    it->second->AddRef();
    return it->second;
}

IRawElementProviderFragmentRoot* acquireRootFragment(const SharedState& state) {
    if (state.detached.load() || state.rootProvider == nullptr) {
        return nullptr;
    }
    state.rootProvider->AddRef();
    return state.rootProvider;
}

constexpr UINT_PTR kUiaSubclassId = 0x4C554D31;  // "LUM1"

}  // namespace

// ---- 桥实现 ----

class UiaAccessibilityBridge::Impl final : public UiaEventSink {
  public:
    Impl(const PlatformAccessibilityHost& host, std::string* diagnostics) {
        state_ = std::make_shared<SharedState>();
        state_->hwnd = static_cast<HWND>(host.nativeWindow);
        state_->deviceScale =
            host.deviceScale > 0.0F ? host.deviceScale : 1.0F;
        state_->applicationName = host.applicationName;
        state_->dispatch = host.dispatch;
        if (state_->hwnd != nullptr && IsWindow(state_->hwnd)) {
            if (SetWindowSubclass(state_->hwnd, uiaSubclassProc, kUiaSubclassId,
                                  reinterpret_cast<DWORD_PTR>(this))) {
                state_->hwndConnected = true;
            } else if (diagnostics != nullptr) {
                *diagnostics = "UIA bridge: SetWindowSubclass failed";
            }
        }
        // hwnd 为空（headless 测试）＝可用但不连接窗口通道（不应答
        // WM_GETOBJECT、不进 UIA raise）。
        available_ = state_->hwnd == nullptr || state_->hwndConnected;
        state_->rootProvider = new UiaRootProvider(state_);
    }

    ~Impl() override {
        // 1) 断开 UIA 应答与子类（后续 WM_GETOBJECT 走默认）。
        if (state_->hwndConnected) {
            UiaReturnRawElementProvider(state_->hwnd, 0, 0, nullptr);
            RemoveWindowSubclass(state_->hwnd, uiaSubclassProc, kUiaSubclassId);
        }
        // 2) 断开全部 provider：AT 残留引用安全降级 NotAvailable；表内
        // 引用统一释放。
        state_->detached.store(true);
        UiaDisconnectProvider(state_->rootProvider);
        state_->rootProvider->Release();
        state_->rootProvider = nullptr;
        for (auto& [id, provider] : state_->providers) {
            (void)id;
            UiaDisconnectProvider(provider);
            provider->Release();
        }
        state_->providers.clear();
    }

    [[nodiscard]] bool available() const { return available_; }

    void updateTree(const SemanticsTree& tree, const SemanticsDiff& diff,
                    const std::string& focusedId) {
        // 旧快照留作 changed 字段级比较。
        std::map<std::string, SemanticsNode> previous =
            std::move(state_->nodes);
        state_->nodes = tree.nodes;
        state_->rootId = tree.rootId;
        state_->focusedId = focusedId;
        state_->parentOf.clear();
        for (const auto& [id, node] : state_->nodes) {
            for (const auto& child : node.children) {
                state_->parentOf[child] = id;
            }
        }
        // provider 增删（identity 稳定 → provider/RuntimeId 稳定）。
        for (const auto& id : diff.removed) {
            const auto it = state_->providers.find(id);
            if (it == state_->providers.end()) {
                continue;
            }
            UiaDisconnectProvider(it->second);
            it->second->Release();
            state_->providers.erase(it);
        }
        for (const auto& id : diff.added) {
            ensureProvider(id);
        }
        // 事件（注入 sink 或默认 UIA 出口）。
        if (!diff.added.empty() || !diff.removed.empty()) {
            raiseEvent(UiaEvent{UiaEvent::Kind::StructureChanged, {}, 0});
        }
        for (const auto& id : diff.changed) {
            raiseChangedEvents(id, previous);
        }
    }

    void setFocusedNode(const std::string& id) {
        state_->focusedId = id;
        raiseEvent(UiaEvent{UiaEvent::Kind::FocusChanged, id, 0});
    }

    void setEventSink(UiaEventSink* sink) { sink_ = sink; }

    [[nodiscard]] IRawElementProviderFragmentRoot* acquireRoot() const {
        return acquireRootFragment(*state_);
    }

    [[nodiscard]] IRawElementProviderFragment* acquireNode(
        const std::string& id) const {
        return acquireFragment(*state_, id);
    }

    [[nodiscard]] bool hasNode(const std::string& id) const {
        return state_->nodes.count(id) != 0;
    }

    // UiaEventSink：默认 UIA 出口（窗口连接且未断开才真正进 UIA）。
    void raise(const UiaEvent& event) override {
        if (!state_->hwndConnected || state_->detached.load()) {
            return;
        }
        switch (event.kind) {
            case UiaEvent::Kind::StructureChanged:
                if (state_->rootProvider != nullptr) {
                    UiaRaiseStructureChangedEvent(
                        state_->rootProvider,
                        StructureChangeType_ChildrenInvalidated, nullptr, 0);
                }
                break;
            case UiaEvent::Kind::PropertyChanged: {
                IRawElementProviderSimple* provider =
                    providerFor(event.nodeId);
                if (provider == nullptr) {
                    break;
                }
                VARIANT oldValue;
                VARIANT newValue;
                VariantInit(&oldValue);
                VariantInit(&newValue);
                // 空 old/new：UIA 自行取当前值（合法用法）。
                UiaRaiseAutomationPropertyChangedEvent(
                    provider, event.propertyId, oldValue, newValue);
                break;
            }
            case UiaEvent::Kind::FocusChanged: {
                IRawElementProviderSimple* provider =
                    providerFor(event.nodeId);
                if (provider == nullptr) {
                    break;
                }
                UiaRaiseAutomationEvent(provider,
                                        UIA_AutomationFocusChangedEventId);
                break;
            }
        }
    }

    LRESULT handleWmGetObject(HWND hwnd, WPARAM wParam, LPARAM lParam) {
        // 只应答 UIA 根对象请求（MSAA OBJID_* 走默认）。
        if (static_cast<LONG>(lParam) != static_cast<LONG>(UiaRootObjectId)) {
            return 0;
        }
        return UiaReturnRawElementProvider(hwnd, wParam, lParam,
                                           state_->rootProvider);
    }

  private:
    void ensureProvider(const std::string& id) {
        // 根节点由 UiaRootProvider 承载（acquireFragment 特判 rootId）。
        if (id.empty() || id == state_->rootId ||
            state_->providers.count(id) != 0 ||
            state_->nodes.count(id) == 0) {
            return;
        }
        auto* provider = new UiaNodeProvider(state_, id);
        state_->providers[id] = provider;  // 表持有构造引用。
    }

    [[nodiscard]] IRawElementProviderSimple* providerFor(
        const std::string& id) const {
        if (id.empty() || id == state_->rootId) {
            return state_->rootProvider;
        }
        const auto it = state_->providers.find(id);
        return it == state_->providers.end() ? nullptr : it->second;
    }

    void raiseEvent(const UiaEvent& event) {
        UiaEventSink& sink = sink_ != nullptr ? *sink_ : *this;
        sink.raise(event);
    }

    void raiseChangedEvents(
        const std::string& id,
        const std::map<std::string, SemanticsNode>& previous) {
        const auto oldIt = previous.find(id);
        const auto newIt = state_->nodes.find(id);
        if (oldIt == previous.end() || newIt == state_->nodes.end()) {
            // 无前后快照可比较（结构漂移）：整体失效兜底。
            raiseEvent(UiaEvent{UiaEvent::Kind::StructureChanged, {}, 0});
            return;
        }
        const SemanticsNode& oldNode = oldIt->second;
        const SemanticsNode& newNode = newIt->second;
        if (oldNode.label != newNode.label) {
            raiseEvent(
                UiaEvent{UiaEvent::Kind::PropertyChanged, id,
                         static_cast<int>(UIA_NamePropertyId)});
        }
        if (oldNode.value != newNode.value) {
            if (supportsValue(newNode)) {
                raiseEvent(UiaEvent{
                    UiaEvent::Kind::PropertyChanged, id,
                    static_cast<int>(UIA_ValueValuePropertyId)});
            } else if (supportsRangeValue(newNode)) {
                raiseEvent(UiaEvent{
                    UiaEvent::Kind::PropertyChanged, id,
                    static_cast<int>(UIA_RangeValueValuePropertyId)});
            }
        }
        if ((oldNode.flags & kSemanticsEnabled) !=
            (newNode.flags & kSemanticsEnabled)) {
            raiseEvent(UiaEvent{
                UiaEvent::Kind::PropertyChanged, id,
                static_cast<int>(UIA_IsEnabledPropertyId)});
        }
        if ((oldNode.flags & kSemanticsChecked) !=
            (newNode.flags & kSemanticsChecked)) {
            raiseEvent(UiaEvent{
                UiaEvent::Kind::PropertyChanged, id,
                static_cast<int>(UIA_ToggleToggleStatePropertyId)});
        }
        if ((oldNode.flags & kSemanticsFocused) !=
            (newNode.flags & kSemanticsFocused)) {
            raiseEvent(UiaEvent{
                UiaEvent::Kind::PropertyChanged, id,
                static_cast<int>(UIA_HasKeyboardFocusPropertyId)});
        }
        if ((oldNode.flags & kSemanticsInvalid) !=
            (newNode.flags & kSemanticsInvalid)) {
            raiseEvent(UiaEvent{
                UiaEvent::Kind::PropertyChanged, id,
                static_cast<int>(UIA_IsDataValidForFormPropertyId)});
        }
        if (oldNode.bounds != newNode.bounds) {
            raiseEvent(UiaEvent{
                UiaEvent::Kind::PropertyChanged, id,
                static_cast<int>(UIA_BoundingRectanglePropertyId)});
        }
    }

    std::shared_ptr<SharedState> state_;
    UiaEventSink* sink_{nullptr};
    bool available_{true};

    static LRESULT CALLBACK uiaSubclassProc(HWND hwnd, UINT msg,
                                            WPARAM wParam, LPARAM lParam,
                                            UINT_PTR idSubclass,
                                            DWORD_PTR data) {
        if (msg == WM_GETOBJECT) {
            auto* impl = reinterpret_cast<Impl*>(data);
            if (impl != nullptr && idSubclass == kUiaSubclassId) {
                const LRESULT handled =
                    impl->handleWmGetObject(hwnd, wParam, lParam);
                if (handled != 0) {
                    return handled;
                }
            }
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
};

// ---- 桥公共转发 ----

UiaAccessibilityBridge::UiaAccessibilityBridge(
    const PlatformAccessibilityHost& host, std::string* diagnostics)
    : impl_(std::make_unique<Impl>(host, diagnostics)) {}

UiaAccessibilityBridge::~UiaAccessibilityBridge() = default;

bool UiaAccessibilityBridge::available() const { return impl_->available(); }

void UiaAccessibilityBridge::updateTree(const SemanticsTree& tree,
                                        const SemanticsDiff& diff,
                                        const std::string& focusedId) {
    impl_->updateTree(tree, diff, focusedId);
}

void UiaAccessibilityBridge::setFocusedNode(const std::string& id) {
    impl_->setFocusedNode(id);
}

void UiaAccessibilityBridge::setEventSinkForTesting(UiaEventSink* sink) {
    impl_->setEventSink(sink);
}

IRawElementProviderFragmentRoot*
UiaAccessibilityBridge::rootProviderForTesting() const {
    return impl_->acquireRoot();
}

IRawElementProviderFragment* UiaAccessibilityBridge::fragmentForTesting(
    const std::string& id) const {
    return impl_->acquireNode(id);
}

bool UiaAccessibilityBridge::hasNode(const std::string& id) const {
    return impl_->hasNode(id);
}

std::unique_ptr<AccessibilityBridge> createUiaBridge(
    const PlatformAccessibilityHost& host, std::string* diagnostics) {
    auto bridge = std::make_unique<UiaAccessibilityBridge>(host, diagnostics);
    if (!bridge->available()) {
        if (diagnostics != nullptr && diagnostics->empty()) {
            *diagnostics = "UIA bridge: window connection failed";
        }
        return nullptr;
    }
    return bridge;
}

}  // namespace lumen::accessibility::uia

#endif  // defined(_WIN32) && defined(LUMEN_ACCESSIBILITY_PROVIDER_UIA)
