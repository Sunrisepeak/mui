# mlibs.image.ktx2

KTX2 textures read: what a file is, its pictures as pixels, and the same pictures as a GPU's own blocks — the same bytes on every machine (the foundation plan's M6.3: KTX2 is the format textures travel in; glTF's `KHR_texture_basisu`).

```cpp
import mlibs.image.ktx2;
namespace image = mlibs::image;

if (!image::is_ktx2(bytes)) return other(bytes);
const auto what = image::describe_ktx2(bytes);            // 768 × 512, 1 level, etc1s, no alpha, sRGB
const auto pixels = image::decode_ktx2(bytes);            // an Image, as decode_png gives one
if (!pixels) return fail(pixels.error());                 // "level 3: the file has 1", "… cut short or damaged"
const auto blocks = image::ktx2_blocks(bytes, image::Ktx2Blocks::bc7);   // 192 × 128 blocks of 16 bytes
```

| | |
|---|---|
| **What is read** | KTX2 files packed by Basis Universal's codecs: ETC1S, UASTC, ASTC's own blocks (4 × 4 to 12 × 12) and XUASTC, with or without Zstandard. `describe_ktx2`: size, levels, layers, faces, the codec, whether the file says it has alpha and whether its numbers are colours (sRGB) or plain numbers |
| **Pixels** | `decode_ktx2(bytes, level)`: an `Image` (straight alpha; whole where the file has none), `decode_ktx2_levels`: every level the file carries, the largest first. Of an array or a cube: the first layer's first face |
| **A GPU's blocks** | `ktx2_blocks(bytes, as, level)`: the picture as BC7, ETC2 or ASTC 4 × 4 — sixteen bytes a block of four by four, a row of blocks after a row from the top — what a device takes as it is |
| **Refusals** | Nothing of a file is trusted. What is not a KTX2 file (its twelve bytes of identifier), a header cut short, a codec that is not Basis Universal's, a file of no size or of more than 2^28 pixels, a level the file has not, data that is cut short or damaged, and HDR files are each refused with the reason |
| **The same bytes everywhere** | A picture's pixels and its BC7, ETC2 and ASTC blocks are pinned in the tests; the wrap's own CI holds the same hashes on Linux (GCC, LLVM), macOS arm64 and Windows |

## How it was designed (D-185)

1. *From this system's own ground.* A texture here has two readers held to one another: the CPU reference reads pixels, a device reads blocks. So the package gives both of the same picture from the same file — pixels for the reference and for every device, blocks for the device that has them — and both are pinned, so that a device's picture can later be held to the reference's from one source. A reader that is one function a question (`is`, `describe`, `decode`, `blocks`), each refusing with a reason an agent can act on, as `mlibs.image`'s other readers do.
2. *Research and what is at hand.* The transcoder is Basis Universal's, the reference implementation of the codecs KTX2 names (and the one Khronos' own libktx uses for them), through its module wrap made upstream for this ([mcpplibs/basis-universal-m](https://github.com/mcpplibs/basis-universal-m), `mcpplibs.mwrap.basis_universal` 2.50.1; D-56, D-83): its design — one small file transcoded at load to whatever the device has — is the point of the format, and writing the codecs again would be a worse copy.
3. *The level of the best.* Against libktx and the engines' importers (Filament, Godot, three.js's KTX2Loader): par in what is decoded (the same transcoder). Behind in what is not read here: HDR codecs, arrays' and cubes' other layers and faces, KTX2 files that hold plain or already-GPU-packed data without Basis Universal's codecs (libktx reads those), and no device takes the blocks yet — `mlibs.rhi` has no compressed formats, so every texture is pixels on the device too. Ahead in one thing: the same bytes pinned on every platform, for pixels and blocks.

## Limits

- No device takes the blocks yet (`mlibs.rhi` has no compressed texture formats): `xrender.asset` reads a KTX2 picture as pixels. The saving in device memory that KTX2 is for is still to come; it restarts when `mlibs.rhi` gains BC7, ETC2 and ASTC formats with a capability each.
- HDR (UASTC HDR, ASTC HDR) is refused: an `Image` holds bytes. It restarts with a picture type of light.
- Speed is not measured here yet (the transcoder's own; a benchmark belongs in `tests/bench` beside the other readers').
- A package of its own beside `mlibs.image` (D-99: dependency isolation): the transcoder is one translation unit of 1.7 MB of source and several times the code of the PNG, WebP and JPEG readers together; what only shows images does not carry it.

## Tests

`tests/ktx2_test.cpp`, on upstream's own test files (`tests/data`, from Basis Universal's `test_files/`, which upstream states it does not own): what each file is; an ETC1S photograph as pixels (pinned, twice the same, every pixel whole) and as BC7, ETC2 and ASTC blocks (pinned); one XUASTC picture packed by Zstandard and by the arithmetic coder is one picture; noise, a file cut at four places, a level that is not there and a file that does not exist are refused with their reasons.
