# openmui.unicode

Text segmentation without ICU (module `openmui.unicode`, D-47): UAX #29 extended grapheme clusters (incl. GB9c Indic conjuncts and GB11 emoji sequences) and word boundaries, and the UAX #14 line breaking algorithm rule by rule (LB1–LB31 of Unicode 17, incl. LB15a–d, LB19a, LB20a, LB21a, LB28a Aksara). Properties come from two-stage tables generated from the Unicode Character Database by `omui-gen unicode`; invalid UTF-8 decodes to U+FFFD per maximal subpart.

Conformance: the UCD's GraphemeBreakTest (766/766), WordBreakTest (1944/1944) and LineBreakTest (19338/19338 in full; every 4th line is committed in `tests/data`) all pass.

Regenerate for a new Unicode version:

```bash
# download LineBreak.txt, EastAsianWidth.txt, auxiliary/*BreakProperty.txt and *Test.txt,
# emoji/emoji-data.txt, DerivedCoreProperties.txt, extracted/DerivedGeneralCategory.txt
omui-gen unicode <ucd-dir> --version 17.0.0 --out modules/unicode/src/generated --tests modules/unicode/tests/data
```
