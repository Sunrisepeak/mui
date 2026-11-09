mpal — the services an operating system gives an application, as one contract on every platform (desktop, mobile, the Web, embedded): notifications first.

# mpal

The platform abstraction above [`mcpplibs.os`](https://github.com/mcpplibs/os): openkal → `mcpplibs.os` (the system's primitives: files and their watcher, processes, local connections, the environment) → **mpal** (the system's *services*, thought of from the service's side). Nothing here draws: the interface's own drawn versions, and the choice between native and drawn, are omui's (`Runtime::NativeSurfaces`, the SDK's `omui::System`).

## Services, by what they are for

Each service is a partition with one contract; a platform that does not give it says so (`Unsupported`), never pretends. What the person answers (a notification's action, a share's target) comes back as the service's own event, not through input.

| Service | What it is for | Platforms (planned) |
|---|---|---|
| `notify` | telling the person something outside the application's view, and their answer | Linux: freedesktop Notifications over D-Bus (done) · Windows: toast · macOS/iOS: UserNotifications · Android: NotificationManager · Web: Notification API |
| `pick` | the person choosing files or folders the system holds (storage access) | Linux: the desktop portal · Windows/macOS: the file dialogs · iOS/Android: the document pickers · Web: File System Access |
| `transfer` | data handed between applications: clipboard, share, drag and drop | |
| `launch` | how an application is started and found: open a URL, file associations and deep links, single instance, start at login | |
| `shell` | the system's surfaces for an application: tray or status item, menu bar, dock and taskbar badges, shortcuts | |
| `secrets` | the system's keychain or keystore | |
| `power` | keeping the device awake, battery and thermal state | |

Input — keys, pointers, touch, gamepads, the IME — is not a service here: it arrives with the window (omui's hosts, SDL3). Device services the input does not cover (haptics, sensors) would be partitions of their own.

The D-Bus wire format (`:dbus`) is its own partition: marshalling, the EXTERNAL authentication and a method call's reply — no libdbus (its licence) and no sd-bus.

Why a package of its own (D-99): it is general (no UI, no omui), its platform code is isolated from the layers that use it, and it is meant to leave the repository once its contracts settle.
