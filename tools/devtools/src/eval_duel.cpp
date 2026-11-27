// eval duel (moved from tools/eval/duel.sh): two codex agents play the fighting game against each other, by turns,
// and a third looks at the recording (the fighting game's use evidence, and the engine's and openas's). The game
// runs headless as `omui-fight --fight P1,P2 --turns N`: the match stands until both players have said what they
// hold for the next N frames (the stage's `turn` action), then plays them. Each player is a codex with omui's MCP
// server, its own actor (agent:codex-p1, agent:codex-p2). Meanwhile every turn is recorded: the stage's state as the
// tree says it (duel.log.xdef.jsonl) and its picture. Then the analyst — a third codex, read-only — gets contact
// sheets of the recording, the log and both players' notes, and writes the report.
module omui.devtools.eval;

import std;
import mcpplibs.os;
import nlohmann.json;
import mlibs.image;
import omui.devtools.run;

namespace omui::devtools::eval {

namespace {

namespace fs = std::filesystem;
namespace os = mcpplibs::os;
using nlohmann::json;

// 3 × 5 digits for the turn numbers under each picture of a contact sheet.
constexpr std::array<std::array<std::string_view, 5>, 10> digits{{
    {"111", "101", "101", "101", "111"}, {"010", "110", "010", "010", "111"}, {"111", "001", "111", "100", "111"}, {"111", "001", "111", "001", "111"},
    {"101", "101", "111", "001", "001"}, {"111", "100", "111", "001", "111"}, {"111", "100", "111", "101", "111"}, {"111", "001", "010", "010", "010"},
    {"111", "101", "111", "101", "111"}, {"111", "101", "111", "001", "111"},
}};

// `picture` scaled to width × height (box filter: each target pixel the mean of the source pixels it covers) and laid
// at (x, y) on an opaque sheet.
void place(mlibs::image::Image& sheet, const mlibs::image::Image& picture, int x, int y, int width, int height) {
    for (int row = 0; row < height; ++row) {
        const int from_y = row * picture.height / height, to_y = std::max(from_y + 1, (row + 1) * picture.height / height);
        for (int column = 0; column < width; ++column) {
            const int from_x = column * picture.width / width, to_x = std::max(from_x + 1, (column + 1) * picture.width / width);
            std::array<std::uint64_t, 3> sum{};
            std::uint64_t count = 0;
            for (int source_y = from_y; source_y < to_y; ++source_y)
                for (int source_x = from_x; source_x < to_x; ++source_x) {
                    const auto pixel = picture.row(source_y).subspan(static_cast<std::size_t>(source_x) * 4, 4);
                    for (int channel = 0; channel < 3; ++channel) sum[channel] += pixel[channel];
                    ++count;
                }
            auto to = sheet.row(y + row).subspan(static_cast<std::size_t>(x + column) * 4, 4);
            for (int channel = 0; channel < 3; ++channel) to[channel] = static_cast<std::uint8_t>(sum[channel] / count);
        }
    }
}

void number_at(mlibs::image::Image& sheet, int value, int x, int y, int scale = 2) {
    const std::string text = std::to_string(value);
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto& glyph = digits[text[index] - '0'];
        for (int line = 0; line < 5; ++line)
            for (int column = 0; column < 3; ++column)
                if (glyph[line][column] == '1')
                    for (int dy = 0; dy < scale; ++dy)
                        for (int dx = 0; dx < scale; ++dx) {
                            auto pixel = sheet.row(y + line * scale + dy).subspan(static_cast<std::size_t>(x + (static_cast<int>(index) * 4 + column) * scale + dx) * 4, 4);
                            pixel[0] = pixel[1] = pixel[2] = 220;
                        }
    }
}

// Every turn's picture, 24 a sheet (6 × 4 of 320 × 180, the turn's number under each).
std::vector<fs::path> contact_sheets(const fs::path& work) {
    std::vector<fs::path> shots, sheets;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(work / "turns", error))
        if (entry.path().extension() == ".png") shots.push_back(entry.path());
    std::ranges::sort(shots);
    for (std::size_t first = 0; first < shots.size(); first += 24) {
        mlibs::image::Image sheet;
        sheet.width = 6 * 320;
        sheet.height = 4 * 200;
        sheet.rgba.resize(static_cast<std::size_t>(sheet.width) * sheet.height * 4);
        for (std::size_t at = 0; at < sheet.rgba.size(); at += 4) sheet.rgba[at] = sheet.rgba[at + 1] = 12, sheet.rgba[at + 2] = 16, sheet.rgba[at + 3] = 255;
        for (std::size_t index = first; index < std::min(first + 24, shots.size()); ++index) {
            auto picture = mlibs::image::load_png(shots[index]);
            if (!picture) continue;
            const int x = static_cast<int>((index - first) % 6) * 320, y = static_cast<int>((index - first) / 6) * 200;
            place(sheet, *picture, x, y, 320, 180);
            int turn = 0;
            const std::string stem = os::fs::utf8(shots[index].stem());
            std::from_chars(stem.data(), stem.data() + stem.size(), turn);
            number_at(sheet, turn, x + 4, y + 184);
        }
        const fs::path out = work / std::format("sheet-{:02}.png", first / 24);
        if (mlibs::image::save_png(out, mlibs::image::view(sheet))) sheets.push_back(out);
    }
    return sheets;
}

// A player's last message: its notes, or the last thing it said in its log.
std::string notes_of(const fs::path& work, int me) {
    if (const fs::path notes = work / std::format("p{}.md", me); fs::exists(notes)) return os::fs::read(notes).value_or("");
    const std::string log = os::fs::read(work / std::format("p{}.log", me)).value_or("");
    if (log.empty()) return "(none)";
    std::string buffer, last;
    bool on = false;
    for (const auto part : std::views::split(std::string_view(log), '\n')) {
        const std::string_view line(part.begin(), part.end());
        if (line == "codex") {
            buffer.clear();
            on = true;
            continue;
        }
        if (line.starts_with("mcp") || line.starts_with("exec") || line.starts_with("tokens used")) on = false;
        if (on) buffer += std::string(line) + "\n";
    }
    return buffer.empty() ? "(none)" : buffer;
}

void analyse(const Duel& duel, const fs::path& work, const fs::path& report, int seen) {
    const auto sheets = contact_sheets(work);
    const std::string prompt = std::format(
        "Two AI agents just played a match of openmui's fighting game against each other, by turns of {0} frames: player 1 as {1}, player 2 as\n{2}. "
        "You are the analyst. The attached contact sheets show the picture after each turn (turn numbers under each); the log of every turn's\nstate "
        "as the semantic tree says it is {3}/duel.log.xdef.jsonl; the players' own notes are {3}/p1.md and {3}/p2.md (missing if a\nplayer stopped "
        "early). The game's code is showcase/fighting and modules/projects/gongfu (read AGENTS.md, showcase/fighting/README.md and the\nfighting "
        "evaluation subject .agents/docs/evaluation/subjects/fighting.md).\nWrite a report in English, under 1500 words:\n1. The match: who won and how "
        "— the decisive exchanges, by turn number.\n2. The game as these players met it: readability (could they and you tell what happened from the "
        "tree and the picture), the moves'\n   reach and feel, the turn-based play itself, fairness and balance between these two fighters, anything "
        "broken or odd (with turn numbers).\n3. What it shows of the engine and of openas: deterministic simulation, the tree as an agent's view, "
        "actions and their answers.\n4. The fixes that would matter most, in order.",
        duel.frames, duel.p1, duel.p2, os::fs::utf8(work));
    std::vector<std::string> args{"exec", "--sandbox", "read-only", "-c", "approval_policy=\"never\"", "-c", "model_reasoning_effort=\"high\"", "-o", os::fs::utf8(report), prompt};
    if (!sheets.empty()) {
        args.push_back("-i");  // after the prompt: -i takes every path that follows
        for (const auto& sheet : sheets) args.push_back(os::fs::utf8(sheet));
    }
    const auto analyst = run::program(duel.root, "codex", std::move(args), {.merged = true});
    std::ofstream(work / "analyst.log", std::ios::binary) << (analyst ? analyst->out : analyst.error());
    if (!analyst || analyst->status != 0) std::cerr << std::format("omui-devtools eval duel: the analyst failed (see {})\n", os::fs::utf8(work / "analyst.log"));
    std::ofstream out(report, std::ios::binary | std::ios::app);
    out << "\n## The players' notes\n";
    for (int me = 1; me <= 2; ++me) out << std::format("\n### Player {}\n\n{}", me, notes_of(work, me));
    out << std::format("\nRecording: {} turns of {} frames; the log and the pictures were in {}.\n", seen, duel.frames, os::fs::utf8(work));
}

std::string player_prompt(const Duel& duel, int side) {
    const int me = side + 1, them = 2 - side;
    return std::format(
        "You are player {0} (player index {1}) in a fighting game match against another AI agent (player {2}). Play to win.\n\nThe game (openmui's "
        "fighting game, gongfu) runs now; the openmui MCP tools reach it. The match is played BY TURNS: it stands still until\nboth players have said "
        "what they hold for the next {3} frames (1/6 s at 60 fps), then plays those frames.\n- Read the stage: ui_snapshot {{root: \"main/stage\"}} — "
        "its value has frame, seconds, round, wins, and for each player: name, health, doing\n  (the move or state), x and y (position and height; the "
        "stage is about 1920 wide), facing, actionable (whether it can act now), move\n  {{name, frame, of, phase: startup|active|recovery, left}}, stun, "
        "hitstop, throw_protected, combo (hits it has taken), posture, meter; and\n  turns {{played, frames, waiting: [players still to say], last: what "
        "happened in the last turn — each move begun (what your pads were read\n  as), hit, block, throw … by whom, with damage}}.\n  ui_capture shows "
        "the picture (look now and then: the fighters, the HUD, the effects).\n- Act: ui_act {{node: \"main/stage\", action: \"turn\", args: {{player: "
        "{1}, pad: PAD}}}}. PAD is numpad notation: a direction 1–9 (5 neutral,\n  6 toward the right of the screen, 4 left, 2 down, 8 up, 3 down-right "
        "…) and buttons LP MP HP LK MK HK (e.g. \"5LP\", \"2MK\", \"6HP\",\n  \"5LP+LK\"). Several pads, one a frame, the last held to the turn's end, "
        "make motions: \"2,3,6LP\" (a quarter circle toward the right and a\n  light punch), \"6,2,3HP\" (a dragon punch), \"4,1,2,3,6HK\". Directions are "
        "on the screen, not relative to facing: check where you and the\n  other player stand (x) to know forward and back. Hold back (away from the "
        "other) to guard, down-back to guard low.\n- After your turn the answer says whether the other player has said theirs; if not, read the stage "
        "again (ui_snapshot) until turns.played\n  grows, then decide your next turn. Do not wait idly for long: poll a few times a second.\n- Your "
        "fighter's moves: read showcase/fighting/fighters/<name>.fighter.gongfu.toml (each [[move]]: name, input, damage …).\nPlay the whole match: keep "
        "taking turns until the stage says over, or after {4} turns of yours. Mix approaches: walk, jump, poke, guard,\nthrow (LP+LK near), specials, "
        "supers when your meter allows. Then, as your last message, write 5–10 sentences: what you tried, what\nworked, what the game made easy or hard "
        "to read and to do (as a player), anything that looked or felt wrong, and the turn numbers where.",
        me, side, them, duel.frames, duel.turns);
}

}  // namespace

int duel(const Duel& duel) {
    const fs::path report = duel.root / ".agents/docs/evaluation/reports" / std::format("{}-fighting-duel-{}-{}.md", duel.date.empty() ? run::today() : duel.date, duel.p1, duel.p2);
    if (!duel.analyse.empty()) {  // a recording made before, analysed again
        const std::string log = os::fs::read(duel.analyse / "duel.log.xdef.jsonl").value_or("");
        analyse(duel, duel.analyse, report, static_cast<int>(std::ranges::count(log, '\n')));
        return 0;
    }
    const auto built = run::build(duel.root, {"--profile", "release", "-p", "omui.cli", "-p", "omui.showcase.fighting"});
    fs::path omui, fight;
    if (built)
        for (const auto& artifact : *built) {
            if (artifact.name == "omui") omui = artifact.path;
            if (artifact.name == "omui-fight") fight = artifact.path;
        }
    if (omui.empty() || fight.empty()) {
        std::cerr << "omui-devtools eval duel: the release build failed" << (built ? std::string() : ": " + built.error()) << '\n';
        return 1;
    }
    const fs::path work = fs::temp_directory_path() / std::format("omui-duel-{}", std::random_device{}());
    std::error_code error;
    fs::create_directories(work / "ep", error);
    fs::create_directories(work / "turns", error);
    os::process::spawn_options launch;
    launch.program = fight;
    launch.args = {"--fight", std::format("{},{}", duel.p1, duel.p2), "--turns", std::to_string(duel.frames), "--quiet", "--serve", "--agent-dir", os::fs::utf8(work / "ep")};
    launch.in = launch.out = launch.err = os::process::stdio::null;
    launch.own_unit = true;
    auto game = os::process::process::spawn(launch);
    if (!game) {
        std::cerr << std::format("omui-devtools eval duel: the game did not start: {}\n", game.error().message());
        return 1;
    }
    for (int attempt = 0; attempt < 300 && !fs::exists(work / "ep" / "org.openmui.fighting.endpoint.xdef.json"); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // The players, each its own actor through omui's MCP server.
    std::vector<os::process::process> players;
    for (int side = 0; side < 2; ++side) {
        const int me = side + 1;
        os::process::spawn_options player;
        player.program = "codex";
        player.args = {"exec", "--sandbox", "read-only", "-c", "approval_policy=\"never\"", "-c", "model_reasoning_effort=\"low\"", "-c",
                       std::format("mcp_servers.openmui.command=\"{}\"", os::fs::utf8(omui)), "-c",
                       std::format("mcp_servers.openmui.args=[\"mcp\",\"--dir\",\"{}\",\"--actor\",\"agent:codex-p{}\"]", os::fs::utf8(work / "ep"), me), "-c",
                       "mcp_servers.openmui.tool_timeout_sec=120", "-c", "mcp_servers.openmui.default_tools_approval_mode=\"approve\"", "-o",
                       os::fs::utf8(work / std::format("p{}.md", me)), player_prompt(duel, side)};
        player.cwd = duel.root;
        player.in = os::process::stdio::null;
        player.out = os::process::stdio::pipe;
        player.err = os::process::stdio::output;
        player.own_unit = true;
        auto started = os::process::process::spawn(player);
        if (!started) {
            std::cerr << std::format("omui-devtools eval duel: player {} did not start: {}\n", me, started.error().message());
            return 1;
        }
        players.push_back(std::move(*started));
    }
    // Each player's log, drained as it comes.
    std::vector<std::jthread> logs;
    for (std::size_t index = 0; index < players.size(); ++index)
        logs.emplace_back([&, index] {
            std::ofstream out(work / std::format("p{}.log", index + 1), std::ios::binary);
            for (;;) {
                auto chunk = players[index].read_output();
                if (!chunk || chunk->empty()) break;
                out << *chunk << std::flush;
            }
        });

    // The recording: each turn played, its state and its picture.
    const std::string where = os::fs::utf8(work / "ep");
    int seen = -1;
    const auto started = std::chrono::steady_clock::now();
    std::ofstream log(work / "duel.log.xdef.jsonl", std::ios::binary);
    const auto running = [&](os::process::process& player) {
        const auto ended = player.try_wait();
        return ended && !*ended;
    };
    while (running(players[0]) || running(players[1])) {
        const auto state = run::program(duel.root, os::fs::utf8(omui), {"agent", "snapshot", "org.openmui.fighting", "--root", "main/stage", "--dir", where},
                                        {.timeout = std::chrono::seconds(30)});
        int played = -1;
        bool over = false;
        json value;
        if (state) {
            const auto first_line = std::string_view(state->out).substr(0, state->out.find('\n'));
            const auto parsed = json::parse(first_line, nullptr, false);
            if (!parsed.is_discarded() && parsed.contains("data") && parsed["data"].contains("nodes") && !parsed["data"]["nodes"].empty()) {
                value = parsed["data"]["nodes"][0].value("value", json());
                if (value.is_object() && value.contains("turns")) {
                    played = value["turns"].value("played", -1);
                    over = value.value("over", false);
                }
            }
        }
        if (played > seen) {
            seen = played;
            log << value.dump() << '\n' << std::flush;
            static_cast<void>(run::program(duel.root, os::fs::utf8(omui),
                                           {"agent", "capture", "org.openmui.fighting", "--out", os::fs::utf8(work / "turns" / std::format("{:04}.png", played)), "--dir", where},
                                           {.timeout = std::chrono::seconds(30)}));
        }
        if (over || seen >= duel.turns || std::chrono::steady_clock::now() - started > duel.timeout) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::this_thread::sleep_for(std::chrono::seconds(20));  // the players' last words
    // What the round was played with, and where it led (a replay's check).
    if (const auto inputs = run::program(duel.root, os::fs::utf8(omui), {"agent", "act", "org.openmui.fighting", "main/stage", "inputs", "--dir", where}))
        std::ofstream(work / "inputs.xdef.jsonl", std::ios::binary) << inputs->out;
    for (auto& player : players) player.kill();
    logs.clear();
    game->kill();
    static_cast<void>(game->wait());
    analyse(duel, work, report, seen);
    std::cout << std::format("omui-devtools eval duel: {} turns → {} (recording in {})\n", seen, os::fs::utf8(fs::relative(report, duel.root)), os::fs::utf8(work));
    return 0;
}

}  // namespace omui::devtools::eval
