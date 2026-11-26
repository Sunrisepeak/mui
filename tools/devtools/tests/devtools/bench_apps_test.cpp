// omui-devtools bench apps: what is read of a process from outside it — pmap's totals by the
// header's names (private dirty, USS = private clean + dirty, PSS, RSS), pidstat's context switches
// a second summed over the threads — and the pictures' hash and uniformity the start is judged by.
// Launching real windows is the command's own run (it needs an X server and the competitors'
// builds), not this test's.
import std;
import mcpplibs.mtest;
import omui.devtools.bench;
import omui.devtools.x11;

using namespace mcpplibs::mtest;
namespace bench = omui::devtools::bench;
namespace x11 = omui::devtools::x11;

namespace {

// pmap -XX's shape (procps-ng 4.0.4): the header, a mapping, the rule, the totals in KiB.
constexpr std::string_view pmap_output =
    "1234:   ./omui-bench-hello\n"
    "         Address Perm   Offset Device    Inode  Size KernelPageSize MMUPageSize   Rss   Pss "
    "Pss_Dirty Shared_Clean Shared_Dirty Private_Clean "
    "Private_Dirty Referenced Anonymous KSM LazyFree AnonHugePages ShmemPmdMapped FilePmdMapped "
    "Shared_Hugetlb Private_Hugetlb Swap SwapPss Locked "
    "THPeligible ProtectionKey              VmFlags Mapping\n"
    "    5d1a0000 r--p 00000000  103:05 13108653   192              4           4   192    23      "
    "   0          192            0             0 "
    "            0        192         0   0        0             0              0             0    "
    "          0               0    0       0      0 "
    "          0             0          rd mr mw me sd omui-bench-hello\n"
    "                                               ===== ============== =========== ===== ===== "
    "========= ============ ============ ============= "
    "============= ========== ========= === ======== ============= ============== ============= "
    "============== =============== ==== ======= ====== "
    "=========== ============= \n"
    "                                               30640            888         888 22624 17235   "
    "  14636         5808            0          2180 "
    "        14636      22500     14636   0        0             0              0             0    "
    "          0               0    8       0      0 "
    "          0             0 KB \n";

constexpr std::string_view pidstat_output =
    "Linux 6.8.0 (host)  11/19/2026  _x86_64_  (32 CPU)\n"
    "\n"
    "10:16:11 AM   UID      TGID       TID   cswch/s nvcswch/s  Command\n"
    "10:16:16 AM  1000      4242         -      2.00      0.20  omui\n"
    "10:16:16 AM  1000         -      4242      2.00      0.20  |__omui\n"
    "10:16:16 AM  1000         -      4250      1.00      0.40  |__omui-render\n"
    "Average:     1000      4242         -      2.00      0.20  omui\n"
    "Average:     1000         -      4242      2.00      0.20  |__omui\n"
    "Average:     1000         -      4250      1.00      0.40  |__omui-render\n";

}  // namespace

static UTest cases{
    "devtools.bench.apps", [] {
        "pmap's totals are read by the header's names: RSS, PSS, USS (private clean and dirty), private dirty, swap"_test =
            [] {
                const auto memory = bench::memory_of(pmap_output);
                expect(fatal(memory.has_value()));
                expect(eq(memory->rss, std::uint64_t{22624} * 1024));
                expect(eq(memory->pss, std::uint64_t{17235} * 1024));
                expect(eq(memory->uss, std::uint64_t{2180 + 14636} * 1024))
                    << "USS = private clean + private dirty";
                expect(eq(memory->private_dirty, std::uint64_t{14636} * 1024));
                expect(eq(memory->swap, std::uint64_t{8} * 1024));
            };

        "a pmap without its totals or its names says nothing"_test = [] {
            expect(!bench::memory_of("1234: ./x\n").has_value());
            expect(!bench::memory_of("  Address Perm Offset Device Inode Size Rss\n 1 2 KB\n")
                        .has_value())
                << "no PSS or private columns";
        };

        "pidstat: the threads' averages summed (voluntary and not); the process's own line and the per-second lines left out"_test =
            [] {
                const auto switches = bench::switches_of(pidstat_output);
                expect(fatal(switches.has_value()));
                expect(std::abs(*switches - 3.6) < 1e-9) << *switches;
                expect(!bench::switches_of("Linux 6.8.0\n").has_value());
                // Which threads woke, by name: what an idle process's switches are made of.
                const auto waking = bench::waking_of(pidstat_output);
                expect(waking.size() == 2 && std::abs(waking.at("omui") - 2.2) < 1e-9 && std::abs(waking.at("omui-render") - 1.4) < 1e-9)
                    << waking.size();
            };

        "a picture's hash ignores the unused byte; a cleared window is uniform, a drawn one is not"_test =
            [] {
                const std::vector<std::uint32_t> cleared(64, 0x00f8f9fau),
                    with_pad(64, 0xfff8f9fau);
                std::vector<std::uint32_t> drawn = cleared;
                drawn[17] = 0x00101828u;
                expect(x11::uniform(cleared) && !x11::uniform(drawn));
                expect(eq(x11::hash(cleared), x11::hash(with_pad)))
                    << "X's 32-bit pixels carry a byte nothing draws";
                expect(x11::hash(cleared) != x11::hash(drawn));
            };

        "a desktop's display is authorised by the session's cookie: this machine's entry for the display, else a wildcard's"_test =
            [] {
                // Xauthority's entries: a family (big-endian), then the address, the display's
                // number, the scheme and its data, each counted by two big-endian bytes.
                const auto entry = [](std::uint16_t family, std::string_view address,
                                      std::string_view number, std::string_view name,
                                      std::string_view data) {
                    std::string out{static_cast<char>(family >> 8), static_cast<char>(family & 0xff)};
                    for (const std::string_view part : {address, number, name, data}) {
                        out += static_cast<char>(part.size() >> 8);
                        out += static_cast<char>(part.size() & 0xff);
                        out += part;
                    }
                    return out;
                };
                const std::string file =
                    entry(256, "other-pc", "1", "MIT-MAGIC-COOKIE-1", "AAAA") +
                    entry(65535, "", "", "MIT-MAGIC-COOKIE-1", "WILD") +
                    entry(256, "this-pc", "0", "MIT-MAGIC-COOKIE-1", "ZERO") +
                    entry(256, "this-pc", "1", "XDM-AUTHORIZATION-1", "XDMX") +
                    entry(256, "this-pc", "1", "MIT-MAGIC-COOKIE-1", "MINE");
                expect(eq(x11::cookie(file, 1, "this-pc").value_or("none"), std::string("MINE")));
                expect(eq(x11::cookie(file, 0, "this-pc").value_or("none"), std::string("ZERO")));
                expect(eq(x11::cookie(file, 7, "this-pc").value_or("none"), std::string("WILD")))
                    << "no entry of this machine's for :7: the wildcard's";
                expect(eq(x11::cookie(file, 1, "third-pc").value_or("none"), std::string("WILD")))
                    << "another host's local entry is not this machine's";
                expect(!x11::cookie("", 1, "this-pc").has_value());
                expect(!x11::cookie(file.substr(0, 9), 1, "this-pc").has_value())
                    << "a cut-off file says nothing";
            };
    }};
