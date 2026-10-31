// The page's half of openmui.rhi.webgl (src/js/omui_webgl.js) under node,
// with a fake WebGL2 context that remembers what it is asked (fake_gl.mjs):
// the library loaded as Emscripten would link it — the program's memory an
// ArrayBuffer, a "pointer" to a string the string itself — and fed what the
// C++ side sends: resources by slot, a submission as rhi's wire words. What
// GL is asked for is checked, deterministically; the real thing runs in
// tools/ci/browser.sh.
//
//   node --test modules/gfx/rhi/webgl/tests/js/webgl.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import { FakeGl, canvas } from './fake_gl.mjs';

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const source = readFileSync(here('../../src/js/omui_webgl.js'), 'utf8');
const plain = (v) => JSON.parse(JSON.stringify(v));
// Values made in the library's realm, compared as data.
const same = (actual, expected, message) => assert.deepEqual(plain(actual), plain(expected), message);

// The library on a page: `gl` the context its canvases give (null: none).
function load({ gl = new FakeGl(), page = true, selected = null } = {}) {
  const memory = new ArrayBuffer(1 << 20);
  const made = [];
  const context = {
    Module: {}, console, Math, String, Number, Object, Array, Set, Map, Uint8Array, Uint16Array, Uint32Array, Float32Array, Error,
    HEAPU8: new Uint8Array(memory), HEAPU32: new Uint32Array(memory), HEAPF32: new Float32Array(memory),
    UTF8ToString: (p, n) => (typeof p === 'string' ? p : new TextDecoder().decode(new Uint8Array(memory, p, n))),
    stringToNewUTF8: (s) => s,
  };
  if (page) {
    context.document = {
      createElement: (tag) => { const c = canvas(gl); made.push([tag, c]); return c; },
      querySelector: (s) => (s === '#canvas' ? (selected ??= canvas(gl, 16, 12)) : null),
    };
  }
  let lib = null;
  context.addToLibrary = (o) => { lib = o; };
  vm.createContext(context);
  vm.runInContext(source, context, { filename: 'omui_webgl.js' });
  for (const [k, v] of Object.entries(lib)) {
    if (k.includes('__')) continue;
    context[k.startsWith('$') ? k.slice(1) : k] = v;
  }
  return { c: context, G: context.omuiGl, gl, made, selected: () => selected };
}

class Heap {
  constructor(context) { this.c = context; this.top = 4096; }
  words(list) { const at = this.top; this.c.HEAPU32.set(list, at >> 2); this.top += list.length * 4 + 64; return at; }
  bytes(list) { const at = this.top; this.c.HEAPU8.set(list, at); this.top += Math.ceil(list.length / 64) * 64 + 64; return at; }
  text(s) { return this.bytes(new TextEncoder().encode(s)); }
}

const f = (x) => new Uint32Array(new Float32Array([x]).buffer)[0];
const NONE = 0xffffffff;
function wire(G) {
  const O = G.OP, words = [];
  const add = (op, ...fields) => { words.push(op | (fields.length << 8), ...fields); return api; };
  const api = {
    words,
    upload_buffer: (b, offset, at, n) => add(O.upload_buffer, b, offset, at, 0, n),
    upload_texture: (t, x, y, w, h, at, n, pitch = 0) => add(O.upload_texture, t, x, y, w, h, at, 0, n, pitch),
    copy_texture: (from, x, y, w, h, to, dx, dy) => add(O.copy_texture, from, x, y, w, h, to, dx, dy),
    begin_render: (t, load, clear, resolve = NONE, depth = NONE, dload = 0, dclear = 1, store = 0) =>
      add(O.begin_render, t, load, ...clear.map(f), resolve, depth, dload, f(dclear), 0, store),
    end_render: () => add(O.end_render),
    set_pipeline: (p) => add(O.set_pipeline, p),
    set_scissor: (x, y, w, h) => add(O.set_scissor, x, y, w, h),
    bind_vertex_buffer: (slot, b, offset = 0) => add(O.bind_vertex_buffer, slot, b, offset),
    bind_index_buffer: (b, offset, wide) => add(O.bind_index_buffer, b, offset, wide ? 1 : 0),
    bind_texture: (stage, slot, t, s) => add(O.bind_texture, stage, slot, t, s),
    push_uniforms: (stage, slot, at, n) => add(O.push_uniforms, stage, slot, at, 0, n),
    draw: (n, instances = 1, first = 0, firstInstance = 0) => add(O.draw, n, instances, first, firstInstance),
    draw_indexed: (n, instances, first, offset, firstInstance) => add(O.draw_indexed, n, instances, first, offset >>> 0, firstInstance),
  };
  return api;
}

// A pipeline's words (openmui.rhi:wire's): a quad's, two buffers (per vertex
// float2 + float4 at 24 bytes; per instance float2 at 8), the stages' counts given.
function quadWords({ vertex = [0, 0, 0, 1], fragment = [1, 0, 0, 1], blend = [1, 1, 7, 0, 1, 7, 0], depth = NONE } = {}) {
  return [...vertex, ...fragment, 0, ...blend, 0, depth, depth === NONE ? 0 : 1, depth === NONE ? 0 : 1, 3, 1,
          2, 24, 0, 2, 0, 1, 0, 1, 3, 8, 8, 1, 1, 2, 1, 0];
}

function opened(options = {}) {
  const l = load(options);
  const heap = new Heap(l.c);
  const info = heap.words([0, 0, 0, 0, 0]);
  assert.equal(l.c.omui_webgl_open('', info), 1);
  return { ...l, heap, info };
}

function pipeline(l, words = quadWords()) {
  const vs = '#version 300 es\nvoid main() {}', fs = '#version 300 es\nvoid main() {}';
  return l.c.omui_webgl_graphics(0, l.heap.words(words), words.length, l.heap.text(vs), vs.length, l.heap.text(fs), fs.length, 'quad');
}

function enumerators(file, name) {
  const text = readFileSync(here(file), 'utf8');
  const m = new RegExp(`enum class ${name}\\s*:\\s*std::\\w+\\s*\\{([^}]*)\\}`).exec(text);
  assert.ok(m, `${file} declares ${name}`);
  return m[1].replace(/\/\/[^\n]*/g, '').split(',').map((s) => s.trim().split(/\s*=/)[0]).filter(Boolean);
}
function bits(file, name) {
  const m = new RegExp(`enum class ${name}\\s*:\\s*std::\\w+\\s*\\{([^}]*)\\}`).exec(readFileSync(here(file), 'utf8'));
  const out = {};
  for (const [, k, v] of m[1].matchAll(/(\w+)\s*=\s*(?:1 << (\d+)|0)/g)) if (v !== undefined) out[k] = 1 << Number(v);
  return out;
}
const types = '../../../src/types.cppm';

test('the page and the C++ agree: rhi\'s enumerations, the wire\'s ops, the kinds of resource', () => {
  const { G } = load();
  same(G.FORMATS.map((x) => x[0]), ['RGBA8', 'RGBA8', 'R8', 'RGBA16F', 'R32F', 'R32UI', 'DEPTH_COMPONENT32F', 'DEPTH32F_STENCIL8']);
  assert.equal(G.FORMATS.length, enumerators(types, 'Format').length);
  assert.equal(G.FORMATS[enumerators(types, 'Format').indexOf('bgra8_unorm')][5], true, 'bgra8 kept as RGBA8, its bytes swapped');
  assert.equal(G.VERTEX_FORMATS.length, enumerators(types, 'VertexFormat').length);
  same(G.BLEND_FACTORS, enumerators(types, 'BlendFactor').map((s) => s.toUpperCase()));
  same(G.BLEND_OPS, enumerators(types, 'BlendOp').map((s) => (s === 'min' || s === 'max' ? s.toUpperCase() : 'FUNC_' + s.toUpperCase())));
  same(G.TOPOLOGIES, enumerators(types, 'Topology').map((t) => ({ triangle_list: 'TRIANGLES', triangle_strip: 'TRIANGLE_STRIP', line_list: 'LINES' })[t]));
  same(G.COMPARES, enumerators(types, 'Compare').map((c) => ({ less_equal: 'LEQUAL', not_equal: 'NOTEQUAL', greater_equal: 'GEQUAL' })[c] ?? c.toUpperCase()));
  same(G.ADDRESS, enumerators(types, 'AddressMode').map((a) => ({ clamp: 'CLAMP_TO_EDGE', repeat: 'REPEAT', mirror: 'MIRRORED_REPEAT' })[a]));
  same(G.FILTERS, enumerators(types, 'Filter').map((x) => x.toUpperCase()));
  same(Object.keys(G.STAGE), enumerators(types, 'Stage'));
  same(Object.keys(G.LOAD), enumerators('../../../src/commands.cppm', 'LoadOp'));
  same(G.TEX, bits(types, 'TextureUsage'));
  same(G.BUF, bits(types, 'BufferUsage'));
  same(Object.entries(G.OP).sort((a, b) => a[1] - b[1]).map((e) => e[0]), enumerators('../../../src/wire.cppm', 'Op'));
  same(Object.keys(G.KIND), enumerators('../../src/page.cppm', 'Kind'));
});

test('a context: the canvas present() shows on, or one of the page\'s own; none without WebGL2', () => {
  const { c, G, gl, made, heap, info } = opened();
  same([...c.HEAPU32.subarray(info >> 2, (info >> 2) + 5)], [1, 4096, 4, 1, 1], 'software, 4096, 4 samples, float targets');
  assert.match(c.omui_webgl_name(), /SwiftShader/);
  assert.equal(made.length, 1, 'a canvas of its own');
  same(made[0][1].attributes, { alpha: true, premultipliedAlpha: true, antialias: false, depth: false, stencil: false, preserveDrawingBuffer: false });
  assert.equal(G.shown, false);
  assert.equal(gl.calls('bindVertexArray').length, 1, 'one vertex array, bound for good');
  same(gl.calls('pixelStorei'), [['pixelStorei', 'UNPACK_ALIGNMENT', 1], ['pixelStorei', 'PACK_ALIGNMENT', 1]]);
  c.omui_webgl_close();
  assert.equal(G.gl, null);

  const shown = load();
  assert.equal(shown.c.omui_webgl_open('#canvas', heap.words([0, 0, 0, 0, 0])), 1);
  assert.equal(shown.G.canvas, shown.selected());
  assert.equal(shown.G.shown, true);
  const missing = load();
  assert.equal(missing.c.omui_webgl_open('#elsewhere', 64), 0);
  assert.match(missing.c.omui_webgl_why(), /no canvas matches #elsewhere/);
  const none = load({ gl: null });
  assert.equal(none.c.omui_webgl_open('', 64), 0);
  assert.match(none.c.omui_webgl_why(), /no WebGL2/);
  const nodePage = load({ page: false });
  assert.equal(nodePage.c.omui_webgl_open('', 64), 0);
  assert.match(nodePage.c.omui_webgl_why(), /no canvas/);
});

test('resources by slot: buffers by their kind, immutable textures, renderbuffers for samples, samplers', () => {
  const { c, G, gl } = opened();
  c.omui_webgl_buffer(0, 64, G.BUF.vertex);
  c.omui_webgl_buffer(1, 6, G.BUF.index);
  same(gl.calls('bindBuffer').map((x) => x[1]), ['COPY_WRITE_BUFFER', 'ELEMENT_ARRAY_BUFFER'], 'an index buffer bound as one from the start');
  same(gl.calls('bufferData').map((x) => x.slice(1)), [['COPY_WRITE_BUFFER', 64, 'DYNAMIC_DRAW'], ['ELEMENT_ARRAY_BUFFER', 6, 'DYNAMIC_DRAW']]);
  c.omui_webgl_texture(0, 1, 8, 4, 1);
  same(gl.calls('texStorage2D')[0].slice(1), ['TEXTURE_2D', 1, 'RGBA8', 8, 4], 'bgra8 as RGBA8');
  c.omui_webgl_texture(1, 7, 8, 4, 4);
  same(gl.calls('renderbufferStorageMultisample')[0].slice(1), ['RENDERBUFFER', 4, 'DEPTH32F_STENCIL8', 8, 4]);
  c.omui_webgl_sampler(0, 0, 1, 1, 2);
  same(gl.calls('samplerParameteri').map((x) => x.slice(2)),
       [['TEXTURE_MIN_FILTER', 'NEAREST'], ['TEXTURE_MAG_FILTER', 'LINEAR'], ['TEXTURE_WRAP_S', 'REPEAT'], ['TEXTURE_WRAP_T', 'MIRRORED_REPEAT']]);
  c.omui_webgl_destroy(G.KIND.texture, 1);
  assert.equal(gl.calls('deleteRenderbuffer').length, 1);
  assert.equal(G.textures[1], null);
});

test('a pipeline: its program, the samplers on their units, the blocks at their points; a compile error says why', () => {
  const l = opened();
  assert.equal(pipeline(l), 1);
  const { gl, G, c } = l;
  same(gl.calls('uniform1i').map((x) => [x[1].uniform, x[2]]), [['omui_fs_sampler0', 0]], 'a fragment sampler i on unit i');
  same(gl.calls('uniformBlockBinding').map((x) => x.slice(2)), [[0, 0], [1, 4]], 'vertex slot 0 at point 0, fragment slot 0 at point 4');
  same(G.pipelines[0].blocks, [[16], [32]], 'the blocks\' sizes, what a binding covers');
  same(G.pipelines[0].blend, { src: 1, dst: 0x303, op: 0x8006, srcAlpha: 1, dstAlpha: 0x303, opAlpha: 0x8006 });
  assert.equal(G.pipelines[0].mode, 0x0004);
  // A vertex sampler goes on unit 16 + i.
  pipeline(l, quadWords({ vertex: [1, 0, 0, 0], fragment: [0, 0, 0, 0] }));
  same(gl.calls('uniform1i').at(-1).slice(2), [16]);
  const bad = opened({ gl: new FakeGl({ compiles: false }) });
  assert.equal(pipeline(bad), 0);
  assert.match(bad.c.omui_webgl_error(), /compiling quad \(vertex\): ERROR: 0:3: syntax error/);
  void c;
});

test('a submission in its order: uploads, a cleared pass on its framebuffer, the pipeline\'s state, the draw\'s bindings', () => {
  const l = opened();
  const { c, G, gl, heap } = l;
  c.omui_webgl_buffer(0, 96, G.BUF.vertex);
  c.omui_webgl_buffer(1, 24, G.BUF.vertex);
  c.omui_webgl_texture(0, 0, 8, 8, 1);
  c.omui_webgl_texture(1, 0, 2, 2, 1);
  c.omui_webgl_sampler(0, 0, 0, 0, 0);
  pipeline(l);
  gl.log = [];
  gl.raw = [];
  const vertices = heap.bytes(Array.from({ length: 96 }, (_, i) => i));
  const scale = heap.bytes(Array.from({ length: 16 }, () => 7));
  const tint = heap.bytes(Array.from({ length: 16 }, () => 9));
  const w = wire(G).upload_buffer(0, 0, vertices, 96).push_uniforms(0, 0, scale, 16)
    .begin_render(0, G.LOAD.clear, [0, 0, 0, 1]).set_pipeline(0).bind_vertex_buffer(0, 0).bind_vertex_buffer(1, 1, 8)
    .bind_texture(1, 0, 1, 0).push_uniforms(1, 0, tint, 16).set_scissor(6, 6, 4, 4).draw(6, 2, 0, 1).end_render().words;
  assert.equal(c.omui_webgl_run(heap.words(w), w.length), 1);
  // Uniforms first, in one write; then the list in order.
  const names = gl.log.map((x) => x[0]);
  assert.ok(names.indexOf('bufferData') < names.indexOf('bufferSubData'), 'the uniform buffer made, then written once');
  same(gl.calls('bufferSubData', true).map((x) => [x[1], x[2], x[4], x[5]]), [[gl.UNIFORM_BUFFER, 0, 0, 512], [gl.COPY_WRITE_BUFFER, 0, vertices, 96]]);
  same(gl.calls('framebufferTexture2D')[0].slice(1, 4), ['FRAMEBUFFER', 'COLOR_ATTACHMENT0', 'TEXTURE_2D']);
  same(gl.calls('clearBufferfv')[0].slice(1, 3), ['COLOR', 0]);
  same([...gl.calls('clearBufferfv')[0][3]], [0, 0, 0, 1]);
  assert.ok(names.indexOf('disable') < names.indexOf('clearBufferfv'), 'the scissor off for the clear');
  same(gl.calls('blendFuncSeparate')[0].slice(1), [1, 'ONE_MINUS_SRC_ALPHA', 1, 'ONE_MINUS_SRC_ALPHA']);
  same(gl.calls('scissor')[0].slice(1), [6, 6, 2, 2], 'inside the target');
  // The attributes: per vertex from offset 0, per instance from the first instance on.
  same(gl.calls('vertexAttribPointer').map((x) => x.slice(1)),
       [[0, 2, 'FLOAT', false, 24, 0], [1, 4, 'FLOAT', false, 24, 8], [2, 2, 'FLOAT', false, 8, 8 + 8]]);
  same(gl.calls('vertexAttribDivisor').map((x) => x.slice(1)), [[0, 0], [1, 0], [2, 1]]);
  same(gl.calls('activeTexture').map((x) => x[1]), ['TEXTURE0']);
  same(gl.calls('bindSampler').map((x) => x[1]), [0]);
  same(gl.calls('bindBufferRange').map((x) => [x[1], x[2], x[4], x[5]]), [['UNIFORM_BUFFER', 0, 0, 16], ['UNIFORM_BUFFER', 4, 256, 32]],
       'each at its offset, covering its block');
  same(gl.calls('drawArraysInstanced')[0].slice(1), [gl.TRIANGLES, 0, 6, 2]);
  same(gl.calls('bindFramebuffer').at(-1).slice(1), ['FRAMEBUFFER', null], 'the page\'s framebuffer back when done');

  // Indexed: the vertex offset moved into the per-vertex pointers, the first index into bytes.
  gl.log = [];
  c.omui_webgl_buffer(2, 36, G.BUF.index);
  const w2 = wire(G).begin_render(0, G.LOAD.load, [0, 0, 0, 0]).set_pipeline(0).bind_vertex_buffer(0, 0).bind_vertex_buffer(1, 1)
    .bind_texture(1, 0, 1, 0).bind_index_buffer(2, 0, true).draw_indexed(6, 1, 3, 2, 0).end_render().words;
  assert.equal(c.omui_webgl_run(heap.words(w2), w2.length), 1);
  same(gl.calls('vertexAttribPointer').map((x) => x[6]), [48, 56, 0], 'two vertices of 24 bytes on');
  same(gl.calls('drawElementsInstanced')[0].slice(1), [gl.TRIANGLES, 6, 'UNSIGNED_INT', 12, 1]);
  assert.equal(gl.calls('clearBufferfv').length, 0, 'a pass that loads clears nothing');
});

test('copies and resolves read one framebuffer and draw another, both made before either is bound', () => {
  const l = opened();
  const { c, G, gl, heap } = l;
  c.omui_webgl_texture(0, 0, 8, 8, 1);
  c.omui_webgl_texture(1, 0, 8, 8, 1);
  c.omui_webgl_texture(2, 0, 8, 8, 4);
  gl.log = [];
  const w = wire(G).copy_texture(0, 1, 1, 2, 2, 1, 5, 5).begin_render(2, G.LOAD.clear, [0, 0, 0, 0], 0).end_render().words;
  assert.equal(c.omui_webgl_run(heap.words(w), w.length), 1);
  const binds = gl.calls('bindFramebuffer').filter((b) => b[1] !== 'FRAMEBUFFER');
  same(binds.map((b) => [b[1], b[2].id]).slice(0, 2), [['READ_FRAMEBUFFER', G.framebuffers.get('0:' + NONE).id],
                                                        ['DRAW_FRAMEBUFFER', G.framebuffers.get('1:' + NONE).id]]);
  same(gl.calls('blitFramebuffer')[0].slice(1), [1, 1, 3, 3, 5, 5, 7, 7, 'COLOR_BUFFER_BIT', 'NEAREST']);
  same(gl.calls('framebufferRenderbuffer')[0].slice(1, 3), ['FRAMEBUFFER', 'COLOR_ATTACHMENT0'], 'the samples a renderbuffer');
  const resolve = binds.slice(2).map((b) => [b[1], b[2].id]);
  same(resolve, [['READ_FRAMEBUFFER', G.framebuffers.get('2:' + NONE).id], ['DRAW_FRAMEBUFFER', G.framebuffers.get('0:' + NONE).id]]);
  same(gl.calls('blitFramebuffer')[1].slice(1), [0, 0, 8, 8, 0, 0, 8, 8, 'COLOR_BUFFER_BIT', 'NEAREST']);
});

test('textures: bgra8\'s bytes swapped both ways, rows at their pitch; r8, half floats and words read back as rhi lays them', () => {
  const l = opened();
  const { c, G, gl, heap } = l;
  c.omui_webgl_texture(0, 1, 2, 1, 1);  // bgra8
  const bgra = heap.bytes([10, 20, 30, 255, 40, 50, 60, 128]);
  const w = wire(G).upload_texture(0, 0, 0, 2, 1, bgra, 8).words;
  c.omui_webgl_run(heap.words(w), w.length);
  const up = gl.calls('texSubImage2D')[0];
  same([...up[9]], [30, 20, 10, 255, 60, 50, 40, 128], 'R and B swapped into RGBA8');
  const out = 65536;
  assert.equal(c.omui_webgl_read_texture(0, 0, 0, 2, 1, out), 1);
  // The fake's pattern: pixel (x, y) channel k is x + 16y + k; swapped back to B G R A.
  same([...c.HEAPU8.subarray(out, out + 8)], [2, 1, 0, 3, 3, 2, 1, 4]);
  // r8 with rows 4 bytes apart: in place, the row length in texels.
  c.omui_webgl_texture(1, 2, 3, 2, 1);
  const r8 = heap.bytes([1, 2, 3, 0, 4, 5, 6, 0]);
  const w2 = wire(G).upload_texture(1, 0, 0, 3, 2, r8, 8, 4).words;
  c.omui_webgl_run(heap.words(w2), w2.length);
  const up2 = gl.calls('texSubImage2D')[1];
  assert.equal(up2[9], c.HEAPU8, 'read where it lies');
  assert.equal(up2[10], r8);
  same(gl.calls('pixelStorei').filter((p) => p[1] === 'UNPACK_ROW_LENGTH').map((p) => p[2]), [2, 0, 4, 0]);
  c.omui_webgl_read_texture(1, 1, 0, 2, 1, out);
  same([...c.HEAPU8.subarray(out, out + 2)], [1, 2], 'red of each RGBA');
  // rgba16f: read as floats, written as halves.
  c.omui_webgl_texture(2, 3, 1, 1, 1);
  c.omui_webgl_read_texture(2, 0, 0, 1, 1, out);
  same([...new Uint16Array(c.HEAPU8.buffer.slice(out, out + 8))], [0, 0x1c00, 0x2000, 0x2200], '0, 1/256, 2/256, 3/256 as halves');
  same([G.half(0.5), G.half(0.25), G.half(1), G.half(0), G.half(-2), G.half(65504), G.half(1e9)], [0x3800, 0x3400, 0x3c00, 0, 0xc000, 0x7bff, 0x7c00]);
  // r32ui: RGBA_INTEGER, the red word of each.
  c.omui_webgl_texture(3, 5, 2, 1, 1);
  c.omui_webgl_read_texture(3, 0, 0, 2, 1, out);
  same(gl.calls('readPixels').at(-1).slice(5, 7), ['RGBA_INTEGER', 'UNSIGNED_INT']);
  same([...new Uint32Array(c.HEAPU8.buffer.slice(out, out + 8))], [0, 1]);
});

test('buffers read back where they are; GL\'s errors and a lost context go to the next call', () => {
  const l = opened();
  const { c, G, gl, heap } = l;
  c.omui_webgl_buffer(0, 16, G.BUF.vertex);
  const data = heap.bytes(Array.from({ length: 16 }, (_, i) => 100 + i));
  const w = wire(G).upload_buffer(0, 0, data, 16).words;
  c.omui_webgl_run(heap.words(w), w.length);
  const out = 65536;
  assert.equal(c.omui_webgl_read_buffer(0, 6, 5, out), 1);
  same([...c.HEAPU8.subarray(out, out + 5)], [106, 107, 108, 109, 110]);
  assert.equal(c.omui_webgl_error(), 0);
  gl.errors.push(0x0502);
  assert.equal(c.omui_webgl_run(heap.words(w), w.length), 0);
  assert.match(c.omui_webgl_error(), /GL INVALID_OPERATION in the submission/);
  assert.equal(c.omui_webgl_error(), 0, 'said once');
  G.canvas.listeners.webglcontextlost({ preventDefault() {} });
  assert.equal(c.omui_webgl_lost(), 1);
  assert.match(c.omui_webgl_error(), /lost/);
});

test('present: the frame blitted onto the canvas flipped, texel for pixel at the same size; nothing on a canvas of no size', () => {
  const l = load();
  const heap = new Heap(l.c);
  assert.equal(l.c.omui_webgl_open('#canvas', heap.words([0, 0, 0, 0, 0])), 1);
  const { c, gl } = l;
  c.omui_webgl_texture(0, 0, 16, 12, 1);
  assert.equal(c.omui_webgl_present(0), 1);
  same(gl.calls('blitFramebuffer').at(-1).slice(1), [0, 0, 16, 12, 0, 12, 16, 0, 'COLOR_BUFFER_BIT', 'NEAREST'], 'flipped: GL\'s canvas is bottom-up');
  const draw = gl.calls('bindFramebuffer').find((b) => b[1] === 'DRAW_FRAMEBUFFER');
  assert.equal(draw[2], null, 'the canvas\'s own');
  l.selected().width = 32;
  c.omui_webgl_present(0);
  same(gl.calls('blitFramebuffer').at(-1).slice(1), [0, 0, 16, 12, 0, 12, 32, 0, 'COLOR_BUFFER_BIT', 'LINEAR'], 'scaled: filtered');
  l.selected().width = 0;
  assert.equal(c.omui_webgl_present(0), 0);
});
