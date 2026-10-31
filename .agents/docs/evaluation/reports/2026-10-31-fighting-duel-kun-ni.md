**No winner was recorded. Kun led when the 120-turn limit stopped play.** At frame 1,200, round one remained active, the timer showed 81, both players had zero round wins, and `winner` was −1. Kun had **4,451/11,200 health (39.7%)**; Ni had **2,882/10,600 (27.2%)**.

Evidence has limits: the [semantic log](/tmp/omui-duel-5qhTZm/duel.log.omui.jsonl) contains 61 states, rather than all 121 boundaries from turn 0 through 120. Both `.md` notes are missing, but [Kun’s execution log](/tmp/omui-duel-5qhTZm/p1.log) and [Ni’s execution log](/tmp/omui-duel-5qhTZm/p2.log) include their final accounts.

**1. The match**

- **Turns 1–9: approach and missed specials.** Both dashed inward. Kun’s crouching medium kick and the fighters’ early rising specials produced no damage. By turn 9, their horizontal separation had fallen from 400 to about 103 units.
- **Turns 10–13: Kun established the lead.** His heavy punch connected for 800, then cancelled into `623HP`. The three-hit sequence dealt **1,924**, leaving Ni at 8,676 while Kun remained untouched. Ni was subsequently knocked down.
- **Turns 21–23: Ni answered the jump-in.** Kun’s airborne pressure met Ni’s `623HK`, which dealt **1,250** across two hits and knocked him down.
- **Turns 31–37: Kun punished recovery.** A jab took 325, followed by a separate rising-special hit for 756. These were separate combos in the tree, despite their proximity.
- **Turns 41–55: throws failed, lows succeeded.** Kun attempted a throw while Ni was waking up; Ni’s sweep had knocked him down by turn 43 for 850. Ni’s later throw attempt failed, and Kun punished for 715 by turn 50. Another sweep had taken 850 from Kun by turn 55.
- **Turns 58–89: repeated knockdown pressure.** Kun punished Ni’s overhead hop for **1,250** by turn 61. Ni’s account places two further sweeps at turns **72 and 77**; the sampled states confirm their combined 1,700 damage between turns 71 and 79. Kun then adapted: low guard stopped subsequent sweeps around **82–84**, and another rising-special sequence dealt **1,374** by turn 89.
- **Turns 94–110: the largest reversal of momentum.** Kun reported starting his level-one super at turn 94; turn 96 confirms the move and its 1,000-meter expenditure. It dealt **no damage** against Ni’s knockdown/wakeup sequence. Ni reported activating her answering super at turn 101. By turn 110, it had delivered **six hits for 2,099**, cutting Kun to 4,451.
- **Turns 118–120: Kun restored his advantage.** His final two-hit `623HP` sequence dealt **1,374**, leaving Ni at 2,882. That secured the ending health lead, not a round victory.

**2. The game these players encountered**

The broad action was readable. Kun’s pale clothing and Ni’s black-and-magenta silhouette distinguish them; launches, prone bodies, trails, health loss, and combo lettering communicate major outcomes. Turns **13–17** clearly progress from launcher to knockdown, while **107–110** show Ni’s super converting into sustained damage.

Precise combat timing was much harder. Both players independently complained that snapshots omit **airborne height, move progress, and remaining recovery**. `doing: "623HP"` can describe startup, active frames, or a long recovery. At turns **96–104**, move names alone cannot explain whose super is frozen, whose attacks have expired, or when guarding becomes possible.

Some exposed fields also need clearer meaning. `p1.combo` counts hits **received**, whereas `training.combo.by` identifies the attacker. `training.advantage` is a historical measurement, not a live countdown: **+67 persists through turns 102–113**, including Kun’s punishment and wakeup. An agent can easily mistake stale information for its present opportunity.

Reach mattered. Early attacks whiffed at wider spacing; Kun’s advancing heavy punch connected once closer. Ni’s sweep became a dependable grounded threat, while rising specials repeatedly defeated airborne approaches. Throws felt unreliable to both players. Kun’s attempt around **41** has a plausible rules explanation—wakeup invulnerability and post-wakeup throw protection—but the observation supplies no reason. Ni’s failed throws around **48–50** remain unresolved from these records.

Motion recognition surprised both agents: Ni reported a quarter-circle becoming a dragon punch at **turn 3**, and Kun reported similar substitutions around **4–5**. The recognizer searches recent directional history, and dragon punches have higher priority than quarter-circles. That plausibly explains the substitutions; it does not establish the exact input sequence.

Ten-frame turns enabled deliberate tactics and adaptation, but compressed short opportunities. A three-frame jab or seven-frame throw-tech window can pass entirely before the next observation. Per-frame pad sequences permit planning, yet agents cannot react inside the batch. This format therefore tests prediction and scheduling as well as fighting skill.

The evidence does not establish matchup balance. Kun begins with 600 extra health and has an advancing heavy punch; Ni moves slightly faster. However, their jabs, medium lows, sweeps, heavy rising specials, and level-one supers share substantial numerical data. This bout exposed more shared reversal-and-sweep play than distinct fighter identities. Both successfully punished mistakes; Kun’s lead is insufficient evidence that he is overpowered.

The recording itself creates apparent contradictions. The [image labelled turn 96](/tmp/omui-duel-5qhTZm/turns/0096.png) shows **Ni’s cut-in and timer 83**, while its logged state shows Kun’s super, Ni knocked down, and timer 84. The turn-107 picture shows four hits; its state records three. Separate snapshot and capture requests likely sampled different moments. These discrepancies should not be diagnosed as rendering faults without synchronized evidence. Still images and a quiet run also cannot establish sound quality, animation smoothness, or real-time latency.

**3. What this demonstrates about the engine and openas**

The engine successfully supported two independent agents through ordinary semantic actions. Simulation advanced in ten-frame batches and paused between commitments, separating thinking time from game time. Damage, meter expenditure, knockdowns, blocking, and combo totals remained sufficiently explicit to reconstruct the main exchanges.

The implementation supports deterministic simulation through fixed-point arithmetic, saved input history, copyable state, and checksums. Existing tests exercise rollback against uninterrupted runs. **This recording is not itself a determinism test:** it preserves neither complete per-frame inputs nor a replay/checksum comparison.

The semantic tree is a useful agent interface, but currently gives outcomes more reliably than causes. Health and position enabled tactical adaptation without image recognition; missing timing and event explanations forced guessing.

For openas, the harness assigns distinct actors, `agent:codex-p1` and `agent:codex-p2`. The action contract declares `turn` reversible; runtime authorization routes through `openas::decide`, and actions enter the runtime journal. That demonstrates integration of identity, effect declarations, and structured requests. The supplied artifacts do not include that journal, so they cannot independently verify every actor attribution or authorization decision.

Action answers are asymmetric: the first submission receives a waiting acknowledgement; the submission that completes the pair receives health deltas and ending states. Successful submission also does not mean the intended move executed—a valid pad may arrive during recovery, produce another recognized command, or whiff.

**4. Fixes that matter most, in order**

1. **Make observations explain combat.** Expose move frame/phase, height, facing, remaining stun/hitstop/freeze, actionable state, and throw protection. Return timestamped hit, block, whiff, and command-recognition events. Label historical advantage and combo ownership explicitly.
2. **Make evaluation evidence complete and synchronized.** Record inputs, answers, states, hashes, and pictures at simulation boundaries. Preserve player summaries before teardown. Report this run as an unfinished round; allow enough turns to reach the intended match conclusion.
3. **Resolve motion ambiguity.** Reproduce the turn-3 and turn-4–5 substitutions, show competing recognized commands, and test deliberate quarter-circles following forward movement before adjusting recognition priority or leniency.
4. **Strengthen turn delivery.** Give both players the same completed-turn result, support waiting for a specific turn, and add expected-turn IDs and duplicate handling. The logs contain **683 snapshot calls for 240 submissions**, showing substantial polling overhead. Bind actors to seats for competitive use; the stage currently accepts either player index without using `ActContext`.
5. **Then tune identity, balance, and presentation.** Run completed matches with swapped seats and varied policies before changing damage. Differentiate the fighters’ shared tools where their intended styles require it, and improve visible recovery, throw eligibility, and contact clarity around crowded exchanges.
## The players' notes

### Player 1

I reached the 120-turn limit with Kun ahead, 4,451 health to Ni’s 2,882; round one remained unfinished, with no wins awarded. I tried dashes, low pokes, heavy punches, jump-ins, throws, guards, specials, and a level-one super. Heavy punch into dragon punch worked on turns 10–13, and dragon punches scored again on turns 88–90 and 117–119. Jump and throw pressure proved unreliable, with punishments around turns 21–22 and 40–43. Low guard successfully stopped repeated sweeps on turns 82–84. My super on turn 94 wasted its opportunity against a knocked-down opponent, and Ni’s answering super hit during my recovery on turns 105–110. Positions, health, and hit effects were easy to read, but missing move-frame, recovery, and airborne-height information made precise timing difficult. Motion history also felt surprising: quarter-circle attempts around turns 4–5 produced dragon punches instead.

### Player 2

I played Ni for all 120 allowed turns; the match remained unfinished, with Ni at 2,882 health and Kun at 4,451, and no rounds won. I tried dashes, low pokes, throws, jump-ins, guarding, reversals, and a super. The reversal worked around turns 21–22, dealing 1,250 damage. Sweeps were my most reliable attack, scoring knockdowns on turns 43, 54, 72, and 77. Ni’s super connected across turns 105–110 for six hits and 2,099 damage. Overhead hops were punished on turns 60–61 and 88–89, while my close throws failed to connect around turns 48–50 without an obvious explanation. The snapshots made health, spacing, and turn readiness easy to read, but missing airborne height and move progress made recovery timing difficult. Motion recognition also felt surprising: my quarter-circle attempt produced a dragon punch on turn 3, possibly because earlier directions remained buffered.

Recording: 61 turns of 10 frames; the log and the pictures were in /tmp/omui-duel-5qhTZm.
