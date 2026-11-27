# Subject: promo-video — openmui's film and omui-promo

**What:** `showcase/promo-video`: a 60-second film and a 15-second cut, every shot a live openmui scene rendered by `omui-promo` from its timeline (`promo.timeline.omui.toml`, `short.timeline.omui.toml`, `promo.subtitles.omui.toml`, script/), with incremental re-rendering (frame digests, GOP chunks). Read its README, dev-story.md, the promo-video design document, src/, tests/.

**Top 3:** Apple's product films (e.g. the Vision Pro introduction) · Linear's release films · Stripe Sessions openers. **Reference:** Vercel Ship, Figma Config openers.

**Dimensions:**
- `story_script` — does a viewer understand what openmui is and why it matters, in 60 s and in 15 s.
- `shot_design` — composition, focal points, scale for small screens.
- `motion_design` — easing, choreography, continuity.
- `typography_titles` — titles, subtitles, readability at 480 px wide.
- `identity_colour` — visual identity, palette, consistency with the site.
- `pacing_edit` — rhythm, cuts, beats, the ending.
- `sound_music` — music, sound design, mix, sync.
- `concept_payoff` — "frames are functions of time / the film rewinds itself" carried visually.
- `tool_quality` — omui-promo: render, check, incremental cache, measured times.
- `code_tests_docs` — code, tests, README, dev story.

**Images:** a frame every few seconds of the film, the README frames.
