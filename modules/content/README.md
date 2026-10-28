# modules/content

Text and what is made of it: shaping and paragraphs, layout, Markdown, internationalisation.

Depends on: [base](../base), [gfx](../gfx) (glyphs go into display lists). Used by: [ui](../ui).

| Package | |
|---|---|
| [text](text) | Font fallback, HarfBuzz shaping, bidi, line breaking, paragraphs, carets, editing, Chinese typography |
| [layout](layout) | Flexbox, grid, stack and absolute layout: incremental, cached, deterministic |
| [markdown](markdown) | CommonMark + GFM to blocks and inline marks over md4c; HTML output; streaming append |
| [i18n](i18n) | Internationalisation without ICU: locales, plurals, numbers, dates, lists, MessageFormat 2; CLDR packs inside it |

Start with [text](text/README.md).
