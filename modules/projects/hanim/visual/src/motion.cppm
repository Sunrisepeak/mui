// openmui.motion — universal motion (D-93; universal motion §4, §6): matter
// taken from what a node drew, and morphs between two drawings of it.
//
//     motion::Matter a = motion::Matter::extract(before, begin, end, resolver);
//     motion::Matter b = motion::Matter::extract(after, begin2, end2, resolver);
//     motion::Morph   m(std::move(a), std::move(b), {.curve = {.spring = hanim::springs::standard}}, now);
//     m.draw(t, list);   // any t: before its start the first drawing, settled the second
export module hanim.visual;

export import :matter;
export import :morph;
