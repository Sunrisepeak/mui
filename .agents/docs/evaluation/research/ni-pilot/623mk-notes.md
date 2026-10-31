# Ni 623MK whole-body keys

Eleven opaque 768×512 review masters, `mk-01.png` … `mk-11.png`, in the requested board order. Built-in image generation supplied all drawing and creative corrections. The exact historical style prompt and every pose/correction prompt are in `generation-prompts.json`. No script draws or repairs anatomy.

`mk-review.png` shows all eleven side by side at 0.5 art scale, with the two knee points and outward normals marked. The review tiles crop unused green equally; the PNG masters retain their full canvas.

## Registration and reproducibility

Run `/usr/bin/python3 prepare-assets.py`, then `/usr/bin/python3 make-review.py` from this directory. ImageMagick applies the original import recipe: 15% green replacement, Lanczos uniform scale **360/863**, 2% green replacement, translation onto exact #00b140, opaque PNG24. Compressed poses retain their reduced height. Sources are not independently fitted to their foreground bounds.

Root is **(284,456)** in every master, scale **0.5** into gameplay. A reviewed pelvis axis supplies horizontal registration; the lowest sole registers vertically to 456. These sources are root-relative drawings with no simulation height baked in. The standing reference calibrates the uniform source scale; it is not a demand to stretch an airborne or crouched silhouette to 360 px. `registration.json` records the reviewed source axes, selected versions and socket annotations; `asset-audit.json` records actual translations, inclusive bounds and SHA-256 hashes.

The old heavy's fixed front-shoe corner is unsuitable for these airborne keys. This delivery follows the requested pelvis/root rule. Landing sole points are measured separately from the shoe's rightmost edge. On mk-10 the front sole is at y455 and the rear sole reaches y456: a 0.5 gameplay-pixel contour difference, within the stated floor allowance.

Pixel-to-local: `x=(u-284)/2`, `y=(456-v)/2`. Image normals use **downward-positive y**; convert their y sign for world-up, then transform with facing. Keep pivots and sockets relative to the original root when trimming. The supplied look's heavy cels demonstrate `scale=0.5`, preserved pivots and the clip's `cels` key; no fighter or look file was modified.

## Measured contacts

| Drawing | Surface | Art pixel | Outward normal, image-down | Local point |
|---|---|---|---|---|
| mk-03 | left knee upper/forward ridge | (374,187) | (0.707107,-0.707107) | (45,134.5) |
| mk-05 | same knee, higher ridge | (370,158) | (0.707107,-0.707107) | (43,149) |
| mk-09 | front sole | (345,456) | (0,1) | (30.5,0) |
| mk-10 | front sole | (375,455) | (0,1) | (45.5,0.5) |

`sockets.json` contains these annotations and every inclusive foreground bbox. Points were picked on the visible final ink contour; normals describe its local outward direction, not an inferred hitbox-center vector. `contact-surfaces.json` supplies adjacent ridge samples and intended victim regions. Both knee points fall inside their unchanged mechanic boxes. They differ from the board's nominal targets by **(+6,-1)** and **(-6,+2) art pixels** respectively; the measured surface must remain authoritative when pairing a victim. These are not claims of a resolved ≤1 gameplay-pixel paired contact.

`exposures.json` records one-based board intervals, hitstops, path and the omitted SK intervals. In particular f5/f8 first contacts are clip keys 4/7; landing is f22, not f29. These are held whole-body keys, with no pixel crossfade. The drawing strip does not include the engine's vertical path.

## Review decisions

All selected sources and final masters were reopened and inspected. Selected drawings have two connected arms and legs, closed fists with outside thumbs, visible neck/collar connection, the anatomical left knee pad, folded shoes behind the knee on contacts, and no arm attack. No opponent, effect or floor shadow is baked in. Rejected images are removed from this delivery; only their decisions/prompts are retained.

| Rejected attempt | Reason | Replacement |
|---|---|---|
| mk-02 v1 | Supporting shoe stayed flat, missing the last toe push. | v2, lifted heel and toe release |
| mk-03 v1 | Knee too low and too far forward relative to pelvis. | v3 |
| mk-03 v2 | Knee tucked behind guarding hand; ridge not freely exposed. | v3, clear higher ridge and tighter guard |
| mk-04 v1 | Recoil dropped the knee too far; head/body scale continuity weaker beside contact. | v3 |
| mk-04 v2 | Still a deep chamber rather than a small one-frame recoil. | v3, small knee retreat |
| mk-05 v1 | Second knee not high enough and too far forward. | v4 |
| mk-05 v2 | Height correction insufficient; guard crowded ridge. | v4 |
| mk-05 v3 | Still too close to the first-contact height. | v4, visibly higher same knee |
| mk-06 v1 | Apex continuity weaker against the revised contact; regenerated from contact identity/scale. | v2 |
| mk-08 v1 | Oversized upper-back jacket flap changed the cropped bomber silhouette. | v2, compact back contour |

## Acceptance gate still open

This is a static key-art delivery, not a D-90 pass. It has no engine binding, SK bridges, paired defender compression, range corrections or authored opposite-side art. The ≤1 native-pixel paired contact, ≤0.5 planted-foot world drift, actual f22 landing under the path, transitions through the continuing horizontal recovery travel, timing/cancel/hitstop interruption, safe framing and both-facing fixture matrix still need in-engine proof. Bone-length stability within the roughly 3% style-lock tolerance has not been quantitatively certified across foreshortened keys; static visual inspection cannot establish that motion requirement.

No 60fps whiff/hit/block/counter/punish captures, audio/input session, replay/seek/rollback pixel checks, evaluator ≥90 scores or maintainer sign-off were produced. The pilot's reaction matrix and cinematic gates are outside these eleven drawings and remain pending. The measured sockets' deviations from the nominal board points are explicit above, rather than hidden by moving the root or changing mechanics.

All authored project files are under `/tmp/ni-pilot/623mk`. The built-in image service additionally maintains its own automatic staging/cache files outside that directory; their destination is not exposed by its API.
