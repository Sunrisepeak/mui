# mcpplibs.os：COM／WinRT 与 Objective-C 运行时（方案，待 review）

> 状态：**方案，未实施**。要求（2026-11-10）：给 mcpplibs.os 提 PR 前先写方案；严格符合 os 仓库的规范（README “Rules” R0–R7）；mpal 是第一个消费者。openkal 不对任何平台做适配，镜像里这些模块按 R5 不提供定义，mpal 如实返回 unsupported。

## 1. 为什么放进 os，而不是 mpal

mpal 的 Windows toast（WinRT `Windows.UI.Notifications`）和 macOS 通知（`UNUserNotificationCenter`）都要：

- Windows：COM 的套间初始化、对象创建与引用计数、按虚表槽调用、HSTRING、WinRT 类的激活、实现一个委托接口（`TypedEventHandler` 收 `Activated`）；
- macOS：取类与选择子、发消息、autorelease pool、NSString 与 UTF-8 互转、block（完成回调）、运行时定义一个类（通知中心的 delegate 收答复）。

这些是**系统的调用约定**，与“通知”无关：文件对话框、分享、钥匙串、电源断言、菜单栏都会用到，xdesk、Luban 也会用到。按 mpal README 的划分（原语归 os，服务归 mpal），它们是原语；按 os 的 R2，它们没有可移植的含义，所以进**平台模块** `mcpplibs.os.windows`／`mcpplibs.os.macos`，不进 `os::<area>`。

## 2. 符合 os 的规范

| 规则 | 本方案 |
|---|---|
| R0 | 标准库没有这些能力。 |
| R1 | 没有可移植门面：不做 `os::com`／`os::objc` 的跨平台抽象。 |
| R2 | 只放 COM／WinRT 与 ObjC 运行时本身；每个声明写明“为什么只有这个系统有”。 |
| R3 | 消费者用 `if constexpr (os::can_run_on(platform::windows))` 加 `os::host()` 选择（mpal 现在就这样用）。 |
| R4 | 原生句柄（`IUnknown*`、`id`、`SEL`、`Class`）只出现在平台模块的类型里，不进可移植类型。 |
| R5 | 两个子命名空间在每个目标上都声明、只在自己的平台上定义；镜像（`<arch>-openkal`）里不定义，丢弃分支不 odr-use，照常链接。CI 断言两者（沿用现有检查）。 |
| R6 | 不跨共享对象边界传递本库资源：对象和 block 的生命周期在本库内配对（AddRef/Release、retain/release、Block_copy/Block_release）。 |
| R7 | 文本一律 UTF-8 进出：`hstring` 从 UTF-8 构造、读回 UTF-8；`objc::string` 同理；不经代码页。 |

另外两条 os 的实现约定也照做：

- **接口单元只 import std，没有系统头文件、没有宏。** 实现单元自己声明要调用的函数（同 `src/windows/win32.h`、`src/macos/darwin.h`）。
- **不新增链接依赖。** `combase.dll` 的 `RoActivateInstance`／`RoGetActivationFactory`／`WindowsCreateString`，以及 `libobjc.A.dylib` 的 `objc_msgSend`／`objc_getClass`／`sel_registerName`／`objc_allocateClassPair`，都在第一次使用时用 os 自己的 `library::open`／`function<F>` 取得。这样不改清单的链接参数，在 openkal-macos 的 libSystem 存根下也照常链接；框架（Foundation、UserNotifications）同样按需加载，加载后类才可取到。

## 3. 接口（草案）

```cpp
// mcpplibs.os.windows — COM and the Windows Runtime: the system's object model; no other system has it (R2).
export namespace mcpplibs::os::windows::com {

struct guid { std::uint32_t data1; std::uint16_t data2, data3; std::array<std::uint8_t, 8> data4; };
consteval guid parse_guid(std::string_view text);   // "{…}" at compile time: no macro, no header

// The calling thread's apartment, for as long as it lives (CoInitializeEx / CoUninitialize).
class apartment { public: enum class kind { single, multi }; static result<apartment> enter(kind); … };

// One reference to an object (AddRef/Release paired here, R6).
class object {
public:
    result<object> query(const guid& interface) const;                 // QueryInterface
    template <class R, class... A> result<R> call(int slot, A... args) const;  // the vtable's slot; HRESULT checked
    void* raw() const noexcept;                                          // for a call the slot form cannot spell
};
result<object> create(const guid& class_id, const guid& interface);     // CoCreateInstance

// The Windows Runtime: strings and classes by name.
class hstring { public: static result<hstring> of(std::string_view utf8); std::string utf8() const; … };
result<object> activate(std::string_view class_name);                   // RoActivateInstance
result<object> factory(std::string_view class_name, const guid& interface);  // RoGetActivationFactory

// A delegate the system calls back (IUnknown + Invoke(sender, args)): every WinRT event handler has this shape.
result<object> delegate(const guid& interface, std::function<void(object sender, object args)> invoked);
}

// mcpplibs.os.macos — the Objective-C runtime: how the system's frameworks are called; no other system has it (R2).
export namespace mcpplibs::os::macos::objc {

struct id_ref;   // one retained reference (retain/release paired here, R6)
result<void> load_framework(std::string_view name);   // "UserNotifications": its classes then exist
std::optional<id_ref> class_named(std::string_view name);
std::uintptr_t selector(std::string_view name);
template <class R, class... A> R send(const id_ref& receiver, std::string_view selector, A... args);  // objc_msgSend, typed
class autorelease_pool { … };                          // objc_autoreleasePoolPush / Pop
id_ref string(std::string_view utf8);                  // NSString from UTF-8; utf8_of(id_ref) back
// A block the framework may keep (heap block: copy/dispose helpers, R6).
template <class Signature> id_ref block(std::function<Signature> body);
// A class defined at run time: a delegate's methods, with their type encodings.
result<id_ref> define_class(std::string_view name, std::string_view superclass, std::span<const method> methods,
                            std::span<const std::string_view> protocols);
}
```

只做 mpal 第一批服务需要的最小集；以后有消费者再加（例如 Windows 的 `IAsyncOperation` 等待、macOS 的 run loop 源）。

## 4. 测试（不依赖界面，CI 能跑）

- **Windows**：在多线程套间里激活 `Windows.Foundation.Uri`（经 `factory` 取 `IUriRuntimeClassFactory`，`CreateUri`），读回 `Host`，四种文字的 UTF-8 往返 `hstring`；`create` 一个系统自带的 COM 类并 `query` 一个它不实现的接口，返回 not_supported；`delegate` 被调用一次、引用计数归零后释放。
- **macOS**：`NSString` 的四种文字往返；`[NSProcessInfo processInfo].processName` 等于本程序名；`define_class` 定义的类收到一条消息；一个 block 被 `-[NSArray enumerateObjectsUsingBlock:]` 调用 N 次后释放。
- **其他平台和镜像**：`can_run_on` 为假时代码在丢弃分支里，照常链接（沿用 R5 的检查）。

## 5. mpal 怎么用（PR 合入并发布后）

- Windows toast：未打包的 Win32 程序需要一个 AppUserModelID；mpal 在 `HKCU\Software\Classes\AppUserModelId\<id>` 登记名字和图标（经 os 的注册表能力，若缺则同样先给 os 提），`ToastNotificationManager.CreateToastNotifierWithId` 显示，`Activated` 经 `com::delegate` 收答复，进 `Notifier::next` 同一个队列。
- macOS：没有 bundle identifier 的程序不能用 `UNUserNotificationCenter`（系统会抛异常），mpal 先检查 `NSBundle.mainBundle.bundleIdentifier`，没有就返回 unavailable 并说明原因；有则请求授权、`addNotificationRequest`，delegate 的 `didReceiveNotificationResponse` 进同一个队列。
- 两者的系统调用都只经 os 的这两个平台模块；mpal 仍是一个构建，镜像里返回 unsupported。

## 6. 版本与流程

- os 0.5.0（新增平台模块 API，CHANGELOG 写明），按 os 仓库的提交习惯：单独分支、CI 的 hosted 行和 image 行全绿后经确认后合并发布；本仓库随后改依赖版本。
- 在 os 的共享检出旁另建 git worktree 工作（其他 Agent 也在用该检出）。

## 7. 待决定

1. 放在 `mcpplibs.os.windows`／`mcpplibs.os.macos` 里的子命名空间（本方案），还是新的平台模块 `mcpplibs.os.windows.com`／`mcpplibs.os.macos.objc`（更细，但 R5 的检查要多覆盖两个模块）。
2. 运行时加载 `combase.dll`／`libobjc` 而不是链接（本方案），是否符合 os 的预期。
3. Windows 注册表的读写现在只有 `machine_registry_path`（只读 HKLM）；toast 需要写 HKCU：一并在这个 PR 里加，还是另提。
