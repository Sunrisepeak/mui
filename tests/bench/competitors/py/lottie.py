# Lottie's side of src/lottie.cpp: the animation omui-bench-lottie writes (DIR/lottie-200.json) played by ThorVG and by
# rlottie, the two native Lottie players (pinned in requirements.txt), each on one thread into a 512 × 512 buffer, the
# frames visited in the same scrambled order as hanim's (frame 37·k mod 120), the clock round the seek and the drawing.
# Each player is driven through its C library by ctypes; what a call through Python costs is measured on its own (the
# same calls on an animation of one layer) and printed beside, not subtracted. Frame 60 is written, on white, as
# straight RGBA (DIR/lottie-200.<player>.rgba) for `omui-bench-lottie --compare DIR`.
#
#   python lottie.py DIR [--runs N]      one JSON line a player
import ctypes
import json
import math
import pathlib
import sys
import time

from rlottie_python import LottieAnimation
from thorvg_python import Engine, LottieAnimation as ThorvgLottie, SwCanvas
from thorvg_python.base import Colorspace

SIDE, FRAMES = 512, 120
ORDER = [visit * 37 % FRAMES for visit in range(FRAMES)]


def on_white(premultiplied: bytes, order: str) -> bytes:
    """Premultiplied pixels (channels in `order`) over white, as straight RGBA."""
    out = bytearray(len(premultiplied))
    index = {name: order.index(name) for name in "RGBA"}
    for at in range(0, len(premultiplied), 4):
        alpha = premultiplied[at + index["A"]]
        for channel, name in enumerate("RGB"):
            out[at + channel] = min(255, premultiplied[at + index[name]] + 255 - alpha)
        out[at + 3] = 255
    return bytes(out)


def thorvg(text: bytes):
    engine = Engine(threads=0)
    canvas = SwCanvas(engine)
    canvas.set_target(SIDE, SIDE, cs=Colorspace.ABGR8888)
    buffer = canvas.buffer_arr
    animation = ThorvgLottie(engine)
    picture = animation.get_picture()
    picture.load_data(text, "lottie", None, True)
    picture.set_size(SIDE, SIDE)
    canvas.add(picture)

    def draw(frame: int):
        animation.set_frame(float(frame))
        canvas.update()
        canvas.draw(True)
        canvas.sync()

    def pixels() -> bytes:
        return on_white(bytes(buffer), "RGBA")

    version = engine.version()[4]
    return f"thorvg {version}", draw, pixels, (engine, canvas, animation)


def rlottie(text: bytes):
    animation = LottieAnimation.from_data(text.decode())
    library, handle = animation.rlottie_lib, animation.animation_p
    buffer = ctypes.create_string_buffer(SIDE * SIDE * 4)
    library.lottie_animation_render.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t]
    library.lottie_animation_render.restype = None

    def draw(frame: int):
        library.lottie_animation_render(handle, frame, buffer, SIDE, SIDE, SIDE * 4)

    def pixels() -> bytes:
        return on_white(buffer.raw, "BGRA")

    import importlib.metadata

    return f"rlottie {importlib.metadata.version('rlottie-python')}", draw, pixels, animation


def measure(draw, runs: int):
    times = []
    for run in range(1 + runs):
        for frame in ORDER:
            begin = time.perf_counter_ns()
            draw(frame)
            spent = (time.perf_counter_ns() - begin) / 1e6
            if run > 0:
                times.append(spent)
    times.sort()
    at = lambda percent: times[min(len(times) - 1, math.ceil(percent / 100 * len(times)) - 1)]
    return at(50), at(95), sum(times) / len(times), len(times)


def one_layer(text: bytes) -> bytes:
    scene = json.loads(text)
    scene["layers"] = scene["layers"][:1]
    return json.dumps(scene).encode()


def main():
    directory = pathlib.Path(sys.argv[1])
    runs = int(sys.argv[sys.argv.index("--runs") + 1]) if "--runs" in sys.argv else 3
    text = (directory / "lottie-200.json").read_bytes()
    for name, player in (("thorvg", thorvg), ("rlottie", rlottie)):
        label, draw, pixels, keep = player(text)
        median, p95, mean, count = measure(draw, runs)
        draw(60)
        (directory / f"lottie-200.{name}.rgba").write_bytes(pixels())
        _, draw_one, _, keep_one = player(one_layer(text))
        one_median = measure(draw_one, 1)[0]
        print(json.dumps({"player": name, "engine": label, "scene": "lottie-200", "frames": count, "median_ms": round(median, 3), "p95_ms": round(p95, 3),
                          "mean_ms": round(mean, 3), "one_layer_median_ms": round(one_median, 3)}))
        del keep, keep_one


if __name__ == "__main__":
    main()
