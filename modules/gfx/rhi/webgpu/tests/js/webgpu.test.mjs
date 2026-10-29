// The page's half of openmui.rhi.webgpu (src/js/omui_webgpu.js) under node,
// with a fake WebGPU that remembers what it is asked (fake_gpu.mjs): the
// library loaded as Emscripten would link it — its `#if ASYNCIFY` taken
// either way, the program's memory an ArrayBuffer, a "pointer" to a string
// the string itself — and fed what the C++ side sends: resources by slot,
// a submission as rhi's wire words. What WebGPU is asked for is checked,
// deterministically; the real thing runs in tools/ci/browser.sh.
//
//   node --test modules/gfx/rhi/webgpu/tests/js/webgpu.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import { FakeGpu, GPUBufferUsage, GPUMapMode, GPUTextureUsage } from './fake_gpu.mjs';

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const source = readFileSync(here('../../src/js/omui_webgpu.js'), 'utf8');
const plain = (v) => JSON.parse(JSON.stringify(v));
// Values made in the library's realm, compared as data.
const same = (actual, expected, message) => assert.deepEqual(plain(actual), plain(expected), message);

// Emscripten's preprocessing of a library, for the one setting it reads.
function preprocess(text, settings) {
  const out = [];
  const keep = [true];
  for (const line of text.split('\n')) {
    const m = /^#(if|else|endif)\b\s*(\w*)/.exec(line);
    if (!m) { if (keep.every(Boolean)) out.push(line); continue; }
    if (m[1] === 'if') keep.push(!!settings[m[2]]);
    else if (m[1] === 'else') keep.push(!keep.pop());
    else keep.pop();
  }
  return out.join('\n');
}

// The library on a fake GPU: its functions and state, the memory, the
// run dependencies taken and released.
async function load({ asyncify = false, gpu = new FakeGpu(), navigator } = {}) {
  const memory = new ArrayBuffer(1 << 20);
  const deps = [];
  const context = {
    Module: {}, console, Promise, Math, String, Uint8Array, Uint32Array, Float32Array,
    navigator: navigator === undefined ? gpu.navigator : navigator,
    GPUBufferUsage, GPUTextureUsage, GPUMapMode,
    HEAPU8: new Uint8Array(memory), HEAPU32: new Uint32Array(memory), HEAPF32: new Float32Array(memory),
    UTF8ToString: (p, n) => (typeof p === 'string' ? p : new TextDecoder().decode(new Uint8Array(memory, p, n))),
    stringToNewUTF8: (s) => s,
    addRunDependency: (id) => deps.push(['add', id]),
    removeRunDependency: (id) => deps.push(['remove', id]),
  };
  let lib = null;
  context.addToLibrary = (o) => { lib = o; };
  vm.createContext(context);
  vm.runInContext(preprocess(source, { ASYNCIFY: asyncify ? 1 : 0 }), context, { filename: 'omui_webgpu.js' });
  for (const [k, v] of Object.entries(lib)) {
    if (k.includes('__')) continue;
    context[k.startsWith('$') ? k.slice(1) : k] = v;
  }
  vm.runInContext(lib.$omuiGpu__postset, context);
  await new Promise((r) => setImmediate(r));
  return { context, G: context.omuiGpu, gpu, deps, memory };
}

// The program's memory, written as the C++ side would: words and bytes at
// addresses the test chooses.
class Heap {
  constructor(context) { this.c = context; this.top = 4096; }
  words(list) {
    const at = this.top;
    this.c.HEAPU32.set(list, at >> 2);
    this.top += list.length * 4 + 64;
    return at;
  }
  bytes(list) {
    const at = this.top;
    this.c.HEAPU8.set(list, at);
    this.top += Math.ceil(list.length / 64) * 64 + 64;
    return at;
  }
  text(s) { return this.bytes(new TextEncoder().encode(s)); }
}

// rhi's wire, as openmui.rhi:wire writes it (the ops' numbers from the C++).
const f = (x) => new Uint32Array(new Float32Array([x]).buffer)[0];
const NONE = 0xffffffff;
function wire(G) {
  const O = G.OP;
  const words = [];
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
    bind_texture: (stage, slot, t, s) => add(O.bind_texture, stage, slot, t, s),
    push_uniforms: (stage, slot, at, n) => add(O.push_uniforms, stage, slot, at, 0, n),
    draw: (n, instances = 1, first = 0, firstInstance = 0) => add(O.draw, n, instances, first, firstInstance),
    begin_compute: (textures, buffers) => add(O.begin_compute, textures.length, ...textures, buffers.length, ...buffers),
    set_compute_pipeline: (p) => add(O.set_compute_pipeline, p),
    dispatch: (x, y, z) => add(O.dispatch, x, y, z),
    end_compute: () => add(O.end_compute),
  };
  return api;
}

// The graphics words device.cpp writes (graphics_words): a textured quad's.
function quadWords({ blend = [0, 1, 0, 0, 1, 0, 0], depth = NONE, samples = 1, vertex = [0, 0, 0, 1], fragment = [1, 0, 0, 1] } = {}) {
  return [...vertex, ...fragment, 0, ...blend, 0, depth, depth === NONE ? 0 : 1, depth === NONE ? 0 : 1, 1, samples,
          1, 16, 0, 2, 0, 1, 0, 1, 1, 8];
}

// rhi's C++ enumerations, read from their sources.
function enumerators(file, name) {
  const text = readFileSync(here(file), 'utf8');
  const m = new RegExp(`enum class ${name}\\s*:\\s*std::\\w+\\s*\\{([^}]*)\\}`).exec(text);
  assert.ok(m, `${file} declares ${name}`);
  return m[1].replace(/\/\/[^\n]*/g, '').split(',').map((s) => s.trim().split(/\s*=/)[0]).filter(Boolean);
}
function bits(file, name) {
  const text = readFileSync(here(file), 'utf8');
  const m = new RegExp(`enum class ${name}\\s*:\\s*std::\\w+\\s*\\{([^}]*)\\}`).exec(text);
  const out = {};
  for (const [, k, v] of m[1].matchAll(/(\w+)\s*=\s*(?:1 << (\d+)|0)/g)) if (v !== undefined) out[k] = 1 << Number(v);
  return out;
}

const types = '../../../src/types.cppm';

test('the page and the C++ agree: rhi\'s enumerations, the wire\'s ops, the kinds of resource', async () => {
  const { G } = await load();
  const webgpu = {
    rgba8_unorm: 'rgba8unorm', bgra8_unorm: 'bgra8unorm', r8_unorm: 'r8unorm', rgba16_float: 'rgba16float', r32_float: 'r32float',
    r32_uint: 'r32uint', d32_float: 'depth32float', d32_float_s8: 'depth32float-stencil8',
  };
  same(G.FORMATS, enumerators(types, 'Format').map((f) => webgpu[f]));
  same(G.VERTEX_FORMATS, enumerators(types, 'VertexFormat').map((v) =>
    ({ float1: 'float32', float2: 'float32x2', float3: 'float32x3', float4: 'float32x4', ubyte4_norm: 'unorm8x4', uint1: 'uint32' })[v]));
  const dashed = (s) => s.replaceAll('_', '-').replace('color', '').replace('--', '-').replace(/-$/, '');
  same(G.BLEND_FACTORS, enumerators(types, 'BlendFactor').map(dashed));
  same(G.BLEND_OPS, enumerators(types, 'BlendOp').map((s) => s.replaceAll('_', '-')));
  same(G.TOPOLOGIES, enumerators(types, 'Topology').map((s) => s.replaceAll('_', '-')));
  same(G.COMPARES, enumerators(types, 'Compare').map((s) => s.replaceAll('_', '-')));
  same(G.ADDRESS, enumerators(types, 'AddressMode').map((a) => ({ clamp: 'clamp-to-edge', repeat: 'repeat', mirror: 'mirror-repeat' })[a]));
  same(G.FILTERS, enumerators(types, 'Filter'));
  same(Object.keys(G.STAGE), enumerators(types, 'Stage'));
  same(Object.keys(G.LOAD), enumerators('../../../src/commands.cppm', 'LoadOp'));
  same((G.TEX), bits(types, 'TextureUsage'));
  same((G.BUF), bits(types, 'BufferUsage'));
  same(Object.entries(G.OP).sort((a, b) => a[1] - b[1]).map((e) => e[0]), enumerators('../../../src/wire.cppm', 'Op'));
  same(Object.keys(G.KIND), enumerators('../../src/page.cppm', 'Kind'));
  assert.equal(G.BYTES.length, G.FORMATS.length);
});

test('the device is asked for as the program loads: main() waits for it', async () => {
  const { context, deps, gpu } = await load();
  same(deps, [['add', 'omui-webgpu'], ['remove', 'omui-webgpu']], 'a run dependency, released once the device came');
  const desc = gpu.calls('requestDevice')[0][1];
  same((desc.requiredFeatures), ['depth32float-stencil8'], 'depth and stencil when the adapter has them');
  assert.equal(desc.requiredLimits.maxTextureDimension2D, 16384, 'the adapter\'s largest texture');
  const out = 64;
  assert.equal(context.omui_webgpu_device(out), 1);
  same([...context.HEAPU32.subarray(out >> 2, (out >> 2) + 3)], [0, 16384, 1]);
  assert.equal(context.omui_webgpu_name(), 'fake test');
  assert.equal(context.omui_webgpu_suspends(), 0);

  // A software adapter says so; one without the feature has no depth targets.
  const soft = await load({ gpu: new FakeGpu({ features: [], info: { vendor: 'google', architecture: 'swiftshader' } }) });
  assert.equal(soft.context.omui_webgpu_device(out), 1);
  same([...soft.context.HEAPU32.subarray(out >> 2, (out >> 2) + 3)], [1, 16384, 0]);

  // No WebGPU, or no adapter: no device, the program told why, and nothing waited for.
  const none = await load({ navigator: {} });
  same(none.deps, []);
  assert.equal(none.context.omui_webgpu_device(out), 0);
  assert.match(none.context.omui_webgpu_why(), /navigator\.gpu/);
  const refused = await load({ gpu: new FakeGpu({ adapter: false }) });
  assert.equal(refused.context.omui_webgpu_device(out), 0);
  assert.match(refused.context.omui_webgpu_why(), /no WebGPU adapter/);
});

test('resources by slot: usages, formats, samples; a pipeline from its words, "auto" layout', async () => {
  const { context: c, G, gpu } = await load();
  const heap = new Heap(c);
  assert.equal(c.omui_webgpu_buffer(0, 6, G.BUF.index, 'indices'), 1);
  assert.equal(gpu.calls('createBuffer')[0][1].size, 8, 'rounded up to 4 bytes');
  assert.equal(gpu.calls('createBuffer')[0][1].usage, GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST | GPUBufferUsage.INDEX);
  assert.equal(c.omui_webgpu_texture(1, 0, 8, 8, G.TEX.color_target | G.TEX.sampled, 1, 'target'), 1);
  let t = gpu.calls('createTexture').at(-1)[1];
  assert.equal(t.format, 'rgba8unorm');
  assert.equal(t.usage, GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT);
  c.omui_webgpu_texture(2, 0, 8, 8, G.TEX.color_target, 4, 'msaa');
  t = gpu.calls('createTexture').at(-1)[1];
  assert.equal(t.sampleCount, 4);
  assert.equal(t.usage, GPUTextureUsage.RENDER_ATTACHMENT, 'a multisampled target is only rendered to');
  c.omui_webgpu_texture(3, 0, 8, 8, G.TEX.compute_write, 1, 'written');
  assert.ok(gpu.calls('createTexture').at(-1)[1].usage & GPUTextureUsage.STORAGE_BINDING);
  c.omui_webgpu_sampler(0, 0, 1, 1, 2);
  same((gpu.calls('createSampler')[0][1]),
                   { minFilter: 'nearest', magFilter: 'linear', mipmapFilter: 'nearest', addressModeU: 'repeat', addressModeV: 'mirror-repeat' });

  const premultiplied = [1, 1, 7, 0, 1, 7, 0];
  const words = heap.words(quadWords({ blend: premultiplied, depth: 6, samples: 4 }));
  const vs = 'vertex code', fs = 'fragment code';
  assert.equal(c.omui_webgpu_graphics(0, words, quadWords().length, heap.text(vs), vs.length, 'quad_vs', heap.text(fs), fs.length, 'quad_fs',
                                      'quad'), 1);
  const p = gpu.calls('createRenderPipeline')[0][1];
  assert.equal(p.layout, 'auto');
  assert.equal(p.vertex.module.code, vs);
  assert.equal(p.vertex.entryPoint, 'quad_vs');
  same((p.vertex.buffers), [{ arrayStride: 16, stepMode: 'vertex', attributes: [
    { shaderLocation: 0, format: 'float32x2', offset: 0 }, { shaderLocation: 1, format: 'float32x2', offset: 8 }] }]);
  same((p.fragment.targets), [{ format: 'rgba8unorm', blend: {
    color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
    alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' } } }]);
  same((p.depthStencil), { format: 'depth32float', depthWriteEnabled: true, depthCompare: 'less' });
  assert.equal(p.multisample.count, 4);
  assert.equal(G.pipelines[0].groups, 4, 'the vertex stage\'s uniforms and the fragment stage\'s texture and uniforms: groups 0–3');
  // min and max take factors of one in WebGPU.
  const words2 = heap.words(quadWords({ blend: [1, 6, 7, 4, 1, 1, 3] }));
  c.omui_webgpu_graphics(1, words2, quadWords().length, heap.text(vs), vs.length, 'quad_vs', heap.text(fs), fs.length, 'quad_fs', 'max');
  const b = gpu.calls('createRenderPipeline')[1][1].fragment.targets[0].blend;
  same((b.color), { srcFactor: 'one', dstFactor: 'one', operation: 'max' });
  same((b.alpha), { srcFactor: 'one', dstFactor: 'one', operation: 'min' });
  assert.equal(gpu.calls('createShaderModule').length, 2, 'one module a text, kept');

  c.omui_webgpu_destroy(G.KIND.texture, 2);
  assert.equal(G.textures[2], null);
  same(gpu.calls('destroyTexture'), [['destroyTexture', 'msaa']]);
});

test('a submission is one command encoder: uploads staged and copied in the list\'s order, uniforms at 256-byte offsets', async () => {
  const { context: c, G, gpu } = await load();
  const heap = new Heap(c);
  c.omui_webgpu_buffer(0, 64, G.BUF.vertex, 'quad');
  c.omui_webgpu_texture(0, 0, 8, 8, G.TEX.color_target, 1, 'target');
  c.omui_webgpu_texture(1, 2, 3, 2, G.TEX.sampled, 1, 'mask');
  c.omui_webgpu_sampler(0, 0, 0, 0, 0);
  const vs = 'v', fs = 'f';
  c.omui_webgpu_graphics(0, heap.words(quadWords()), quadWords().length, heap.text(vs), 1, 'quad_vs', heap.text(fs), 1, 'quad_fs', 'quad');
  gpu.log = [];

  const vertices = heap.bytes(Array.from({ length: 64 }, (_, i) => i));
  const mask = heap.bytes([1, 2, 3, 0, 4, 5, 6, 0]);  // 3×2 texels of r8, rows 4 bytes apart
  const tint = heap.bytes(Array.from({ length: 16 }, () => 9));
  const scale = heap.bytes(Array.from({ length: 16 }, () => 7));
  const w = wire(G)
    .upload_buffer(0, 0, vertices, 64)
    .upload_texture(1, 0, 0, 3, 2, mask, 8, 4)
    .push_uniforms(0, 0, scale, 16)
    .begin_render(0, G.LOAD.clear, [0, 0, 0, 1])
    .set_pipeline(0)
    .bind_vertex_buffer(0, 0)
    .bind_texture(1, 0, 1, 0)
    .push_uniforms(1, 0, tint, 16)
    .set_scissor(6, 6, 4, 4)
    .draw(6)
    .end_render().words;
  assert.equal(c.omui_webgpu_run(heap.words(w), w.length), 1);
  const names = gpu.log.map((l) => l[0]);
  // The staging and uniform buffers made by the first submission (and kept).
  same(names, ['createBuffer', 'createBuffer', 'copyBufferToBuffer', 'copyBufferToTexture', 'beginRenderPass', 'setPipeline', 'setVertexBuffer', 'setScissorRect',
                           'createBindGroup', 'setBindGroup', 'createBindGroup', 'setBindGroup', 'createBindGroup', 'setBindGroup',
                           'createBindGroup', 'setBindGroup', 'draw', 'end', 'writeBuffer', 'writeBuffer', 'submit']);
  // The uploads: from the staging buffer, written once before the encoder is submitted.
  const [, from, at, to, offset, size] = gpu.calls('copyBufferToBuffer')[0];
  assert.equal(from.desc.label, 'omui.rhi.staging');
  same([at, to.desc.label, offset, size], [0, 'quad', 0, 64]);
  const [, src, dst, extent] = gpu.calls('copyBufferToTexture')[0];
  same(({ offset: src.offset, bytesPerRow: src.bytesPerRow, rowsPerImage: src.rowsPerImage }), { offset: 256, bytesPerRow: 256, rowsPerImage: 2 });
  same((dst.origin), { x: 0, y: 0 });
  same((extent), { width: 3, height: 2 });
  same([...G.staging.bytes.subarray(256, 259)], [1, 2, 3], 'row 0, packed at its pitch');
  same([...G.staging.bytes.subarray(512, 515)], [4, 5, 6], 'row 1, from 4 bytes on in the list');
  same(gpu.calls('writeBuffer').map((x) => x.slice(1)), [['omui.rhi.staging', 0, 768], ['omui.rhi.uniforms', 0, 512]]);
  // The pass: cleared, the scissor inside the target.
  const pass = gpu.calls('beginRenderPass')[0][1].colorAttachments[0];
  same(({ loadOp: pass.loadOp, storeOp: pass.storeOp, clearValue: pass.clearValue }),
                   { loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 } });
  same(gpu.calls('setScissorRect')[0].slice(1), [6, 6, 2, 2]);
  // The bind groups: 0 the vertex stage's (empty), 1 its uniforms, 2 the
  // texture at binding 0 and its sampler at 1, 3 the fragment uniforms.
  const groups = gpu.calls('createBindGroup').map((g) => g[1]);
  same(groups.map((g) => g.layout.group), [0, 1, 2, 3]);
  assert.equal(groups[0].entries.length, 0);
  same((groups[1].entries.map((e) => [e.binding, e.resource.offset, e.resource.size])), [[0, 0, 256]]);
  same(groups[2].entries.map((e) => [e.binding, e.resource.kind]), [[0, 'view'], [1, 'sampler']]);
  same((groups[3].entries.map((e) => [e.binding, e.resource.offset, e.resource.size])), [[0, 256, 256]]);
  same([...G.uniforms.bytes.subarray(0, 2)], [7, 7]);
  same([...G.uniforms.bytes.subarray(256, 258)], [9, 9]);
  same(gpu.calls('draw')[0].slice(1), [6, 1, 0, 0]);
  // The next draw with nothing changed makes no bind group.
  gpu.log = [];
  const again = wire(G).begin_render(0, G.LOAD.load, [0, 0, 0, 0]).set_pipeline(0).bind_vertex_buffer(0, 0).bind_texture(1, 0, 1, 0)
    .push_uniforms(0, 0, scale, 16).push_uniforms(1, 0, tint, 16).draw(6).draw(3).end_render().words;
  c.omui_webgpu_run(heap.words(again), again.length);
  assert.equal(gpu.calls('createBindGroup').length, 3, 'groups 1–3 for the first draw (0 kept, empty), none for the second');
  assert.equal(gpu.calls('createBuffer').length, 0, 'the staging and uniform buffers kept');
  assert.equal(gpu.calls('beginRenderPass')[0][1].colorAttachments[0].loadOp, 'load');
});

test('multisampled targets resolve and drop their samples; depth is cleared, kept when asked', async () => {
  const { context: c, G, gpu } = await load();
  const heap = new Heap(c);
  c.omui_webgpu_texture(0, 0, 8, 8, G.TEX.color_target, 4, 'msaa');
  c.omui_webgpu_texture(1, 0, 8, 8, G.TEX.color_target | G.TEX.sampled, 1, 'resolved');
  c.omui_webgpu_texture(2, 7, 8, 8, G.TEX.depth_target, 4, 'depth');
  const w = wire(G).begin_render(0, G.LOAD.clear, [0, 0, 0, 0], 1, 2, G.LOAD.clear, 0.5, 1).end_render().words;
  assert.equal(c.omui_webgpu_run(heap.words(w), w.length), 1);
  const d = gpu.calls('beginRenderPass')[0][1];
  assert.equal(d.colorAttachments[0].storeOp, 'discard');
  assert.equal(d.colorAttachments[0].resolveTarget.texture.desc.label, 'resolved');
  same(({ ...d.depthStencilAttachment, view: undefined }), {
    depthLoadOp: 'clear', depthStoreOp: 'store', depthClearValue: 0.5, stencilLoadOp: 'clear', stencilStoreOp: 'store', stencilClearValue: 0,
  });
});

test('a compute pass: what it writes in group 1, its uniforms in group 2, the work groups', async () => {
  const { context: c, G, gpu } = await load();
  const heap = new Heap(c);
  c.omui_webgpu_texture(0, 0, 8, 8, G.TEX.compute_write, 1, 'out');
  c.omui_webgpu_buffer(0, 256, G.BUF.compute_write, 'values');
  const cs = 'compute code';
  c.omui_webgpu_compute(0, heap.words([0, 0, 0, 1, 1, 1]), 6, heap.text(cs), cs.length, 'pattern', 'pattern');
  const params = heap.bytes([8, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
  const w = wire(G).begin_compute([0], [0]).set_compute_pipeline(0).push_uniforms(2, 0, params, 16).dispatch(1, 1, 1).end_compute().words;
  assert.equal(c.omui_webgpu_run(heap.words(w), w.length), 1);
  const groups = gpu.calls('createBindGroup').map((g) => g[1]);
  same(groups.map((g) => g.layout.group), [0, 1, 2]);
  same(groups[1].entries.map((e) => [e.binding, e.resource.kind ?? e.resource.buffer.desc.label]), [[0, 'view'], [1, 'values']]);
  assert.equal(groups[2].entries[0].resource.buffer.desc.label, 'omui.rhi.uniforms');
  same(gpu.calls('dispatchWorkgroups')[0].slice(1), [1, 1, 1]);
});

test('waiting and reading back: -1 where the program cannot suspend; rows packed where it can', async () => {
  const plainLib = await load();
  plainLib.context.omui_webgpu_texture(0, 0, 4, 4, plainLib.G.TEX.sampled, 1, 't');
  assert.equal(plainLib.context.omui_webgpu_wait(), -1);
  assert.equal(plainLib.context.omui_webgpu_read_texture(0, 0, 0, 4, 4, 0), -1);

  const { context: c, G, gpu } = await load({ asyncify: true });
  assert.equal(c.omui_webgpu_suspends(), 1);
  c.omui_webgpu_texture(0, 0, 4, 4, G.TEX.sampled, 1, 't');
  assert.equal(await c.omui_webgpu_wait(), 1);
  assert.equal(gpu.calls('onSubmittedWorkDone').length, 1);
  const out = 8192;
  assert.equal(await c.omui_webgpu_read_texture(0, 1, 2, 2, 2, out), 1);
  const [, source, destination] = gpu.calls('copyTextureToBuffer')[0];
  same((source.origin), { x: 1, y: 2 });
  assert.equal(destination.bytesPerRow, 256);
  // Texel (x, y), byte k is x + 16·y + k: the region's rows packed tightly.
  const texel = (x, y) => [0, 1, 2, 3].map((k) => (x + 16 * y + k) & 255);
  same([...c.HEAPU8.subarray(out, out + 16)], [...texel(1, 2), ...texel(2, 2), ...texel(1, 3), ...texel(2, 3)]);
  // A buffer: the aligned range around the bytes asked for.
  const heap = new Heap(c);
  c.omui_webgpu_buffer(0, 16, G.BUF.vertex, 'b');
  const data = heap.bytes(Array.from({ length: 16 }, (_, i) => 100 + i));
  const w = wire(G).upload_buffer(0, 0, data, 16).words;
  c.omui_webgpu_run(heap.words(w), w.length);
  assert.equal(await c.omui_webgpu_read_buffer(0, 6, 5, out), 1);
  same([...c.HEAPU8.subarray(out, out + 5)], [106, 107, 108, 109, 110]);
  const copy = gpu.calls('copyBufferToBuffer').at(-1);
  same([copy[2], copy[5]], [4, 8], 'from byte 4, 8 bytes: whole words');
});

test('what WebGPU reports goes to the next call; a lost device says so', async () => {
  const { context: c, gpu } = await load();
  assert.equal(c.omui_webgpu_error(), 0);
  gpu.raise('Pipeline layout mismatch');
  gpu.raise('Invalid texture');
  assert.equal(c.omui_webgpu_error(), 'Pipeline layout mismatch; Invalid texture');
  assert.equal(c.omui_webgpu_error(), 0, 'said once');
  // What WebGPU throws at is a failure (0), said the same way.
  c.omuiGpu.device.createBuffer = () => { throw new TypeError('Value is not of type \'unsigned long long\''); };
  assert.equal(c.omui_webgpu_buffer(0, 4, 1, 'b'), 0);
  assert.match(c.omui_webgpu_error(), /unsigned long long/);
  gpu.loseDevice('GPU reset');
  await new Promise((r) => setImmediate(r));
  assert.equal(c.omui_webgpu_lost(), 1);
  assert.match(c.omui_webgpu_error(), /lost: GPU reset/);
});

test('present: the frame drawn over the canvas, nearest at the same size; nothing on a canvas of no size', async () => {
  const canvas = { width: 8, height: 8, getContext: () => context2 };
  const context2 = { configured: null, configure(d) { this.configured = d; }, getCurrentTexture: () => ({ createView: () => ({ kind: 'swap' }) }) };
  const { context: c, G, gpu } = await load();
  c.document = { querySelector: (s) => (s === '#canvas' ? canvas : null) };
  assert.equal(c.omui_webgpu_canvas('#nothing'), 0);
  assert.match(c.omui_webgpu_error(), /no canvas matches #nothing/);
  assert.equal(c.omui_webgpu_canvas('#canvas'), 1);
  assert.equal(context2.configured.format, 'bgra8unorm');
  assert.equal(context2.configured.alphaMode, 'premultiplied');
  c.omui_webgpu_texture(0, 0, 8, 8, G.TEX.sampled | G.TEX.color_target, 1, 'frame');
  assert.equal(c.omui_webgpu_present(0), 1);
  const p = gpu.calls('createRenderPipeline').at(-1)[1];
  assert.equal(p.fragment.targets[0].format, 'bgra8unorm');
  const bind = gpu.calls('createBindGroup').at(-1)[1];
  assert.equal(bind.entries[1].resource.desc.magFilter, 'nearest', 'the same size: texel for pixel');
  same(gpu.calls('draw').at(-1).slice(1), [3]);
  canvas.width = 16;
  c.omui_webgpu_present(0);
  assert.equal(gpu.calls('createBindGroup').at(-1)[1].entries[1].resource.desc.magFilter, 'linear', 'scaled: filtered');
  canvas.width = 0;
  assert.equal(c.omui_webgpu_present(0), 0);
});
