// mrender.text — text as the renderer draws it: mlibs.text's glyphs
// rasterised (:glyphs, the renderer's GlyphSource over a FontCollection) and
// its laid-out paragraphs painted into display lists (:paint). Text itself
// knows no renderer (convergence plan §5.3); this is the renderer's side.
//
//     mrender::text::GlyphCache glyphs(fonts);
//     mrender::text::paint(paragraph, list, origin);
//     cpu.render(list, surface, {nullptr, &glyphs});
export module mrender.text;

export import :glyphs;
export import :paint;
