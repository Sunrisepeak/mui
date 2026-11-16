// omui.generate — runtime generation as an application ships (D-144,
// convergence plan W5): interface an Agent makes while the application runs,
// put into the surfaces the application declares, through stages each asked
// for and each readable (draft → validated → prepared → previewed →
// committed); A2UI as one of its input formats. Its operations' catalog
// (generate.*) is omui.agent's, beside the protocol's and Live Design's.
export module omui.generate;

export import :generation;
export import :a2ui;
