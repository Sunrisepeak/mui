mpal — the services an operating system gives an application, as one contract on every platform (desktop, mobile, the Web, embedded): notifications first.

# mpal

The platform abstraction above [`mcpplibs.os`](https://github.com/mcpplibs/os): openkal → `mcpplibs.os` (the system's primitives: files and their watcher, processes, local connections, the environment) → **mpal** (the system's *services*, thought of from the service's side). Nothing here draws: the interface's own drawn versions, and the choice between native and drawn, are omui's (`Runtime::NativeSurfaces`, the SDK's `omui::System`).

## Services, by what they are for

Each service is a partition with one contract; a platform that does not give it says so (`Unsupported`), never pretends. What the person answers (a notification's action, a share's target) comes back as the service's own event, not through input.

| Service | What it is for | Platforms (planned) |
|---|---|---|
| `notify` | telling the person something outside the application's view, and their answer | Linux: freedesktop Notifications over D-Bus, with the answers (an action chosen, dismissed, expired, closed) read back from its signals (done) · Windows: toasts through the Windows Runtime, the application named by a Start menu shortcut (done) · macOS: UserNotifications, for an .app (done) · iOS: UserNotifications · Android: NotificationManager · Web: Notification API |
| `pick` | the person choosing files or folders the system holds (storage access) | Linux: the desktop portal's FileChooser over D-Bus (done; omui's desktop host uses it when a portal is there) · Windows/macOS: the file dialogs · iOS/Android: the document pickers · Web: File System Access |
| `transfer` | data handed between applications: clipboard, share, drag and drop | |
| `launch` | how an application is started and found: open a URL, file associations and deep links, single instance, start at login | |
| `shell` | the system's surfaces for an application: tray or status item, menu bar, dock and taskbar badges, shortcuts | |
| `secrets` | the system's keychain or keystore | |
| `power` | keeping the device awake, battery and thermal state | |

Input — keys, pointers, touch, gamepads, the IME — is not a service here: it arrives with the window (omui's hosts, SDL3). Device services the input does not cover (haptics, sensors) would be partitions of their own.

## One build for every system, the openkal ecosystem first-class

mpal is one build: a backend is chosen with `mcpplibs.os`'s platform questions — `if constexpr (os::can_run_on(platform))` decides at compile time for a native build, `os::host()` when an ok image (`x86_64-openkal`, `aarch64-openkal`) is loaded, since an image runs on every system. A service spoken as a protocol (D-Bus, later openbus) is ordinary code and works from an image as from a native build; `mcpp test -p mpal --target x86_64-openkal` runs the suite as an image (CI does). A service a system gives as objects rather than a protocol (Windows's toasts, macOS's notification centre) is spoken through `mcpplibs.os`'s object models (`windows::com`, `macos::objc`, os 0.4.4) in that system's own unit (`src/windows`, `src/macos`, built for it alone: `mpal:native`) — no system header anywhere; an image is no native build of either and says `unsupported` there until openkal or a system service (Luban's bus) carries it. Platform-only extras, if a program wants them, are thin packages of their own (`mpal.linux`, …) over the same code.

## os or mpal

| `mcpplibs.os` — the system's primitives | mpal — the system's services |
|---|---|
| a call the kernel or the C library answers: files and their watcher, processes, local connections and sockets, the environment and known directories, time, randomness, the terminal, dynamic libraries, interrupts, single instance (a primitive over a local name), the platform questions | a conversation with a service the session or the system runs, with an effect the person sees or a policy behind it, answered asynchronously, and different on each system: notify, pick, transfer, launch (open URL by the system's handler, associations, login items), shell, secrets, power, devices, the person's settings (dark mode, reduced motion, contrast, locale) |
| no daemon, no UI meaning, needed by a command-line tool as much as an application | the services an application, a desktop (xdesk) or a system (Luban) is made of |

A primitive mpal needs and os lacks is asked of os; a service never goes into os. Drawing is neither's: mpal takes pixels (an icon as RGBA), xrender draws; a service that is given pixels and gives back events (a surface, opensal's sense) would be a package above xrender (`mpal.surface`, gfx layer), so mpal itself stays at the base layer.

The D-Bus wire format (`:dbus`) is its own partition: marshalling, the EXTERNAL authentication, a method call's reply and the signals a match rule asks for (kept in order while a call waits) — no libdbus (its licence) and no sd-bus.

A service answered asynchronously is read by pulling (`Notifier::next(wait)`): mpal starts no threads. A program that must not wait where it shows (an interface's thread) listens on a second connection in a thread of its own (`Notifier::listen()`), as omui's desktop host does.

Why a package of its own (D-99): it is general (no UI, no omui), its platform code is isolated from the layers that use it, and it is meant to leave the repository once its contracts settle.
