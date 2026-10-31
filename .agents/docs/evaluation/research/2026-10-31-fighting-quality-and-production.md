# Research: what quality and detail mean in the best fighting games, and how this game could reach them

Written by codex (with web search) at the maintainer's request, to discuss and decide on before more is built. The maintainer's note: "to the joint, even the finger" means the craft — exact animation, binding and effects — not finger attacks, and not necessarily rigging down to the fingers.

**openmui’s main presentation problem is insufficiently authored movement and contact.** The current pipeline produces detailed paintings, then asks a small set of flat parts and skeletal poses to perform actions that require changes in perspective, anatomy, silhouette, and expression. More detailed textures, more bones, or larger effects will not resolve that mismatch.

I recommend a narrowly scoped hybrid 2D pilot: retain skeletal animation where it works, but author replacement drawings—including whole-body drawings—for important attack poses, turns, compression, and reactions. Use offline 3D blocking where it helps establish volume and contact. Expand production only after one fighter survives direct comparison with the references.

The maintainer’s correction is fundamental: precision “down to the finger” means deliberate craft. It means a fist closes convincingly, a wrist supports a palm strike, a grabbing hand actually wraps around something, and the fingers preserve their shape between drawings. It does not prescribe finger attacks or a particular rigging technology.

**Evidence and limits.** This report draws on the repository instructions, fighting design documents, README and development story, presentation source, all seven fighter/look file pairs—including Kata—and existing artwork and test captures. No assets were generated. The checkout contains ongoing changes; existing captures are evidence of their recorded states, not proof that every current edit has been rendered.

External production claims below use developer interviews, presentations, and publisher material where available. Visual interpretations are distinguished from implementation facts. I did not perform instrumented captures of the commercial games or play and listen to the current build, so this report cannot establish measured input latency, audible quality, or exact commercial-game camera curves.

**What the leading references actually demonstrate.**

**Street Fighter 6** uses real-time 3D characters and environments, with stylized anatomy, material rendering, facial performance, and cinematic cameras. It is not a sprite pipeline. Capcom explicitly identifies muscle tension, cinematic Super Arts, and environmental responses as benefits of its rendering technology. Its character artists also presented their modeling work at the [2023 ZBrush Summit](https://pixologic.com/zbrushlive/street-fighter-6-capcom-2023-zbrush-summit/). [Capcom’s announcement](https://news.capcomusa.com/2022/06/02/street-fighter-6-redefines-the-genre-in-2023/) supports these production features, but not a universal animation-frame count.

The useful benchmark is coordinated bodily intention. A punch involves the supporting foot, pelvis, rib cage, shoulder, elbow, wrist, and fist; recovery restores the guard rather than simply reversing the extension. Hands remain readable from gameplay distance and survive cinematic enlargement. The opponent’s compression and recoil complete the action.

Its effects reinforce different events through shape, color, timing, and sound: ordinary contact, guarded contact, Drive mechanics, and decisive punishment have different identities. A bright spark alone is not the distinction. The HUD supports resource decisions while cinematics temporarily reorganize attention.

A useful qualitative breakdown of a cinematic uppercut is: establish the successful contact; tighten framing around the bodily consequence; follow the rising finishing action; restore gameplay geography. Exact shot counts and durations vary by move and Critical Art variant and were not verified here. Critics specifically emphasize bodily weight and force: [Tom’s Guide](https://www.tomsguide.com/reviews/street-fighter-6) describes attacks as more visceral and impactful, while [GamesRadar’s hands-on account](https://www.gamesradar.com/street-fighter-6-hands-on-ps5-preview/) singles out heavy-feeling hits.

**Guilty Gear -Strive-** uses real-time 3D models, custom toon rendering, and deliberately limited animation. This does not mean “animate everything at twelve frames per second.” It means artists choose which poses hold, which transitions snap, and where extra temporal detail matters.

The team describes control over shading on individual hands, legs, and weapons, including physically inaccurate lighting chosen for the desired image. Facial animation is authored for the moment. This is a production system built around artistic correction, not merely a cel-shading filter. [Arc System Works’ developer interview](https://www.unrealengine.com/developer-interviews/how-guilty-gear--strive--hits-an-ultra-combo-with-groundbreaking-visuals-and-gameplay?lang=en-US)

Arc System Works advertises approximately **2,000 animation frames per character and 30,000 total**. Its counting methodology is not explained, so these figures must not be equated with unique hand-drawn sprite cells. [Official company description](https://www.arcsystemworks.jp/portal/company/business/)

The craft benchmark is graphic authority: strong attack silhouettes, controlled negative space, expressive hands and faces, abrupt spacing changes, and effects whose contours share the character animation’s visual language. Large counter-hit announcements and wall-break staging make major events unmistakable. Their scale also illustrates a tradeoff: spectacle can compete with visibility. Copying their size without their hierarchy would make openmui harder to read.

The frequently cited [Xrd GDC presentation](https://www.gdcvault.com/play/1022031/GuiltyGearXrd-s-Art-Style-The) documents an important predecessor workflow. Its exact implementation details should not automatically be attributed to Strive.

**Skullgirls 2nd Encore** demonstrates traditional, digitally drawn frame-by-frame character animation. Its central lesson is not maximum frame count. Lead animator Mariel Cartwright’s presentation shows an early Filia animation reduced from **21 drawings to 15**, and a Cerebella example from **45 to 29**, with stronger poses and less redundant motion. She emphasizes silhouettes, anticipation, smears, overshoot, follow-through, and uneven holds. [Animation Bootcamp presentation](https://media.gdcvault.com/GDC2014/Presentations/Cartwright_Muriel_Animation_Bootcamp_Fluid.pdf)

The original eight-character release contained **11,515 animation frames, averaging 1,439 per character**. That is a historical production count, not a verified total for every later 2nd Encore fighter. [Guinness record](https://www.guinnessworldrecords.com/world-records/105505-most-frames-of-animation-per-character-in-a-videogame)

Hands, foreshortened limbs, distorted bodies, and transitional shapes can be redrawn to serve each action. They need not remain geometrically faithful to a reusable part. Effects and reactions contribute to the same animated performance. Supers often work through a freeze, a strong graphic declaration, and staged action in the gameplay plane; elaborate rotating cameras are not necessary.

Its perceived fluidity comes from motion design and overlap as much as drawing quantity. A detailed drawing held at the wrong moment is less effective than a simpler drawing with decisive spacing.

The additional references broaden the available production choices:

| Reference | Production evidence and useful craft benchmark |
|---|---|
| **Granblue Fantasy Versus Rising** | Real-time toon-shaded 3D with illustration-oriented staging. The original GBVS team documents angle-dependent facial/body presentation and deliberately substantial hitstop, balanced against tempo. That interview establishes the predecessor workflow, not every Rising implementation detail. Hands, weapon grips, facial close-ups, elemental effects, and Skybound Art staging are useful targets for consistency across gameplay and cinematics. [Developer interview](https://www.unrealengine.com/developer-interviews/inventive-combat-in-granblue-fantasy-versus-combines-accessibility-with-new-layers-of-depth?lang=en-US) |
| **BlazBlue Centralfiction** | Sprite-based character presentation with graphic effects and illustrated cut-ins. Arc System Works describes the earlier BlazBlue workflow as using 3D outlines followed by hand redrawing, including artistic shadow correction. This establishes a hybrid lineage; I could not verify that every Centralfiction asset followed an identical process. Its useful benchmark is readable, highly differentiated attack silhouettes and hand gestures despite extensive supernatural effects. [Developer interview](https://www.gamedeveloper.com/business/dodging-striking-winning-the-arc-system-works-interview) |
| **King of Fighters XV** | Real-time 3D animation with stylized materials. SNK describes customized lighting and cinematic-camera depth of field for special moves. It demonstrates that recognizable fighting rhythm, hand/weapon posing, and strong Climax staging do not require Strive’s limited-animation appearance. Its ordinary attacks remain an important check against spending the entire budget on supers. [SNK interview](https://www.unrealengine.com/developer-interviews/why-snk-shifted-to-unreal-engine-for-its-latest-fighting-games?lang=en) |
| **Garou: Mark of the Wolves** | Authored pixel-art sprites. Published production sketches show move ideas communicated through drawings and refined into animation. Study whole-body gestures, restrained neutral acting, compact hand shapes, recoil, and recovery. Its fixed-plane supers and economical HUD show how much force can be communicated without modern camera freedom. Oda describes the intended rhythm as offensive and engaging without becoming tiring. [Production drawings](https://www.gameanim.com/2019/12/08/garou-mark-of-the-wolves-animation-designs/), [developer interview](https://shmuplations.com/garou/) |
| **Them’s Fightin’ Herds** | Hand-drawn cartoon sprite presentation, rather than a valid example of simply rotating television-style puppets. I could not verify the complete internal balance of redrawing and reusable elements. Quadruped silhouettes, ears, hooves, facial acting, and exaggerated reactions demonstrate economical shape design. There are no human fingers to reproduce. A contemporary hands-on account specifically connects its sparks and recovery animation with responsive, satisfying hits. [Hands-on analysis](https://www.thesixthaxis.com/2018/03/08/horsing-around-with-thems-fightin-herds-four-legged-fighting/) |
| **Pocket Bravery** | Pixel-art presentation with deliberately compact proportions and cinematic enlargements. Its developer says a KOF XIII-like direction was abandoned because the small team estimated roughly three times the work; the adopted direction had already taken three years by the interview. Large, economical facial and hand shapes support readability. The lesson is to design detail density around production capacity. Exact frame totals and internal use of 3D references were not verified. [Developer interview](https://warpzone.me/entrevista-pocket-bravery-jogo-de-luta-brasileiro/) |
| **Hollow Knight** | Traditional drawn animation made in Photoshop and assembled in Unity. Compact silhouettes, immediate attack marks, clear recoil, and restrained UI demonstrate effective detail selection. Its simplified protagonist avoids the anatomical acting burden of six realistically proportioned martial artists. It is useful for economy and responsiveness, not as an equivalent production-cost benchmark. [Team Cherry FAQ](https://www.teamcherry.com.au/faq), [production account](https://unity.com/made-with-unity/hollow-knight) |

For these additional references, I could not verify authoritative, comparable per-move drawing counts or complete per-character totals. Nor did I verify comprehensive reaction inventories, finger-rig specifications, super durations, or numerical camera-shake tables. Those gaps should remain visible rather than be filled with plausible-looking numbers.

**Hitstop numbers require particular care.** Simulation frames, unique drawings, animation samples, hitstop, hitstun, superfreeze, and cinematic duration describe different things.

Publisher evidence confirms move-specific exceptions. Strive’s official 2.00 notes change Dragon Install Charged Stun Edge hitstop from **15 to 11 frames**. Skullgirls’ published lobby patch notes change Robo-Fortune explosion hitstop from **8 to 12 frames**. Neither example establishes a universal light/medium/heavy table. [Strive notes](https://www.guiltygear.com/ggst/en/news/post-2851/), [Skullgirls notes](https://skullgirls.com/2017/01/sg2e-lobby-patch-notes/)

I could not verify publisher-backed strength-by-strength hitstop and shake tables across the requested games. The defensible conclusion is that timing is authored per event and constrained by gameplay. More stop is not automatically better: it changes confirmation time, cancel rhythm, and perceived responsiveness.

**What a strict reviewer notices first.** The following distinctions are practical review criteria, not claims that every reference implements every item identically.

| Detail | Strong execution | Common mediocre execution |
|---|---|---|
| Key poses | Attack intention reads from the body and silhouette | The same torso with a differently rotated arm |
| Weight transfer | Support foot, pelvis, torso, and striking limb cooperate | Floating torso, sliding feet, disconnected arm motion |
| Hands | Correct grip, wrist alignment, perspective, and continuity | Generic open hand reused for punches, grabs, and recoil |
| Spacing | Deliberate acceleration, holds, overshoot, and recovery | Uniform easing between equally important poses |
| Smears | Brief, directional shapes connecting extreme positions | Several transparent copies standing in for motion design |
| Impact | Visible contact and immediate bodily consequence | A spark bridges a gap between unrelated performances |
| Reactions | Height, force, direction, stance, and trajectory matter | One backward lean for almost everything |
| Effects | Designed core, directional expansion, breakup, and decay | One attractive texture scaled and faded |
| Camera | Reveals the decisive action and preserves geography | Zoom exposes flat artwork or crops the victim |
| Supers | Setup, escalation, climax, aftermath, readable contact | Portrait panels and repeated hits substitute for acting |
| UI | Resources and calls remain legible without stealing action | Decorative framing occupies cinematic safe areas |

Anatomical correctness alone is insufficient. Deliberate distortion can make an action convincing. Accidental distortion makes its construction visible.

**Where the current game stands.** The artwork has substantially more visual information than the movement can support. Faces, garments, accessories, painted stages, and lacquer-and-gold UI establish identities. Their presence does not establish finished animation quality.

The inspected production method is demonstrably generated painted artwork cut into reusable parts, attached to a skeleton, with grid-mesh bending, IK, named poses, replacement drawings, procedural secondary motion, and image-based effects. It is already a hybrid. The question is whether enough of each action is authored beyond its reusable rig.

The six designed fighters currently have **15–19 bones, 15–18 parts, and 52–53 clips each**. Named-pose counts range from 13 to 26. Mo, Kun, Li, Yue, and Ke have multiple hand drawings; Ni has only one listed replacement on her front forearm. These are current file counts, which differ from some development-story descriptions.

**Poses and anatomy remain below the proposed bar.** In [Ni’s knee capture](/tmp/openmui-tests/fighting-ni-level3/02-29-knee.png), overlapping limbs and torso shapes make the bodily mechanism difficult to read. The image communicates “something struck the abdomen” more readily than a convincing collar-controlled knee. In [Kun’s shoulder strike](/tmp/openmui-tests/fighting-kun-level3/06-95-kao.png), the effect communicates force more clearly than the attacker’s torso and shoulder mechanics.

The source explains the limitation: most pose changes are angular transformations of existing artwork. Part depth remains static within the body drawing. Mirroring changes facing; it cannot reveal an undrawn back or reconstruct a rotating rib cage.

**Hand detail is uneven and often contextual rather than anatomical.** The replacement sheets contain useful fists, palms, grabs, and gestures. Yet an isolated correct hand does not guarantee a correct wrist, forearm perspective, or grip. Ni’s combined forearm/hand artwork particularly restricts articulation. Mo’s two-finger throat beat and Ke’s finger-led attacks are creative choices that should be reconsidered independently of the original request for precision.

**Timing has real structure, but the reactions are too coarse.** Current normal-hit values include:

| Fighter | LP / MP / HP hitstop | Light / medium / heavy camera shake |
|---|---:|---|
| Mo | 7 / 9 / 14 frames | 0; 2 pixels over 6 frames; 8 over 14 |
| Ni | 5 / 7 / 9 | 0; 2 over 6; 6 over 10 |
| Yue | 8 / 11 / 15 | 0; 2 over 6; 12 over 16 |

These are authored parameters, not measured screen displacement or commercial benchmarks.

Two implementation choices deserve immediate attention. First, [reaction selection](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:665) chooses the generic heavy reaction before examining height when shake or damage crosses a threshold. A heavy abdominal hit can therefore lose its abdominal identity.

Second, [reaction playback](/home/speak/workspace/mui/mgame/gongfu/present2d/src/present2d.cpp:1434) derives time from clip length minus remaining stun. A short stun can begin partway through the reaction, skipping its initial compression. Contact onset and recovery completion need separate timing control.

Ordinary victim shaking is horizontal alternation. Camera shaking uses generic oscillations. Neither is a fully authored response to the strike’s direction.

**Effects have material identity but insufficient animation variety.** `fx.cpp` supports textured sprites, curved strokes, and seeded sprays; fallback sparks remain procedural. This is more capable than “everything is procedural,” but many textures change through translation, scale, rotation, and opacity rather than successive designed shapes. Counters can recolor/enlarge the same effect; punish effects can stamp it again.

In [Mo’s palm capture](/tmp/openmui-tests/fighting-mo-level3/02-29-palm.png), the ink texture obscures much of the important interaction. Precision means designing both the effect and the visibility it leaves.

**Supers have choreography data, but inconsistent staging.** Cinematic locks last 170–210 frames, approximately 2.83–3.5 seconds, with a separate 50-frame level-three activation freeze. Those are reasonable quantities to author; duration is not the principal defect.

[Yue’s lift](/tmp/openmui-tests/fighting-yue-level3/03-lock-66.png) places the defender behind the HUD. [Ke’s cut-in](/tmp/openmui-tests/fighting-ke-level3/06-lock-140.png) competes with the bodies. Some captures use Kata deliberately; they cannot establish that grabs and strikes work across the six finished body types.

The UI is more coherent than the body animation, but its select-screen CPU plate covers Ni’s preview head. Cinematic safe areas need explicit ownership. Static captures cannot establish whether the music, sound, and pacing make these sequences satisfying in play.

**Production options for six fighters.** The estimates below are planning ranges, not measured AI throughput. A day means approximately eight focused human production hours, including review and correction. Totals cover fighter art and presentation, excluding the rest of the game.

| Approach | Work and risk | Plausible result; provisional effort |
|---|---|---|
| **A. Generated frame-by-frame sprites, cleaned per pose** | Approve rough motion first; generate/edit drawings against fixed references; correct anatomy, registration, costume, and temporal drift. Independent prompting is particularly risky. | Excellent isolated frames are plausible; sustained Skullgirls-level animation is unproven with this team. Pilot 20–40 days; six fighters 300–900 days. |
| **B. Expanded skeletal 2D with replacements** | Author torso angles, foreshortened limbs, hands, faces, depth changes, and deformation corrections. Replacements must sometimes cover most of the body. | Strong indie presentation is plausible in controlled views. Full rotations and close interaction remain expensive. Pilot 15–30 days; roster 180–400 days. |
| **C. Full real-time stylized 3D** | Six consistent models, rigs, corrective shapes, facial acting, animation, toon materials, camera work, and an expanded rendering/content pipeline. | Highest flexibility, but neither generated models nor a shader supply Arc System Works’ artistic corrections. Pilot 45–100 days; roster 350–800 days. |
| **D. Hybrid authored 2D, optionally assisted by offline 3D** | Use rigs for suitable motion; replace decisive poses with drawings. Use 3D blocking or rendered guides for difficult volumes and interactions, then finish consistently. | Best near-term fit. A narrow slice may approach reference-level craft; a uniformly finished roster remains substantial work. Pilot 20–40 days; roster 220–500 days. |

No option reliably produces six SF6-quality fighters merely by allowing agents to run longer. The missing capacity is animation judgment and correction. Coding agents can automate production, but someone must recognize and resolve weak movement.

Offline 3D rendered to sprites is materially different from Strive’s real-time rendering. It fits `present2d` more easily and makes approved images immutable, but arbitrary camera rotation becomes impossible. Cinematic angles must be authored and exported. Paired interactions also require defender-specific validation; six attackers against six defenders create 36 pairings, including mirrors.

For scale, **800 untrimmed 512×512 RGBA frames consume 800 MiB per fighter**, before atlas overhead; six consume approximately 4.69 GiB. Cropping and compression reduce this, but WebP download size does not equal decoded memory. Normal maps and additional views increase residency. The existing 42 fighter-art WebPs total about 3.86 MB because the pipeline reuses relatively few pictures. That economy is real, but partly explains its limitations.

**The recommended workflow is D, beginning close to B.** Lock a simpler, consistent animation drawing style and native gameplay scale. Preserve the detailed paintings as references and illustration assets. Approve silhouettes and contact poses before requesting polished replacements. Use whole-body cels whenever forcing the rig would produce a worse image.

Maintain a canonical model sheet, proportions, costume map, palette, hand vocabulary, and approved views. Every generated edit should reference these and neighboring accepted poses. Record provenance and rejected iterations; a shared prompt alone cannot guarantee consistency.

Add only the necessary presentation primitives: cel/exposure tracks, replacement assemblies, keyed depth, and attachment/contact metadata. Sampling should remain a pure function of move identity, simulation frame, facing, and event history. Runtime generation and nondeterministic retiming are unnecessary.

Sprite and rig paths must share explicit gameplay attachment and hurtbox metadata. The current skeleton-oriented bake cannot simply infer meaningful limbs from a whole-body cel. Keep collision authoring separate from decorative silhouettes, with intentional changes reviewed and rebaked.

The immediate tool should provide side-by-side playback, onion skins, silhouette mode, contact overlays, frame stepping, and JSON export. The maintainer reviews motion first, then cleaned art, then effects and camera. Measure accepted assets per review hour during the pilot before forecasting the roster.

**Pilot: Ni, one heavy normal, one rising special, one level-three super.** Ni offers recognizable boxing and kicking mechanics, and her current limitations cannot easily hide behind elaborate clothing.

Use `5HP`: 28 simulation frames, active on frames 8–10, with nine-frame hitstop. Author approximately 8–12 distinct drawings or replacement composites. Show preparation, pelvis/shoulder rotation, a supported wrist and fist, extension, recoil, and return to guard. Preserve gameplay timing initially.

Use `623MK`: a 41-frame, two-contact rising attack, currently active at 5–6 and 8–9. Author approximately 12–18 drawings, including takeoff, compressed knee/torso relationships, airborne continuation, landing, and recovery. The second contact must look like a continuation with a new consequence.

Retain the current super’s 180-frame lock and mechanical hit schedule for the first comparison. Give it an explicit shot plan:

| Lock frames | Framing and purpose |
|---|---|
| 0–16 | Two-shot establishing collar contact and both bodies |
| 16–64 | Medium two-shot for knees and headbutt; show compression |
| 64–96 | Wider launch and pursuit; preserve direction |
| 96–124 | Airborne two-shot for four kicks; distinct poses |
| 124–150 | Heel drop and ground impact; preserve the force line |
| 150–180 | Landing, aftermath, and readable gameplay return |

Budget roughly 40–70 super drawings/composites, plus support poses and opponent reactions. These are pilot estimates, not reference-game counts.

Test light, medium, and heavy fixtures using Ni’s existing 5/7/9-frame stops, then normal hit, counter, punish counter, block, clash, and throw. Preserve current timing initially so art changes can be evaluated independently. Design tier differences through reaction, sound, core shape, and decay—not only size.

Acceptance should require:

- **Unassisted readability:** with effects disabled, the normal, special, and super contacts remain understandable at full speed.
- **Contact precision:** proposed tolerance within four screen pixels at the agreed 1080p framing; grabs remain attached throughout.
- **Grounding:** planted feet drift no more than two pixels during declared holds.
- **Hand continuity:** no broken wrists, disappearing fingers, arbitrary grip changes, or unexplained costume changes.
- **Reaction coverage:** head, body, and low contacts retain their identities under heavy force; short stun never skips impact onset.
- **Staging:** no essential face, hand, contact, or landing is hidden by HUD or panels.
- **Pairing coverage:** validate Mo and Yue as different-sized defenders, plus mirrored facing and stage corners.
- **Presentation integrity:** effects remain readable on light and dark stages; sound onset and playback require actual listening review.
- **Determinism:** replay, seek, and rollback reproduce cel selection, attachments, effects, and CPU reference pixels.
- **Reference review:** compare equivalent actions with SF6 at matched on-screen body size, full speed first and frame-by-frame second.

A strict reviewer should see an intentional sequence of physical decisions comparable in clarity to SF6, even with a simpler visual style. If they still see a painting assembled into moving pieces, the pilot has failed. Additional effects should not compensate for that failure.

**Decisions for the maintainer.**

1. Is the target SF6-level craft in a distinct, simpler style, or also comparable anatomical detail and cinematic freedom?
2. Can the painted character style be simplified to make consistent animation affordable?
3. Can the six-fighter rollout wait until one slice passes?
4. Who owns visual direction and anatomical correction, and is specialist animation help possible?
5. Are established offline authoring tools acceptable through reproducible imports?
6. How much download size and decoded texture memory can the game spend?
7. Which supers justify additional views and defender-specific interaction work?
8. Should finger-led attacks introduced under the earlier interpretation be redesigned?
9. What pilot time budget and failure threshold will trigger a production-method change?
10. Which experienced players will judge full-speed hit feel, alongside the maintainer’s final visual approval?