# Subject: site — openmui's official website

**What:** `site/` (openmui.site: built with openmui itself, wasm in the browser; the overview atlas of places, documents, live works, lenses, the page's time, the agent panel, the frame meter). Read site/README.md, site/src/*.cpp, site/content/*.md, docs/en/features.md. The images are the site's places at 1440×900.

**Top 3:** linear.app · vercel.com · rive.app. **Reference:** bun.sh, tldraw.com, stripe.com.

**Dimensions:**
- `first_impression_identity` — brand mark, typography, colour, a signature visual, the first viewport.
- `artistic_expression` — a coherent visual language, composition, materials, motion as expression.
- `innovation` — does the page itself prove the framework's idea (one semantic tree, many renderings) in a way no other site could.
- `storytelling_ia` — hero, narrative, information architecture, navigation, calls to action.
- `reading_typography` — documents: measure, size, leading, hierarchy, code, tables, Chinese.
- `live_works` — the works in their places (wall, workshop, example, move list) as seen and used.
- `interactions` — lenses, the page's time, the agent panel, overview atlas: clarity and delight.
- `motion` — transitions, flights, timing, interruption, reduced motion.
- `performance` — load size, startup, frame rate (the frame meter, GPU path), measured where possible.
- `detail_polish` — alignment, spacing, contrast, states, empty space, consistency.
- `accessibility_seo` — semantic DOM mirror, static pre-render, keyboard, contrast.

**Run:**
- `omui-site --size 1440x900` (openmui.site)
- `omui-site --size 390x844 --lang zh --agent-name org.openmui.site.phone` (openmui.site)

**Use:**
1. The opening: read the first viewport as a newcomer; tick each check with a click, then Ship; undo; let the agent ship it; follow what each rendering panel shows.
2. Every navigation entry, clicked: the place it flies to, the flight (step it with ui_clock), the place at rest, the way back.
3. Links and buttons inside documents and live places: click them where they are drawn; each must answer.
4. The overview atlas: drag to pan, wheel to zoom, double-click a place; find each place from the overview.
5. The lenses: switch each and read what it shows of the same place; the page's time: pause, scrub, rewind.
6. The live works in their places: the wall (pan, open a document), the workshop (change a value, undo), the example, the move list (play a move).
7. The frame meter while flying and at rest; the renderer and how places are drawn while moving (textures first, then live: find the switches).
8. Keyboard only: Tab through, Enter, Escape, arrows; focus always visible.
9. The phone-sized copy (`org.openmui.site.phone`, Chinese): the same tasks that fit a phone; text never cut or overlapping.

**Images:** the places at 1440×900 (home, overview, features, practices, timeline, wall, workshop, example 01, move list, lenses).
