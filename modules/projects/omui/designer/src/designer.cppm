// omui.designer — Live Design's designer (D-149, D-151): what an
// application in development mode is designed with. A program that links it
// has the design.* verbs (Live Design's catalog), the design's history beside
// the application (.omui/design/), previews and variants in stand-ins and the
// timeline; a release links none (D-149: the designer is left out at build
// time). Its history is a store of its own (DesignHistory): `omui design sync`
// reads it.
export module omui.designer;

export import :history;
