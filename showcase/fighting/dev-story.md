# Dev story — the fighting game

How `omui-fight` is made, told from the developer's side: what was built, at which layer, with which of the framework's capabilities, how every asset was made and with which tools, how fast it runs, and what the making taught us. The quality bar is the [strict acceptance rubric](../../.agents/docs/2026-10-29-fighting-acceptance-rubric.md); the combat design is in [the hardcore combat design](../../.agents/docs/2026-10-29-fighting-hardcore-combat-design.md).

![墨's level-3 super](media/mo-level-3.webp)

**Contents**

1. [What we set out to make](#1-what-we-set-out-to-make)
2. [The layers](#2-the-layers)
3. [The fighters](#3-the-fighters)
4. [Combat systems](#4-combat-systems)
5. [How a fighter is made: the workflow](#5-how-a-fighter-is-made-the-workflow)
6. [Tools](#6-tools)
7. [The frame: presentation as a pure function](#7-the-frame-presentation-as-a-pure-function)
8. [Sound and music](#8-sound-and-music)
9. [Performance](#9-performance)
10. [Self-review](#10-self-review)
11. [Practices that generalise](#11-practices-that-generalise)

## 1. What we set out to make

A serious 2D fighting game at the level of a top independent release — not a demo with a fighting theme. Five designed fighters plus a sixth (坤), each with their own way of winning; the systems a fighting-game player expects (levels, strengths, cancels, throws, posture and guard breaks, clashes, three levels of super); a training mode; rollback netcode.

It is also a showcase (D-63), but the framework shows through what the game needs, never as a lecture: deterministic rollback is why replays and pinned pictures are free; the semantic tree is why an agent can play; data files with located diagnostics are why fighters can be modded.

**The reset.** The first version drew fighters from geometric parts posed by numbers. The maintainer rated it 1/10 (codex, against a strict rubric, 2/10): mannequin fighters, slideshow supers, effects that looked like presentation software, a glyph written as a super. We changed the method rather than polishing it: the art now comes from generated (or an artist's) layered pictures, and the framework does the cutting, rigging, animating, directing, effects, sound and music — all as data, all deterministic. The second direction from the maintainer made the combat **unarmed and hardcore**: hands, feet, body and joints for every normal; signature props only in special moments; supers that are interactive and different for everyone.

## 2. The layers

Every layer is usable without the one above it. Nothing was pushed down for the game's convenience, and nothing general was written inside the game.

| Layer | Package | What it is | Depends on |
|---|---|---|---|
| Fighting framework, core | `mgame/gongfu` | The match as a pure function: state, rules, frame data, Q16.16 fixed point | the standard library only |
| Fighting framework, data | `gongfu.data`, `gongfu.rules` | Fighter files read and checked with located diagnostics; the build rule that bakes hurtboxes and embeds fighters | gongfu, toml++ |
| Fighting framework, presentation | `gongfu.present2d` | A frame drawn: rigs and image parts, key poses, grips, reactions, effects, cut-ins, cinematics | gongfu, `openmui.render.vector`, hanim |
| Fighting framework, tools | `gongfu.training`, `gongfu.ai`, `gongfu.studio` | Readouts and dummies; seeded bots; cut, picture, sheet, bake, check, versus | the above, `openmui.image` |
| Independent components | `openmui.image`, `openmui.audio`, `openmui.raster`, `openmui.render.vector`, `hanim` | Images (PNG, WebP, cutting), sound and music, rasterising, vector rendering, animation | the standard library, each other, wrapped C libraries |
| UI framework | openmui | The stage as a component, its semantic node, its actions, the window | — |
| The game | `showcase/fighting` | The stage, the HUD, the roster, the fighters' files and art, the music | all of the above |

Examples of the rule at work: recolouring a range of drawn commands (刻's grey time stop) went into the renderer; cutting a sheet into pieces went into `openmui.image`; a piece's long axis (where a limb's joint lies) went into `openmui.image` too; music became an `openmui.audio` capability, not game code.

## 3. The fighters

Each fighter is three files in `fighters/` — what they do (`*.fighter.gongfu.toml`), how they look (`*.look.gongfu.toml`), and their hurtboxes baked from the look (`*.boxes.gongfu.toml`) — plus pictures in `fighters/art/`.

### 墨 (Mo)

| | |
|---|---|
| **Concept** | A calligrapher: calm, precise, keep-away. Paper-white robe with charcoal and vermilion, a high bun with a pin, a long ink brush |
| **Fighting style** | Being rebuilt unarmed (the hardcore design §3): palm and sleeve "strokes" — point, horizontal knife-hand, vertical chop, diagonal palms — driven from the waist; long kicks. The brush is slung on her back and comes out only for her specials (a wave of flung ink), supers and idle flourishes |
| **Art** | codex generated a concept, a side view, a parts sheet on green and six expressions; cut into a 987 × 440 parts atlas (94 KB WebP) and a 490 × 279 faces atlas (40 KB) |
| **Rig** | 21 bones; 18 image parts at one art scale (0.36 stage units per pixel); grid meshes bent at elbows and knees; sleeves and skirt panels on trailing bones; the back hand gripping the brush and both feet planted by two-bone IK; the head swaps between six drawn expressions |
| **Animation** | 48 clips keyed by 11 named key poses (`[pose.stance]`, `crouch`, `air`, `hit`, `guard`, `down`…), each key changing only what it names; four hit reactions (head, body, low, heavy) |
| **Effects** | Ink cut from a generated sheet: splash bursts, crescent slashes, dry-brush strokes laid along curves, droplets — played by her impacts, her projectiles and her clips |
| **Super (level 3)** | Unarmed until the last stroke, and every blow lands on her. Her palm strikes the chest; her front hand takes the wrist and turns them in (a hold: the defender is wherever her hand is), and her free hand strikes four times — palm to the chest, two fingers to the throat, the knife of the hand across the ribs, a chop down on the collarbone — each leaving its stroke of ink where it lands (撇, 点, 横, 竖, laid along the hand's path from her generated ink sheet), the held body reacting to each; both palms drive them off. Only then does she draw the brush from her back, sink low, and sweep one great horizontal stroke, 一, across the picture and through them: they are thrown up along it and fall. Seven hits, the same 4000 damage; no glyph is written over anyone |
| **Win pose** | She draws the brush over her shoulder, writes one slow circle in the air and holds it upright before her face, the free palm raised beside it; slings it back and stands still, the fist in the palm before her chest, a small bow and a smile |
| **Music** | The study: guzheng in D minor pentatonic, 128 BPM, 4-bar intro and 32-bar loop, layers by intensity |

### 霓 (Ni)

| | |
|---|---|
| **Concept** | A rushdown street fighter from a rainy neon city: magenta undercut fringe, cropped bomber with a lit zip, fingerless gloves with lit knuckles, cargo trousers, white-soled sneakers |
| **Fighting style** | Street boxing and kicks: jab, cross, hook, uppercut; low sweep, side kick, high kick; a dash that closes the screen |
| **Art** | Generated with the joints covered by her clothes (the lesson from 墨's parts); a 1017 × 438 parts atlas; eyes and strike cut-ins; a finishing illustration |
| **Rig** | 16 bones; her head drawn facing left, so its part is mirrored across its axis; her fringe and her jacket's flap trail her; an open-hand drawing swaps in for the front fist |
| **Animation** | 48 clips by 7 named poses (a boxer's guard as the stance) |
| **Super (level 3)** | One brawl in close-up, then in the air. Her straight lands and her front hand takes the collar (a hold: the defender is wherever her hand is). She yanks them down onto a knee to the gut, then a knee to the face, and hauls them up into a headbutt; each blow is a new hold, so the body in her hand reacts to it. She heaves them overhead, turning over, leaps after them and kicks them higher four times — a snap kick, a roundhouse, a high kick, a spinning back kick (drawn turned) — her neon trail behind her. Above them, her heel comes down like an axe and drives them into the street. Ten hits, the same 4000 damage. Her effects are generated art: an effects sheet painted as light on black, its brightness made its alpha, cut by `gongfu-studio cut` into slash arcs, bursts, streaks, a heel's drop and a street shockwave (`art/ni-fx.webp`); they sit where each blow lands, and in the clinch she is drawn over the body she strikes (`in_front`) |
| **Win pose** | Chin up, she smacks her lit knuckles into her open palm twice, flicks a thumb across her nose, and leans back on her heels, the back hand on her hip, the front fist hanging loose |
| **Music** | The street: drum and bass in F minor, 168 BPM, 8-bar intro and 40-bar loop |

### 坤 (Kun) — in progress

An original character built on an internet meme's signs — a clean centre part, a white shirt with rolled sleeves, grey suspender trousers, white-and-gold high-tops, a basketball — played completely straight: cold, quiet, precise. Bajiquan close-range bursts (铁山靠, the shoulder driven in with a step; 顶肘, the elbow) on a dancer's footwork; the ball appears only in his specials and supers. Research and the reasons for the contrast are in the hardcore design §5.

**Super (level 3).** The first version was a stance with one arm pumping seventeen times — the formula the maintainer had asked us to leave. It is now one exchange in which the bodies and the ball really meet, and every contact is a frame of the cinematic, not an effect laid over it:

| Frames | Beat |
|---|---|
| 0–17 | The elbow lands; she staggers back. The ball pops out from behind him and arcs onto one finger |
| 17–42 | He spins it, chin up, looking away (the eyes cut-in was the flash) |
| 42–54 | It drops into his palm; he winds up low behind his hip as the strike cut-in slides across the top |
| 54–62 | The bounce pass: off the floor and into her gut; she folds over it in a hitstop |
| 62–86 | It rebounds high out of the picture while he watches it go |
| 76–94 | He glides in on a dancer's step, arms trailing, afterimages behind him; spins (drawn turned from here, his back to her), lifts the near foot and stamps it down inside her stance |
| 94–104 | 铁山靠: his back driven into her, the far arm thrust away for the line of force; she takes it on her feet, arched, then leaves them |
| 104–144 | She spins up and over; he stands, his back to her, and holds a hand behind him without looking. The ball drops into it |
| 144–180 | She crashes down behind him; his finishing illustration; he is still standing with the ball |

Three hits carry the same 4000 damage as before (900, 1900, 1200). The engine pieces it needed are general and went into `gongfu.present2d` first: path keys with an ease (a body flung up and falling instead of stopping at every key), turns, hitstops for the struck, cinematic afterimages, and the taken reactions (`launched`, `slammed`) that every rebuilt look now has. His force is drawn art, generated as light on black and cut by `gongfu-studio cut` (`art/kun-fx.webp`): a burst and dust where the ball hits the floor, a spark and a push at the gut, the stamp's floor burst, 铁山靠's force wave running on past her, the crash's burst and dust; the camera pushes close on the gut and goes low for 铁山靠. What it still lacks: there is no drawing of his back, so "turned" reads as facing away rather than showing it.

**Win pose.** He straightens out of his stance, runs a thumb down his suspender and lets it go, then stands with his arms at his sides, chin up and eyes closed, turned from the fallen. No ball: it is the specials' and supers' alone.

### 璃 (Li)

| | |
|---|---|
| **Concept** | A glass artisan who fights: composed, precise, cold. A pale blue-grey jacket with a mandarin collar, frosted-glass toggles and an ice-crackle print (crackle-glaze porcelain) on its panels; a slate sash with a trailing end and a tassel of glass beads; wide sleeve cuffs over faceted glass bracers; slate trousers in pale wraps, soft grey boots; a silver bob with a prism hairpin |
| **Fighting style** | Unarmed and exact: the knife-hand thrust (贯手) and level cut, the rear palm driven through on a long step, a snap kick, a side kick, a high crescent kick against jumps; the overhead 「裂」 is a hop and a knife-hand brought straight down. Panes are her set-up, raised and broken by gestures of the hands, not by a weapon |
| **Art** | codex generated a concept (raised once to the cast's detail: the crackle print, the beads, the hem's glass facets), a side view, a parts sheet, a sheet of eight hands, six expressions, two cut-ins, a finishing picture and an effects sheet. Cut into a 1040 × 242 parts atlas (53 KB WebP), 428 × 208 faces (30 KB) and 396 × 86 hands (9 KB); the effects, painted as light on black, keyed by brightness and cut into 955 × 278 (108 KB) |
| **Rig** | 19 bones at one art scale (0.4 stage units per pixel): grid meshes bent at elbows and knees; the jacket's two panels, the sash's end and the bead tassel on trailing bones; both feet planted by two-bone IK; each hand swaps between eight drawings (open, fist, fist toward the viewer, knife-hand, palm, sword fingers, claw, palm up); the head between six expressions. Three materials: porcelain cloth, hard-faceted glass for the bracers and beads (a bright cold rim: her thin white outline), soft skin |
| **Animation** | 52 clips keyed by 13 named poses (`stance`, `crouch`, `air`, `thrust`, `palm`, `gather`, `set`, `side_kick`, `high_kick`, `guard`, `hit`, `down`, `flung`); every hit reaction, and `held`, `launched`, `slammed` |
| **Effects** | Her glass, cut from the effects sheet: bursts, crescent cuts, cracks and glints by weight; shards thrown and falling; the panes themselves are drawn art — whole, cracked once worn, breaking when struck — and her shards fly as slivers of glass |
| **Super (level 3)** | The eyes cut-in through cracking glass; the palm lands and the world turns to glass, a pane standing either side; she darts through her panes and strikes twelve times from in front and from behind (knife-hand, palm, low kick, cut, side kick), each blow breaking glass at the struck and leaving a crack in the world, the struck reacting to each; the strike cut-in as she gathers the light; the palm through the last pane, shards raining over the picture, the struck thrown up; her finishing picture |
| **Win pose** | She draws her feet together and stands tall, sets the prism pin in her hair with two fingers of the far hand, then raises the near hand's sword fingers before her face and is still |

Making her taught three things. The parts sheet came back twice: the first with a sleeve drawn onto the torso (a third arm whenever the front arm moved), the second with an open armhole; the second's armhole was closed by filling it with the jacket beside it, so its piping reads as a seam under the front arm. The generated boots were drawn larger than the legs, so they were scaled down in the sheet before cutting rather than given a scale of their own (the lesson of one art scale). And the first cinematic's prism doubled the training dummy into a row of ghosts and its fall was flat vector triangles; the prism was toned down to one faint image and the fall became her own shards raining from the top of the picture. The panes needed one general capability, which went into `gongfu.present2d`: a pane drawn from a picture's regions.

### 岳 (Yue)

| | |
|---|---|
| **Concept** | The guardian of a stone mountain gate above a sea of clouds: a giant grappler in his forties, heavy and unshakeable, stern and quietly warm. A short quilted wrestler's jacket (shuai jiao style) in russet with bronze-stitched trim and a mountain pattern in the quilting; a wide slate sash under a leather belt with a bronze mountain buckle; a string of wooden prayer beads; bare arms below short sleeves, forearms bound in sand-coloured wraps; wide charcoal trousers, wrapped shins, heavy black boots; a black topknot in a bronze ring, grey at the temples, a short full beard. Broad shoulders, short legs |
| **Fighting style** | Unarmed and close: palms driven through on stamping steps (a sumo's thrust), the elbow (顶肘) and the headbutt (「额」), push and stamping kicks; then the grabs — the half circle, the 360 and the 720, the anti-air catch — where his hands take collar and belt, haul the body overhead and bring it down |
| **Art** | codex generated a concept, a side view, a parts sheet (the torso drawn again, upright, when the first came back a hunched sack), eight expressions, eight hands, two cut-ins, a summit picture, a finishing picture, an effects sheet painted as light and a sheet of broken granite. Each piece was scaled to its place in the rig before cutting (the limbs drawn longer than the torso in their sheet; the hands drawn redder than the arms, shifted to his skin), then cut into a 1026 × 196 parts atlas (38 KB WebP), 784 × 111 faces (31 KB) and 499 × 65 hands (11 KB); the effects keyed by their brightness into 1205 × 275 (178 KB), the granite keyed from green into 598 × 173 (25 KB) |
| **Rig** | 18 bones at one art scale (0.5 stage units per pixel), a head taller than 坤: grid meshes bent at elbows and knees; the jacket's front and back skirts and the sash's ends on trailing bones; both feet planted by two-bone IK; each hand swaps between eight drawings (open, fist, fist toward the viewer, palm, claw, grab, knife-hand, palm up), the head between eight faces (calm, a grim smile, the glare, the roar, hurt, pain, serene, straining). Two materials: quilted cloth with a warm sunset rim, tanned skin |
| **Animation** | 53 clips keyed by 26 named poses (`stance`, `crouch`, `air`, `guard`, `hit`, `down`, `flung`, `grab`, `clinch`, `lift`, `slam`, `palm`, `shove`, `elbow`, `headbutt`, `stomp_up`, `stomp`, `plunge`, `folded`…); a giant's walk planting each step (the camera dips on it); every hit reaction, and `held`, `launched`, `slammed` |
| **Effects** | His weight, cut from the sheets: bursts with stone flecks by weight; dust rolling along the floor; shock waves; cracks that glow for a moment and are left dark in the stone (a light sprite drawn again in one dark colour); force arcs ahead of 镇岳's palms; rubble and pebbles thrown by seed. 震地 runs along the floor as a wave of dust and stone |
| **Super (level 3)** | 「泰山压顶」, out of the grab: his hands take collar and belt; he hauls them overhead, flat across his palms (a hold on his hand), and sinks into a squat, the stone cracking under him; he heaves them into the sky and leaps after them, dust blasting from where he stood; above the clouds he catches them again (his face in close-up, then his summit picture); he turns over and comes down on them like the mountain itself — knees first, both hands driving them, drawn over them, the fall streaking past; the crash: a flash, the picture quaking, dust, cracks, rubble; he stands, stamps once beside them and folds his arms (his finishing picture). The same three hits and 4300 damage |
| **Win pose** | A sumo's stomp: the knee raised high and driven down into a deep squat, the stone cracking; he rises, folds his arms over his chest and lifts his chin, eyes closed |

### 刻 (Ke)

| | |
|---|---|
| **Concept** | The keeper of a clock tower and a horologist who fights: patient, exact, a faint knowing smile, always a move ahead. A black cape lined in midnight blue with gold dial rings and gear teeth; a black double-breasted waistcoat with brass buttons and a gold pocket watch on its chain; an ivory shirt with black sleeve garters; white horologist's gloves; a round gold monocle on a chain; long black hair with one white streak, in a low ponytail with brass clasps that swings like a pendulum |
| **Fighting style** | Unarmed and economical: two fingers thrust like a clock's hand (the jab, the stab of her level 3), a level knife-hand, the rear palm on a long step, snap, side and high kicks; 「停摆」 the pendulum, the straight leg swung back and through; 「拨针」 the hands turned up to twelve. The clock's hand she throws and the watch are her specials' and supers' only |
| **Art** | codex generated a concept (twice corrected: it drew spectacles, then a second lens), a side view, a parts sheet of fourteen pieces (the ponytail recut below its clasp, where the sheet had drawn scalp above it), nine gloved hands (the ninth holding the open watch), six faces, two cut-ins (her eyes, the monocle holding a clock's face; the two-finger thrust), a finishing picture and an effects sheet of 22 sprites painted as light. Cut into a 958 × 250 parts atlas (52 KB WebP), 486 × 63 hands (9 KB) and 532 × 200 faces (32 KB), each sheet at the scale that puts its art at the one art scale; the effects keyed by their brightness into 1082 × 224 (111 KB) |
| **Rig** | 19 bones at one art scale (0.4 stage units per pixel): grid meshes bent at elbows and knees; the cape on two trailing bones, one mesh bent between them, swapping to a drawing flared open (its lining shown) in jumps, dashes and big moves; the ponytail on two; both feet planted by two-bone IK; each hand swaps between nine drawings, the head between six faces. Three materials: tailored cloth with a brass rim, softer skin and gloves, hair in three hard bands with a cold rim |
| **Animation** | 52 clips keyed by 18 named poses (`stance` — the clock's hand level at their eyes, the rear palm at the solar plexus — `point`, `cut`, `palm`, `snap`, `low_side`, `high`, `pendulum`, `twelve`, `mark`, `flick`, `watch`…); every hit reaction, and `held`, `launched`, `slammed`. The strike keys sit on the move's first active frame (a key's ease is its way to the next key, so a strike pose keyed one frame late is still on its way when the hit is live) |
| **Effects** | Time, cut from the sheet of light: clock-hand sweeps, gear rings and white-gold bursts by weight; a tick is a spark that stops where it is (bright at once, fading only at its end); the thrown hand turns end over end and comes back turning the other way; a small dial with a turning gear stands on the floor where her mark is (a general capability of `gongfu.present2d`: a look's `mark` effect) |
| **Super (level 3)** | 「零时」: the stab of two fingers into the chest; she lifts the open watch — the camera close on it — and clicks it. Time stops: the picture goes grey (all of it, drawn art too: `openmui.render`'s new `tone` effect), a great dial stands behind them with its hands still and two gears stopped. She walks to them and strikes ten times on a body that cannot move — the palm, the knife-hand at the neck, a snap kick at the knee; behind them in a step for the knife-hand across the back of the neck, the side kick behind the knee, the high kick; in front again for the clock's hand in the ribs, the pendulum heel, the palm, two fingers to the forehead — each blow's sweep and spark left hanging where it struck. Her back to them, two steps away, she opens the watch and snaps it shut: the bell. The colour comes back, the hands jump to twelve, every blow lands at once and they are thrown up and down while her finishing picture stands over it. The same ten ticks and 3400 at the bell |
| **Win pose** | Feet together, she reads the time in her open palm, head bowed; snaps the watch shut to her chest, chin up; and the far hand goes straight up, two fingers: the clock's hand at twelve |

Making her needed three general capabilities, each in its own commit and tested where it lives. Time stopping greyed only shapes: a display list's recolour leaves pictures alone, so drawn art stayed in colour in a grey world — the stop now draws the picture in a layer under a built-in `tone` effect (CPU and GPU, the same bytes). In the stop the struck stands as it was and the blows landing in it take effect at the bell, a contact's drawn effect hangs as its spark did, and a cinematic's effects can stand `behind` the fighters (the dial). And her clone and ghosts showed the triangles of every mesh as darker seams, because a translucent tint was drawn triangle by triangle: a tinted body is now one silhouette, drawn opaque in a layer at the tint's opacity — every fighter's afterimages are cleaner for it.

### Kata

Kata, the training dummy, stays deliberately plain, but acts: it has the four hit reactions, so a super's cuts land visibly on it. Its win pose: it comes to attention and bows from the hip.

## 4. Combat systems

The match is a pure step `step(state, pads) → state` over one plain struct in Q16.16 fixed point: no clocks, no allocation, no virtual calls, so rolling back 8 frames is copying and stepping again, and a whole recorded match hashes the same on GCC, LLVM, ARM and wasm. The rules arrived as numbered requirements (G-1 … G-22, P-1 … P-8), each with a test. Frame data is measured, not trusted: the tests play every move and compare startup, active, recovery and advantage with the file.

A hit during the defender's startup is a counter: 120 % damage and 2 more frames of stun. A hit during a whiffed move's recovery is a punish counter: 130 % and 4 more frames. The tests check this by punishing a whiffed heavy.

The hardcore design lists every detail with its rule, its look and its acceptance: high, mid and low; light, medium and heavy; attacks and reactions; combos; the three levels of super; knockdowns; defence; neutral; whiff punishes (limbs' hurtboxes are baked from the animation, so an extended limb can be hit); jumps; cross-ups (inputs are recorded relative to facing, so blocking a cross-up means holding the other way); throws. The bots (`gongfu.ai`) play every pair for hundreds of rounds; the tests hold each fighter to 38–62 % wins.

## 5. How a fighter is made: the workflow

Each step is a tool with a command line (JSON out, an exit code) so an agent can run it; the steps are the same for an artist's layers.

| Step | What happens | Tool |
|---|---|---|
| 1. Concept | A concept, a side view and a parts sheet (plain green, parts apart, joints covered by clothing), expressions, cut-ins, a finishing picture, an effects sheet (black, for light) | codex image generation, prompts kept with the art; judged against the rubric |
| 2. Cut | Key out the green; find each piece (pixels joined across a small gap); mask it to its own pixels; scale; pack an atlas; report each piece's region and its long axis | `gongfu-studio cut sheet.png --out dir --scale 0.5 --webp 92` |
| 3. Pictures | Cut-ins and illustrations keyed, trimmed, scaled and encoded | `gongfu-studio picture in.png --out x.webp --key 0,255,0 --trim --width 640` |
| 4. Rig | In the look file: `[[picture]]`s, `[[bone]]`s (joints and lengths from the art at one `art_scale`), `[[part]]`s (a picture's region, its anchor on the joint and its tip along the bone — the long axis from step 2 gives both), meshes and bends, `[[grip]]`s for hands and feet | the look format (`gongfu.present2d`) |
| 5. Key poses | `[pose.<name>]` tables (bones' angles to the world, rise, grips, drawings), clips keyed by them with per-key changes | the look format |
| 6. Look at it | Any clips at any frames at any size, as a contact sheet | `gongfu-studio sheet fighters mo --clips 5MP,5HP --frames 1,8 --cell 300` |
| 7. Bake | Hurtboxes from the poses; the build refuses a stale bake | `gongfu-studio bake fighters` |
| 8. Balance | Bots play every pair | `gongfu-studio versus fighters --rounds 200` |
| 9. In the game | The tests draw pinned frames and the README loop headless | `mcpp test` in `showcase/fighting` |
| 10. Grade | codex grades the frames against the rubric (one independent source); the maintainer's grade is final | — |

Throwaway scripts were used once — to turn world-angle key poses into tracks before `aim` keys and named poses existed. Both are now features of the look format, and the looks are plain data again with nothing beside them.

## 6. Tools

| Tool | What it does | Where |
|---|---|---|
| `gongfu-studio cut` | Sheet → masked pieces, packed atlas (PNG or WebP), JSON manifest with regions, atlas regions, long axes and an alpha histogram | `mgame/gongfu/studio` |
| `gongfu-studio picture` | Key out, trim, scale, encode a picture (PNG or WebP by its name) | same |
| `gongfu-studio sheet` | Contact sheets of clips at chosen frames, with boxes, the look's drawn art included | same |
| `gongfu-studio bake` / `check` | Hurtboxes from the animation; checks that bakes are current | same |
| `gongfu-studio versus` | Every pair played by bots: win rates and where damage comes from | same |
| `gongfu-studio stage` | A stage file's painted layers as a frame, with two fighters for scale, framed as the game frames them | same |
| `openmui.build.embed` `embed(.index = true)` | Every embedded file of a tag in one generated table by name (`<tag>_files`), so a program finds its art without a list kept by hand | `mcpp/rules`, `tools/gen` |
| `omui-music check / render / stats / fmt` | A piece checked, rendered to WAV at an intensity, measured (loudness, bands, per instrument) | `modules/media/audio` |
| `tools/ci/readme-media.sh` | Copies what the tests drew into each showcase's `media/`; in CI, checks they match | `tools/ci` |
| `openmui.image` | PNG, WebP (still and animated), key out, pieces, pack, crop, scale, principal axis | `modules/base/image` |

The tools' openmui interfaces (a rig editor, a pose editor with IK handles, a timeline with onion skins, an impact tuner with slow motion) are the next step — the command lines came first so agents and CI could use them.

## 7. The frame: presentation as a pure function

`gongfu.present2d` draws a frame from `(state, frame)` alone; rollback never saves presentation state, and a replay renders the same pixels.

- **Rigs and image parts.** A part is a picture's region skinned over a grid mesh to its bone and to the bones it bends at; `DisplayList::draw_mesh` draws it on the CPU reference renderer and on the GPU the same (max difference 0).
- **Key poses and IK.** Clips are keyed by named poses in world angles (`aim`); grips solve two-bone IK for a hand on the brush or a foot on the ground, and clips let go and step.
- **Reactions.** The latest blow's height and weight choose `hitstun.head`, `.body`, `.low` or `.heavy`; a cinematic names a reaction for each of its hits; a grab anchor holds the defender at the attacker's hand, playing `held`.
- **Effects.** `[fx.<name>]`: sprites that grow, turn and fade; brush strokes laid along a curve and drawn out; sprays of droplets from a seed — named by impacts, projectiles, clips and cinematics.
- **Cut-ins.** `[cutin.<move>]` over a super's flash; panels in a cinematic; a cinematic can hide its attacker while an illustration stands in.
- **Materials.** A picture may have a `relief`: each region's silhouette rounded in, with folds where it is painted darker. `openmui.image`'s `normal_map` makes it once at load, in integers, so it is the same everywhere. A look's `[material.<name>]` sets how its image parts take the stage's light: the bands, how soft their edges are, and the shadow's and the rim's strengths. The light's direction and colours are the stage's, so both fighters are lit alike. Each fighter has a style:
  - 墨: two soft washes toward a cool ink.
  - 霓: hard bands with a hot pink rim.
  - 坤: clean cel shading with a warm rim.
  - Faces and hands use a softer, warmer `skin`.

  The renderer's lit mesh fill turns each normal with the part, so a mirrored or rotated arm still faces the light. The CPU reference and the GPU agree to 0 levels.
- **Stages** are painted in layers. Each home stage was generated as five layers meant to stack, judged together:
  - the far view;
  - the back wall, keyed;
  - the floor;
  - the props at the stage's ends;
  - the foreground.

  `gongfu-studio picture` keys and encodes them. A stage file places each picture region in a layer by its parallax (`[[layer.image]]`: `at`, `width`, an optional `height` to stretch a floor, `flip`, `flicker` for a lantern). The procedural layers stay over them, such as the street's rain.

  Three rules came from previewing with `gongfu-studio stage` at the middle, a corner and fully zoomed out:
  - keep the band where the fighters stand calm;
  - size props to the fighters (a desk at about half their height);
  - keep foreground pieces out of the fighters' zone (never over their legs at a corner).

  A narrow back wall is extended by mirrored copies at its sides, so it covers the widest zoom.

  The second set (璃's greenhouse, 岳's mountain gate, 刻's clock tower) was generated with the first two sheets and a frame of the game as references, so it matches them. Previewing caught two more problems:
  - the gate's wall at the first set's scale cut off the paifang's roof, so it is drawn smaller;
  - the clock tower's catwalk ran behind the fighters' heads, its girders busying their band, so its wall is cropped at the deck and raised above them.

  A front layer generated with a balustrade across its whole bottom was redrawn sparse; the rule "nothing in the middle" has to be checked, not only asked for.
- **The HUD** (black lacquer, gold leaf, a red seal) is generated art cut into frames, lettering, the names' calligraphy and fills. Its parts:
  - portraits (each look's head);
  - health with the brush texture and the recent damage draining;
  - the posture, the meter and the level;
  - the timer;
  - combos, COUNTER and PUNISH COUNTER, K.O.

  It is all a pure function of the match and its kept events. Calligraphy and coloured digits sit on lacquer plates or ink edges, so they read on paper and on night scenes alike.
- **The menus** (the title and the select screen) are a component of their own beside the stage, with the same keys and pads. The application keeps what was chosen in signals: the stage's `player1`, `player2`, `mode` and `arena` follow them, and the two screens' `visible` and `active` are computed from whether a fight is on. The select screen's `chosen` event starts the fight, and the stage's `round_over` brings the select screen back.
  - Its art is generated lacquer and gold like the HUD's: the logo, the mode bars, tiles, banners and their lit versions, 对战 and 训练 in calligraphy. The regions are named in code.
  - The previews are the fighters' own looks drawn by `gongfu.present2d`'s painter at 1.75× in the banners (their materials under the default light), so a look change shows here at once.
  - The roster's pictures are registered once per image store (`Registered`) and shared by the stage and the menus.
  - It is a pure function of its frame and its choices, so `tests/select_test.cpp` pins its pixels like the fight's.
- **Rounds**: the match is best of three, kept by the stage, not the simulation. gongfu's match is one round, so the stage counts the frames to the next round itself. It keeps the wins, starts a new match with the same fighters, and lets the theme play on. Draws count for both, and a match ends after five rounds at most. The semantic value carries the round, the wins and whether the match is decided, so an agent can follow a whole match.
  - The K.O. plays out. gongfu used to stop at a round's end, so the K.O. was a still picture. Now an ended round goes on as its finish: nobody's pad is read and nothing lands or counts down, but the fallen fall and a winner's move runs out. The stage plays the first 90 frames of it at half speed. A winner who stands again takes the look's `win` clip from the round's end, once the looks have one.
  - Not yet: no look has a `win` clip, and the winner can still be in the follow-through as the loser lands under their feet.
- **Afterimages and clones** are the part's shape in one colour (`draw_mesh_mask`), not a faded copy, so 霓's trail reads as neon.
- **Lit by the stage**: a stage's light can have a colour (`light.color`), and the fighters' drawn parts take it on their lit side. The renderer gained it as `Material::key`. White is exactly the picture as painted, so no earlier frame changed, and the CPU and GPU shade a coloured light the same, 0 levels apart. The two sunsets warm the fighters, the study's moonlight cools them, and the street adds a touch of magenta. The first review had called them pasted over the scenery.
- **Grounding and framing**: each fighter casts a soft contact shadow on the floor. It is drawn in rings, darkest under the feet, smaller and fainter the higher the fighter is, in the stage's colour (a stage file's `contact`). The camera closes in further on fighters near each other, up to 1.35. A cinematic keeps its own camera.
- **Hit feel**, the five parts (hardcore design §2.6), each checked by a test:
  - **Hitstop:** light < medium < heavy for every fighter's normals. A counter adds 2 frames and a punish counter 4, for both fighters.
  - **The struck shakes:** every frame of the hitstop moves the struck fighter's picture, and the shaking stops when the hitstop ends.
  - **Effects by tier:** a counter's effect is the hit's own, larger and drawn in the counter colour. A punish's is stamped twice and flashes white, unless the look gives `fx_counter` / `fx_punish` effects of its own.
  - **Camera:** it shakes only for medium and heavier hits (a light hit's shake is 0), and harder for a counter or a punish.

## 8. Sound and music

- **Sounds**: synthesised sounds as data (`sounds/fight.sound.omui.toml`), named by each impact (attack, body, tail), mixed sample-accurately; an offline render of a match is the same bits on GCC and LLVM.
- **The announcer**: Kenney's CC0 Voiceover Pack: Fighter, converted to 22.05 kHz mono WAV (`voice/`, about 450 KB for eight calls) and embedded by the generated `voice_files` table. Playing it needed a capability the synthesiser lacked, so `openmui.audio` gained recordings: `read_wav`, `Mixer::add_recording`, and a sound layer that plays one at a speed under the layer's envelope, filter, drive and pan. The calls (`sounds/voice.sound.omui.toml`) are the takes played a touch slower (0.95), driven a little, and doubled 14 ms behind by a muffled copy, through the stage's hall.
  - The calls: "fight" on the round's frame 20; "time" when the clock runs out; after the K.O.'s moment, "player 1" or "player 2" then "winner", or "flawless victory" when the winner was never touched; "it's a tie". The select screen says "choose your character". Training is quiet.
  - The first render put "fight" at the limiter: every property the application sets restarts the round, and each restart had queued the call again on the same sample. The call now comes from the match's own clock (its frame 20), so a round makes it once.
- **The fighters' voices**: CC0 takes (`voice/README.md` names the packs), chosen with a tool made for it. `omui-music takes` splits a recording on its silences and measures each take: length, peak and RMS, pitch (the median of its voiced windows by normalised autocorrelation), how voiced it is, and how bright (zero crossings a second). Grunts were chosen from those numbers, since an agent cannot listen. Low, dark, short takes became 坤's restrained efforts and cries; short bright ones became cries for the women; long voiced ones became falls.
  - `sounds/fighters.sound.omui.toml` processes them into each fighter, and the names say what each take is (`<fighter>_hurt_<n>`, `<fighter>_ko`, `<fighter>_effort_<n>`). 墨's takes are a touch slower and softened. 霓's are quicker and higher, driven, the low end thinned. 坤's are lower and darker.
  - What is said follows the match's events. A heavy blow (800 damage and up) or a counter makes the struck one cry out 3 frames after the crack, with a breath (36 frames) before the next cry, so a combo is not a chorus. The striker strains with a heavy blow. A K.O. is the fall alone, without a cry as well. The take comes from the frame, so the same fight says the same; `Sounds::spoken()` counts them for tests and agents.
  - Not yet: 墨 and 霓 share one actress, told apart only by processing; 璃, 岳 and 刻 are silent; efforts come at contact rather than at a move's start.
- **Music**: `music/*.music.omui.toml` pieces — patches, patterns as readable note text, sections, an arrangement, and layers by intensity — played by `openmui.audio`'s sequencer. The stage starts its home's piece with the round; the music rises as the lower health falls, and plays fully under a super. The round over, the piece goes to its outro over the finish. Six homes, six pieces:
  - the study: guzheng in D minor, 128 BPM;
  - the street: drum and bass in F minor, 168 BPM;
  - the rooftop: boom-bap in C minor, 92 BPM;
  - the greenhouse: a glockenspiel theme and a harp's answer over glassy pads and a celesta arpeggio, in F-sharp minor, 132 BPM;
  - the gate: horns over a low string ostinato, taiko and a great drum, in D minor, 96 BPM;
  - the clock: a woodblock tick and tock under pizzicato turning like gears, a reedy lead and chimes, in G minor, 120 BPM.

  Each is balanced with `omui-music stats`, at each intensity, against the others: about −20 LUFS calm and −16 under a super, no instrument buried. The gate's cello ostinato first measured −36 LUFS and was doubled.
- **The menus' music**: the main theme (`music/menu.music.omui.toml`, E minor at 96) gathers the three homes: 墨's dizi and guzheng, 坤's sub bass and boom-bap kit, 霓's wide saw at the top. The select screen drives its intensity as a match drives a stage's: calm on the title, the beat on the select screen, all of it once a player is ready. It fades under the VS and is finished behind the fight, so the menus and the fight never play over each other.
  - The cues (`sounds/menu.sound.omui.toml`: a move, a choice, a step back, a player ready, the VS) are pitched in the theme's key and panned to the player's side.
  - The first renders put the VS 17 dB over the music, since two players ready on one frame sum into one loud gong. The levels were set by rendering the menus offline and reading their loudness every quarter second, the way `omui-music stats` reads a piece. The VS now stands about 10 dB over the music for a moment.
  - `tests/select_test.cpp` renders the menus' sound frame by frame and pins its bits.

## 9. Performance

| What | Measured | How |
|---|---|---|
| A frame of the match | well under a microsecond | `tests/bench`, budgets in `budgets.bench.omui.json` |
| Rollback, 8 frames | 196 ns a frame, nothing allocated | the rollback bench |
| Sound, 48 voices | about 0.045 ms per 128-frame block (budget 0.5 ms) | `audio/mix-48` |
| Determinism | the same match hash on six platforms; the same pixels on GCC and LLVM | pinned in the tests |

Skinning runs on the CPU per frame for two fighters of a few thousand triangles; the plan (math and fighter technology design §5.3) is a mesh render feature with prebuilt meshes and GPU skinning, benchmarked before it is claimed.

## 10. Self-review

**What worked**

- Determinism first made almost everything else simpler: rollback, replays, pinned pixels and sound, a bot-driven balance report, an agent playing through the same actions.
- Data files with located diagnostics, and the build refusing stale bakes, caught real mistakes early.
- Generated art plus framework tools reached in a day what weeks of tuning geometric parts did not.

**What was hard, and what it changes**

| Finding | What it says to do | Status |
|---|---|---|
| Code alone could not reach the art bar | Art comes from pictures; the framework cuts, rigs and animates them | done (墨, 霓) |
| Generated parts had skin-coloured stumps at every joint | Ask for joints covered by clothing; crop and order parts to hide the rest | done in the prompts |
| Placing parts was guesswork | The cut tool reports each piece's long axis | done |
| Key poses were numbers produced by a script | Named key poses and `aim` in the look format | done |
| A long textured triangle lost half of itself in a clipped cell | The rasteriser cut edges at the clip's sides instead of clamping their ends (`openmui.raster`) | fixed |
| The brush became the weapon, and too big | Unarmed normals; props in special moments; believable proportions | in progress (hardcore design) |
| Black ink disappears on dark stages | "Colour by alpha" and additive image paints in both renderers | planned |
| Fighters need a rebuild to change | Hot reload of fighter files in a development mode | planned |
| A changed pin is hard to review | Pixel diffs against the last accepted frame in `openmui.testing` | planned |
| Every cinematic grew its own special fields | Generic primitives (panels, effects, holds, reactions); the old fields to be rewritten and removed | in progress |

## 11. Practices that generalise

Collected in [best practices](../../docs/en/best-practices.md):

- Keep simulations as plain data and pure steps (fixed point, or doubles with exactly rounded operations only). Rollback, replay, tests and netcode follow from that.
- Draw presentation as a pure function of (state, frame). Never save rendering state for rollback.
- Make components contract-first, so a person, an agent and a test act through the same actions.
- Keep assets as data with located diagnostics, and have the build check what was derived from them (bakes, embeds).
- Give every production step a command line with JSON before its graphical tool: agents and CI can use it the day it exists.
- Bring art in as pictures and let the framework do the rest; ask the generator for what the rig needs (separate parts, covered joints, one scale).
- Write performance-critical code (simulation, mixing, rendering) in C++; tunable content in data files that can be reloaded; scripts (TS, Lua) only where logic itself needs tuning live — never in a frame.

## Playing it

- **Keyboard**: player 1 WASD, U I O (punches) J K L (kicks); player 2 the arrows, Insert Home PageUp, Delete End PageDown.
- **Gamepad**: the first two connected are players 1 and 2, beside the keyboard. The d-pad and the left stick are directions (opposites held together go to gongfu's SOCD rule, as keys do); the buttons are the six-button layout — west LP, north MP, R1 HP, south LK, east MK, R2 HK — written as data in `controls/pad.controls.omui.json`, so it can be remapped. The host reads any pad through SDL's mapping database (the browser's standard mapping on the Web) and applies the dead zones; `tests/gamepad_test.cpp` injects pads headless and checks that a press arrives as the move.
- **Web and Bilibili Toy**: `tools/ci/toy.sh openmui.showcase.fighting` builds it for wasm32-emscripten, packs it with `dist-toy` and checks it with Toy's `toy_doctor.py`; serve `target/toy/omui-fight/` and open it. `#/training` opens training mode (a hash route is the program's flag). Publishing is the maintainer's (`toy create`).
