module gongfu;

import std;

namespace gongfu {

namespace {

struct Checker {
    std::vector<Problem> problems;
    std::string          prefix;

    void need(bool ok, std::string where, std::string text) {
        if (!ok) problems.push_back({prefix + where, std::move(text)});
    }
    void box(const Box& b, std::string where) {
        need(b.w > Fixed{} && b.h > Fixed{}, std::move(where), std::format("a box of {}×{}: both must be more than 0", b.w.to_double(), b.h.to_double()));
    }
    void frames(const Frames& f, std::int32_t total, std::string where) {
        need(f.from >= 1 && f.from <= f.to && f.to <= total, std::move(where),
             std::format("frames {}–{} of a move of {} (from 1, in order, within the move)", f.from, f.to, total));
    }
    void hit(const HitDef& h, std::int32_t total, const std::string& where) {
        frames(h.active, total, where + ".active");
        need(!h.boxes.empty(), where + ".boxes", "a hit with no box hits nothing");
        for (std::size_t i = 0; i < h.boxes.size(); ++i) box(h.boxes[i], std::format("{}.boxes[{}]", where, i));
        need(h.damage >= 0 && h.chip >= 0, where + ".damage", "damage is 0 or more");
        need(h.chip <= h.damage, where + ".chip", std::format("chip damage {} is more than the hit's {}", h.chip, h.damage));
        need(h.hitstun >= 1 && h.blockstun >= 1, where + ".hitstun", "hitstun and blockstun are at least 1 frame");
        need(h.hitstop >= 0, where + ".hitstop", "hitstop is 0 or more frames");
    }
};

}  // namespace

std::int32_t Fighter::find(std::string_view move) const noexcept {
    for (std::size_t i = 0; i < def.moves.size(); ++i)
        if (def.moves[i].name == move) return static_cast<std::int32_t>(i);
    return -1;
}

std::int32_t Fighter::startup(std::int32_t move) const noexcept {
    const MoveDef& m = def.moves[static_cast<std::size_t>(move)];
    std::int32_t first = m.total + 1;
    for (const HitDef& h : m.hits) first = std::min(first, h.active.from);
    if (m.throw_) first = std::min(first, m.throw_->active.from);
    if (m.projectile) first = std::min(first, m.projectile->spawn);
    return first;
}

std::expected<Fighter, std::vector<Problem>> compile(FighterDef def) {
    Checker c;
    c.prefix = def.name.empty() ? std::string("?") : def.name;
    c.need(!def.name.empty(), "", "a fighter has a name");
    c.need(def.health > 0, ".health", "health is more than 0");
    c.need(def.gravity > Fixed{}, ".gravity", "gravity pulls down: more than 0");
    c.need(def.jump_up > Fixed{}, ".jump_up", "a jump goes up: more than 0");
    c.need(def.prejump >= 1 && def.landing >= 0, ".prejump", "prejump is at least 1 frame, landing 0 or more");
    c.box(def.push, ".push");
    c.box(def.hurt_standing, ".hurt_standing");
    c.box(def.hurt_crouching, ".hurt_crouching");
    c.box(def.hurt_air, ".hurt_air");

    Fighter f;
    for (std::size_t i = 0; i < def.moves.size(); ++i) {
        const MoveDef& m = def.moves[i];
        const std::string at = "/" + (m.name.empty() ? std::format("moves[{}]", i) : m.name);
        c.need(!m.name.empty(), at, "a move has a name");
        for (std::size_t j = 0; j < i; ++j) c.need(def.moves[j].name != m.name, at, std::format("two moves are called `{}`", m.name));
        c.need(m.total >= 1, at + ".total", "a move lasts at least 1 frame");
        c.need(m.command.buttons != 0, at + ".command", "a move is asked for with at least one button");
        for (std::size_t h = 0; h < m.hits.size(); ++h) c.hit(m.hits[h], m.total, std::format("{}.hits[{}]", at, h));
        for (std::size_t k = 0; k < m.invulnerable.size(); ++k) c.frames(m.invulnerable[k], m.total, std::format("{}.invulnerable[{}]", at, k));
        for (std::size_t s = 0; s < m.steps.size(); ++s) {
            c.need(m.steps[s].frame >= 1 && m.steps[s].frame <= m.total, std::format("{}.steps[{}]", at, s),
                   std::format("a step at frame {} of a move of {}", m.steps[s].frame, m.total));
            if (s > 0) c.need(m.steps[s - 1].frame < m.steps[s].frame, std::format("{}.steps[{}]", at, s), "steps in order of their frames");
        }
        if (m.throw_) {
            c.frames(m.throw_->active, m.total, at + ".throw.active");
            c.box(m.throw_->range, at + ".throw.range");
            c.need(m.throw_->tech_window >= 0, at + ".throw.tech_window", "0 or more frames");
        }
        if (m.projectile) {
            c.need(m.projectile->spawn >= 1 && m.projectile->spawn <= m.total, at + ".projectile.spawn",
                   std::format("spawned at frame {} of a move of {}", m.projectile->spawn, m.total));
            c.need(m.projectile->life >= 1, at + ".projectile.life", "a projectile lives at least 1 frame");
            c.hit(m.projectile->hit, m.projectile->life, at + ".projectile.hit");
        }
        c.need(m.meter_cost >= 0 && m.meter_gain >= 0 && m.freeze >= 0, at + ".meter", "meter and freeze are 0 or more");
        for (std::size_t k = 0; k < m.cancels.size(); ++k) {
            const CancelDef& cd = m.cancels[k];
            c.frames(cd.frames, m.total, std::format("{}.cancels[{}].frames", at, k));
            for (const std::string& into : cd.into) {
                std::int32_t t = -1;
                for (std::size_t j = 0; j < def.moves.size(); ++j)
                    if (def.moves[j].name == into) t = static_cast<std::int32_t>(j);
                c.need(t >= 0, std::format("{}.cancels[{}]", at, k), std::format("no move is called `{}`", into));
            }
        }
    }
    if (!c.problems.empty()) return std::unexpected(std::move(c.problems));
    // Resolved: per move, per cancel, the moves it allows, in the order named.
    f.cancel_targets.assign(def.moves.size(), {});
    for (std::size_t i = 0; i < def.moves.size(); ++i)
        for (const CancelDef& cd : def.moves[i].cancels) {
            auto& into = f.cancel_targets[i].emplace_back();
            for (const std::string& name : cd.into)
                for (std::size_t j = 0; j < def.moves.size(); ++j)
                    if (def.moves[j].name == name) into.push_back(static_cast<std::int32_t>(j));
        }
    f.def = std::move(def);
    return f;
}

std::vector<Problem> check(const SystemDef& s) {
    Checker c;
    c.prefix = "system";
    c.need(!s.scaling.empty(), ".scaling", "combo scaling has at least one step");
    for (std::size_t i = 0; i < s.scaling.size(); ++i)
        c.need(s.scaling[i] >= 1 && s.scaling[i] <= 100, std::format(".scaling[{}]", i), "a step is 1–100 percent");
    c.need(s.counter_damage >= 100, ".counter_damage", "a counter hit does at least the damage of a hit (100 percent)");
    c.need(s.counter_hitstun >= 0, ".counter_hitstun", "0 or more frames");
    c.need(s.round_frames >= 1, ".round_frames", "a round lasts at least 1 frame");
    c.need(s.start_distance > Fixed{} && s.start_distance < s.stage_width, ".start_distance", "the fighters start apart, inside the stage");
    c.need(s.meter_max >= 0 && s.knockdown >= 1 && s.wakeup >= 0, ".knockdown", "meter 0 or more; down at least 1 frame");
    return c.problems;
}

}  // namespace gongfu
