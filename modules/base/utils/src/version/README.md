# mlibs.version

Versions compared, the one implementation: part by part as numbers where both parts are numbers (`0.1.10` after `0.1.9`, a missing part is 0), else as text; a pre-release (`0.1.0-dev.3`) before its release, two pre-releases compared as the version is. Its users: omui's launcher choosing the newest installed runtime that satisfies an application, and the release tool ordering its packs.

```cpp
import mlibs.version;
if (mlibs::version::compare(installed, wanted) >= 0) { /* new enough */ }
```
