// xengine.cellular.present.matter — what matter looks like on the lattice (D-168): a look (xengine.matter's
// appearance, prepared against a program) lowered to the lattice's shade, for a presenter of a field that
// program's lowering steps. The package's feature `matter`: the presenter alone knows a key and numbers.
//
//     auto program = matter::standard();
//     auto look = matter::standard_look(*program);                       // or look_document(program, text, file)
//     auto view = FieldPresenter::make(store, shape, channels, lower(*look));
//     view->present(cells);  view->draw(list, dest);  view->draw_glow(list, dest, 6);
export module xengine.cellular.present.matter;

import std;
export import xengine.cellular.present;
export import xengine.matter.appearance;

export namespace xengine::cellular {

// A look as a shade: the key is the material (channel 0, where a lowered program keeps it), a row a material,
// a state a tint — its range's end by row. What is no material is the shade's fallback.
[[nodiscard]] Shade lower(const matter::Look& look);

}  // namespace xengine::cellular
