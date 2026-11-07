// openmui.rhi.webgpu's half in the page: omui.rhi on the browser's WebGPU.
// An Emscripten library (--js-library, build.mcpp): each omui_webgpu_*
// function is linked into the program's JavaScript by name, and
// src/native/browser/page.cpp calls it.
//
// The device is asked for as the program loads (a run dependency: main()
// starts once the page has one, or knows it has none), so creating an
// omui.rhi device is synchronous. Resources are created one call each, by
// the slot index the C++ side gave them; a submission crosses in one call,
// as rhi's wire words (openmui.rhi:wire), and becomes one command encoder:
// every upload staged in one buffer written before it is submitted, copied
// in the list's order, the uniforms in another at 256-byte offsets, bind
// groups made as draws need them (the pipelines' layouts are WebGPU's
// "auto", from the shaders openmui.shader wrote: a stage's read-only group
// holds sampled texture i at binding 2i and its sampler at 2i + 1, then the
// storage textures and buffers).
//
// Waiting and reading back are asynchronous in WebGPU. A program linked to
// suspend (-sASYNCIFY, or -sJSPI) waits for them; any other is told it
// cannot (-1), and its submissions still run in order.
addToLibrary({
  $omuiGpu__deps: ['$addRunDependency', '$removeRunDependency', '$UTF8ToString'],
  $omuiGpu__postset: 'omuiGpu.start();',
  $omuiGpu: {
    // rhi's enumerations, in their order (tests/js checks them against the C++).
    FORMATS: ['rgba8unorm', 'bgra8unorm', 'r8unorm', 'rgba16float', 'r32float', 'r32uint', 'depth32float', 'depth32float-stencil8'],
    BYTES: [4, 4, 1, 8, 4, 4, 4, 8],
    VERTEX_FORMATS: ['float32', 'float32x2', 'float32x3', 'float32x4', 'unorm8x4', 'uint32'],
    BLEND_FACTORS: ['zero', 'one', 'src', 'one-minus-src', 'dst', 'one-minus-dst', 'src-alpha', 'one-minus-src-alpha', 'dst-alpha', 'one-minus-dst-alpha'],
    BLEND_OPS: ['add', 'subtract', 'reverse-subtract', 'min', 'max'],
    TOPOLOGIES: ['triangle-list', 'triangle-strip', 'line-list'],
    COMPARES: ['never', 'less', 'equal', 'less-equal', 'greater', 'not-equal', 'greater-equal', 'always'],
    ADDRESS: ['clamp-to-edge', 'repeat', 'mirror-repeat'],
    FILTERS: ['nearest', 'linear'],
    // TextureUsage and BufferUsage bits.
    TEX: { sampled: 1, color_target: 2, storage_read: 4, compute_read: 8, compute_write: 16, depth_target: 32 },
    BUF: { vertex: 1, index: 2, indirect: 4, storage_read: 8, compute_read: 16, compute_write: 32 },
    LOAD: { clear: 0, load: 1, dont_care: 2 },
    STAGE: { vertex: 0, fragment: 1, compute: 2 },
    // The wire's ops (openmui.rhi:wire).
    OP: {
      upload_buffer: 0, upload_texture: 1, copy_texture: 2, begin_render: 3, end_render: 4, set_pipeline: 5, set_viewport: 6,
      set_scissor: 7, bind_vertex_buffer: 8, bind_index_buffer: 9, bind_texture: 10, bind_storage_texture: 11,
      bind_storage_buffer: 12, draw: 13, draw_indexed: 14, begin_compute: 15, end_compute: 16, set_compute_pipeline: 17,
      dispatch: 18, push_uniforms: 19,
    },
    NONE: 0xffffffff,
    KIND: { buffer: 0, texture: 1, sampler: 2, graphics: 3, compute: 4 },

    adapter: null,
    device: null,
    info: null,           // {name, software, maxTexture, depthStencil}
    why: '',              // why there is no device
    errors: [],           // what WebGPU reported since the program last asked
    lost: '',
    buffers: [],          // by slot: {buffer, size}
    textures: [],         // by slot: {texture, view, format, width, height, samples}
    samplers: [],
    pipelines: [],        // graphics, by slot
    computes: [],
    modules: null,        // WGSL text -> GPUShaderModule
    staging: null,        // a GPUBuffer the uploads are written to, and its size
    stagingSize: 0,
    stagingBytes: null,   // the bytes staged for one submission (grown, kept)
    uniforms: null,
    uniformsSize: 0,
    uniformBytes: null,
    readback: null,
    readbackSize: 0,
    canvas: null,         // {element, context, format, blit, nearest, linear}

    align: function (v, a) { return Math.ceil(v / a) * a; },

    start: function () {
      var G = omuiGpu;
      G.modules = new Map();
      if (typeof navigator === 'undefined' || !navigator.gpu) {
        G.why = 'this page has no WebGPU (navigator.gpu)';
        return;
      }
      addRunDependency('omui-webgpu');
      var done = function () { removeRunDependency('omui-webgpu'); };
      var options = (typeof Module !== 'undefined' && Module['omuiWebGpu']) || {};
      Promise.resolve().then(function () {
        return navigator.gpu.requestAdapter({ powerPreference: options.powerPreference });
      }).then(function (adapter) {
        if (!adapter) {
          G.why = 'the browser offered no WebGPU adapter';
          return null;
        }
        var features = [];
        if (adapter.features.has('depth32float-stencil8')) features.push('depth32float-stencil8');
        var limits = { maxTextureDimension2D: adapter.limits.maxTextureDimension2D };
        return adapter.requestDevice({ requiredFeatures: features, requiredLimits: limits }).then(function (device) {
          G.adapter = adapter;
          G.device = device;
          var i = adapter.info || {};
          var name = [i.vendor, i.architecture, i.device, i.description].filter(function (s) { return s; }).join(' ');
          var software = !!(i.isFallbackAdapter || adapter.isFallbackAdapter) || /swiftshader|llvmpipe|lavapipe|warp|basic render/i.test(name);
          G.info = {
            name: name || 'WebGPU',
            software: software,
            maxTexture: device.limits.maxTextureDimension2D,
            depthStencil: device.features.has('depth32float-stencil8'),
          };
          device.addEventListener('uncapturederror', function (e) { G.errors.push(e.error && e.error.message || String(e)); });
          device.lost.then(function (l) { G.lost = l.message || l.reason || 'the device was lost'; });
        });
      }).catch(function (e) {
        G.why = 'WebGPU: ' + (e && e.message || e);
      }).then(done, done);
    },

    fail: function (e) { omuiGpu.errors.push(e && e.message || String(e)); return 0; },

    module: function (code) {
      var G = omuiGpu;
      var m = G.modules.get(code);
      if (!m) {
        m = G.device.createShaderModule({ code: code });
        G.modules.set(code, m);
      }
      return m;
    },

    textureUsage: function (bits, samples) {
      var G = omuiGpu, T = G.TEX, U = GPUTextureUsage;
      if (samples > 1) return U.RENDER_ATTACHMENT;
      var u = U.COPY_SRC | U.COPY_DST;
      if (bits & (T.sampled | T.storage_read | T.compute_read)) u |= U.TEXTURE_BINDING;
      if (bits & (T.color_target | T.depth_target)) u |= U.RENDER_ATTACHMENT;
      if (bits & T.compute_write) u |= U.STORAGE_BINDING;
      return u;
    },

    bufferUsage: function (bits) {
      var B = omuiGpu.BUF, U = GPUBufferUsage;
      var u = U.COPY_SRC | U.COPY_DST;
      if (bits & B.vertex) u |= U.VERTEX;
      if (bits & B.index) u |= U.INDEX;
      if (bits & B.indirect) u |= U.INDIRECT;
      if (bits & (B.storage_read | B.compute_read | B.compute_write)) u |= U.STORAGE;
      return u;
    },

    // A GPU buffer of at least `size` bytes for `usage`, kept in G[key].
    grown: function (key, size, usage, label) {
      var G = omuiGpu;
      if (G[key] && G[key + 'Size'] >= size) return G[key];
      if (G[key]) G[key].destroy();
      var n = Math.max(G.align(size, 256), G[key + 'Size'] * 2, 4096);
      G[key] = G.device.createBuffer({ size: n, usage: usage, label: label });
      G[key + 'Size'] = n;
      return G[key];
    },

    bytes: function (key, size) {
      var G = omuiGpu;
      if (!G[key] || G[key].length < size) G[key] = new Uint8Array(Math.max(G.align(size, 256), G[key] ? G[key].length * 2 : 4096));
      return G[key];
    },

    // The blend state of rhi's Blend (min and max take factors of one in WebGPU).
    blend: function (w, at) {
      var G = omuiGpu;
      if (!w[at]) return undefined;
      var part = function (src, dst, op) {
        var o = G.BLEND_OPS[op];
        var minmax = o === 'min' || o === 'max';
        return { srcFactor: minmax ? 'one' : G.BLEND_FACTORS[src], dstFactor: minmax ? 'one' : G.BLEND_FACTORS[dst], operation: o };
      };
      return { color: part(w[at + 1], w[at + 2], w[at + 3]), alpha: part(w[at + 4], w[at + 5], w[at + 6]) };
    },

    // The groups a pipeline's stages use, as rhi counts them: [read-only, uniforms] per stage.
    counts: function (w, at) {
      return { samplers: w[at], storageTextures: w[at + 1], storageBuffers: w[at + 2], uniforms: w[at + 3] };
    },

    // A graphics pipeline from its words (openmui.rhi:wire's).
    graphics: function (w, vs, vsEntry, fs, fsEntry, label) {
      var G = omuiGpu;
      var vertex = G.counts(w, 0), fragment = G.counts(w, 4);
      var buffers = [];
      var at = 22, n = w[at++];
      for (var b = 0; b < n; ++b) {
        var stride = w[at++], instance = w[at++], attributes = [], count = w[at++];
        for (var a = 0; a < count; ++a) {
          attributes.push({ shaderLocation: w[at], format: G.VERTEX_FORMATS[w[at + 1]], offset: w[at + 2] });
          at += 3;
        }
        buffers.push({ arrayStride: stride, stepMode: instance ? 'instance' : 'vertex', attributes: attributes });
      }
      var target = { format: G.FORMATS[w[8]] };
      var blend = G.blend(w, 9);
      if (blend) target.blend = blend;
      var desc = {
        label: label,
        layout: 'auto',
        vertex: { module: G.module(vs), entryPoint: vsEntry, buffers: buffers },
        fragment: { module: G.module(fs), entryPoint: fsEntry, targets: [target] },
        primitive: { topology: G.TOPOLOGIES[w[16]] },
        multisample: { count: w[21] },
      };
      if (w[17] !== G.NONE) {
        var test = w[18] !== 0, write = w[19] !== 0;
        desc.depthStencil = { format: G.FORMATS[w[17]], depthWriteEnabled: test && write, depthCompare: test ? G.COMPARES[w[20]] : 'always' };
      }
      var pipeline = G.device.createRenderPipeline(desc);
      // Groups 0 and 1 the vertex stage's, 2 and 3 the fragment stage's: as
      // many as the last one used, the ones between them empty.
      var used = [vertex.samplers + vertex.storageTextures + vertex.storageBuffers, vertex.uniforms,
                  fragment.samplers + fragment.storageTextures + fragment.storageBuffers, fragment.uniforms];
      var groups = 0;
      for (var g = 0; g < 4; ++g) if (used[g]) groups = g + 1;
      return { pipeline: pipeline, vertex: vertex, fragment: fragment, groups: groups, empty: [] };
    },

    compute: function (w, cs, entry, label) {
      var G = omuiGpu;
      var counts = { samplers: w[0], storageTextures: w[1], storageBuffers: w[2], uniforms: w[3], rwTextures: w[4], rwBuffers: w[5] };
      var pipeline = G.device.createComputePipeline({ label: label, layout: 'auto', compute: { module: G.module(cs), entryPoint: entry } });
      var used = [counts.samplers + counts.storageTextures + counts.storageBuffers, counts.rwTextures + counts.rwBuffers, counts.uniforms];
      var groups = 0;
      for (var g = 0; g < 3; ++g) if (used[g]) groups = g + 1;
      return { pipeline: pipeline, counts: counts, groups: groups, empty: [] };
    },

    // A stage's read-only group: sampled texture i and its sampler at 2i and
    // 2i + 1, then the storage textures and buffers.
    readEntries: function (bound, c) {
      var e = [];
      for (var i = 0; i < c.samplers; ++i) {
        e.push({ binding: 2 * i, resource: bound.samplers[i].view });
        e.push({ binding: 2 * i + 1, resource: bound.samplers[i].sampler });
      }
      var at = 2 * c.samplers;
      for (var t = 0; t < c.storageTextures; ++t) e.push({ binding: at + t, resource: bound.storageTextures[t] });
      at += c.storageTextures;
      for (var b = 0; b < c.storageBuffers; ++b) e.push({ binding: at + b, resource: { buffer: bound.storageBuffers[b] } });
      return e;
    },

    uniformEntries: function (pushed, n) {
      var G = omuiGpu, e = [];
      for (var u = 0; u < n; ++u) e.push({ binding: u, resource: { buffer: G.uniforms, offset: pushed[u].offset, size: pushed[u].size } });
      return e;
    },

    group: function (p, g, entries) {
      var G = omuiGpu;
      var layout = p.pipeline.getBindGroupLayout(g);
      if (!entries.length) {
        if (!p.empty[g]) p.empty[g] = G.device.createBindGroup({ layout: layout, entries: [] });
        return p.empty[g];
      }
      return G.device.createBindGroup({ layout: layout, entries: entries });
    },

    // One submission: rhi's wire words, already validated.
    run: function (w) {
      var G = omuiGpu, O = G.OP, device = G.device, NONE = G.NONE;
      var f32 = new Float32Array(w.buffer, w.byteOffset, w.length);
      // What the list uploads and pushes, where it goes in the staged bytes.
      var staged = 0, pushed = 0, i, op, len;
      for (i = 0; i < w.length; i += 1 + len) {
        op = w[i] & 0xff;
        len = w[i] >>> 8;
        if (op === O.upload_buffer) staged += G.align(w[i + 5], 4);
        else if (op === O.upload_texture) {
          var t = G.textures[w[i + 1]];
          staged = G.align(staged, 256) + G.align(w[i + 4] * G.BYTES[t.formatIndex], 256) * w[i + 5];
        } else if (op === O.push_uniforms) pushed += G.align(w[i + 5], 256);
      }
      var stagingBytes = staged ? G.bytes('stagingBytes', staged) : null;
      var uniformBytes = pushed ? G.bytes('uniformBytes', pushed) : null;
      if (staged) G.grown('staging', staged, GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST, 'omui.rhi.staging');
      if (pushed) G.grown('uniforms', pushed, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, 'omui.rhi.uniforms');

      var encoder = device.createCommandEncoder();
      var pass = null, compute = false, target = null, pipeline = null;
      // What each stage has bound in this pass, and its uniforms (pushed
      // once, they hold from there on, through later passes).
      var stage = function () { return { samplers: [], storageTextures: [], storageBuffers: [] }; };
      var bound = [stage(), stage(), stage()];
      var uniforms = [[], [], []];
      var dirty = [true, true, true, true];
      var rwTextures = [], rwBuffers = [];
      var at = 0, pushAt = 0;
      var payload = function (k) { return HEAPU8.subarray(w[k], w[k] + w[k + 2]); };
      var flush = function () {
        var g, entries;
        if (compute) {
          var c = pipeline.counts;
          for (g = 0; g < pipeline.groups; ++g) {
            if (!dirty[g]) continue;
            if (g === 0) entries = G.readEntries(bound[2], c);
            else if (g === 1) {
              entries = [];
              for (var t = 0; t < c.rwTextures; ++t) entries.push({ binding: t, resource: rwTextures[t] });
              for (var b = 0; b < c.rwBuffers; ++b) entries.push({ binding: c.rwTextures + b, resource: { buffer: rwBuffers[b] } });
            } else entries = G.uniformEntries(uniforms[2], c.uniforms);
            pass.setBindGroup(g, G.group(pipeline, g, entries));
            dirty[g] = false;
          }
          return;
        }
        for (g = 0; g < pipeline.groups; ++g) {
          if (!dirty[g]) continue;
          var s = g < 2 ? 0 : 1, counts = s === 0 ? pipeline.vertex : pipeline.fragment;
          entries = g % 2 === 0 ? G.readEntries(bound[s], counts) : G.uniformEntries(uniforms[s], counts.uniforms);
          pass.setBindGroup(g, G.group(pipeline, g, entries));
          dirty[g] = false;
        }
      };
      var everything = function () { dirty = [true, true, true, true]; };

      for (i = 0; i < w.length; i += 1 + len) {
        op = w[i] & 0xff;
        len = w[i] >>> 8;
        var k = i + 1;
        switch (op) {
          case O.upload_buffer: {
            var size = w[k + 4], b = G.buffers[w[k]];
            stagingBytes.set(payload(k + 2), at);
            // A length not a multiple of 4 ends at the buffer's end (page.cpp
            // refuses the others): the GPU buffer's size was rounded up.
            encoder.copyBufferToBuffer(G.staging, at, b.buffer, w[k + 1], G.align(size, 4));
            at += G.align(size, 4);
            break;
          }
          case O.upload_texture: {
            var tex = G.textures[w[k]], x = w[k + 1], y = w[k + 2], width = w[k + 3], height = w[k + 4];
            var row = width * G.BYTES[tex.formatIndex], pitch = G.align(row, 256), from = w[k + 8] || row;
            var data = HEAPU8.subarray(w[k + 5], w[k + 5] + w[k + 7]);
            at = G.align(at, 256);
            for (var r = 0; r < height; ++r) stagingBytes.set(data.subarray(r * from, r * from + row), at + r * pitch);
            encoder.copyBufferToTexture({ buffer: G.staging, offset: at, bytesPerRow: pitch, rowsPerImage: height },
                                        { texture: tex.texture, origin: { x: x, y: y } }, { width: width, height: height });
            at += pitch * height;
            break;
          }
          case O.copy_texture:
            encoder.copyTextureToTexture({ texture: G.textures[w[k]].texture, origin: { x: w[k + 1], y: w[k + 2] } },
                                         { texture: G.textures[w[k + 5]].texture, origin: { x: w[k + 6], y: w[k + 7] } },
                                         { width: w[k + 3], height: w[k + 4] });
            break;
          case O.begin_render: {
            target = G.textures[w[k]];
            var color = {
              view: target.view,
              loadOp: w[k + 1] === G.LOAD.load ? 'load' : 'clear',
              storeOp: w[k + 6] !== NONE ? 'discard' : 'store',
              clearValue: { r: f32[k + 2], g: f32[k + 3], b: f32[k + 4], a: f32[k + 5] },
            };
            if (w[k + 6] !== NONE) color.resolveTarget = G.textures[w[k + 6]].view;
            var desc = { colorAttachments: [color] };
            if (w[k + 7] !== NONE) {
              var z = G.textures[w[k + 7]], zload = w[k + 8] === G.LOAD.load ? 'load' : 'clear', zstore = w[k + 11] ? 'store' : 'discard';
              var ds = { view: z.view, depthLoadOp: zload, depthStoreOp: zstore, depthClearValue: f32[k + 9] };
              if (z.format === 'depth32float-stencil8') {
                ds.stencilLoadOp = zload;
                ds.stencilStoreOp = zstore;
                ds.stencilClearValue = w[k + 10];
              }
              desc.depthStencilAttachment = ds;
            }
            pass = encoder.beginRenderPass(desc);
            compute = false;
            pipeline = null;
            bound[0] = stage();
            bound[1] = stage();
            break;
          }
          case O.end_render:
          case O.end_compute:
            pass.end();
            pass = null;
            break;
          case O.set_pipeline:
            pipeline = G.pipelines[w[k]];
            pass.setPipeline(pipeline.pipeline);
            everything();
            break;
          case O.set_viewport:
            pass.setViewport(f32[k], f32[k + 1], f32[k + 2], f32[k + 3], 0, 1);
            break;
          case O.set_scissor: {
            var sx = w[k], sy = w[k + 1];
            pass.setScissorRect(sx, sy, Math.min(w[k + 2], target.width - sx), Math.min(w[k + 3], target.height - sy));
            break;
          }
          case O.bind_vertex_buffer:
            pass.setVertexBuffer(w[k], G.buffers[w[k + 1]].buffer, w[k + 2]);
            break;
          case O.bind_index_buffer:
            pass.setIndexBuffer(G.buffers[w[k]].buffer, w[k + 2] ? 'uint32' : 'uint16', w[k + 1]);
            break;
          case O.bind_texture:
            bound[w[k]].samplers[w[k + 1]] = { view: G.textures[w[k + 2]].view, sampler: G.samplers[w[k + 3]] };
            dirty[w[k] === G.STAGE.fragment ? 2 : 0] = true;
            break;
          case O.bind_storage_texture:
            bound[w[k]].storageTextures[w[k + 1]] = G.textures[w[k + 2]].view;
            dirty[w[k] === G.STAGE.fragment ? 2 : 0] = true;
            break;
          case O.bind_storage_buffer:
            bound[w[k]].storageBuffers[w[k + 1]] = G.buffers[w[k + 2]].buffer;
            dirty[w[k] === G.STAGE.fragment ? 2 : 0] = true;
            break;
          case O.draw:
            flush();
            pass.draw(w[k], w[k + 1], w[k + 2], w[k + 3]);
            break;
          case O.draw_indexed:
            flush();
            pass.drawIndexed(w[k], w[k + 1], w[k + 2], w[k + 3] | 0, w[k + 4]);
            break;
          case O.begin_compute: {
            var nt = w[k];
            rwTextures = [];
            rwBuffers = [];
            for (var a = 0; a < nt; ++a) rwTextures.push(G.textures[w[k + 1 + a]].view);
            var nb = w[k + 1 + nt];
            for (var c2 = 0; c2 < nb; ++c2) rwBuffers.push(G.buffers[w[k + 2 + nt + c2]].buffer);
            pass = encoder.beginComputePass();
            compute = true;
            pipeline = null;
            bound[2] = stage();
            break;
          }
          case O.set_compute_pipeline:
            pipeline = G.computes[w[k]];
            pass.setPipeline(pipeline.pipeline);
            everything();
            break;
          case O.dispatch:
            flush();
            pass.dispatchWorkgroups(w[k], w[k + 1], w[k + 2]);
            break;
          case O.push_uniforms: {
            var st = w[k], n = w[k + 4];
            uniformBytes.set(payload(k + 2), pushAt);
            uniforms[st][w[k + 1]] = { offset: pushAt, size: G.align(n, 256) };
            pushAt += G.align(n, 256);
            dirty[st === G.STAGE.vertex ? 1 : st === G.STAGE.fragment ? 3 : 2] = true;
            break;
          }
        }
      }
      if (staged) device.queue.writeBuffer(G.staging, 0, stagingBytes, 0, G.align(at, 4));
      if (pushed) device.queue.writeBuffer(G.uniforms, 0, uniformBytes, 0, pushAt);
      device.queue.submit([encoder.finish()]);
      return 1;
    },

    // The frame onto the canvas: drawn over it (formats and sizes may differ).
    present: function (id) {
      var G = omuiGpu, C = G.canvas, device = G.device;
      var el = C.element;
      if (!el.width || !el.height) return 0;
      if (!C.blit) {
        var code =
          '@group(0) @binding(0) var frame : texture_2d<f32>;\n' +
          '@group(0) @binding(1) var smp : sampler;\n' +
          'struct V { @builtin(position) pos : vec4<f32>, @location(0) uv : vec2<f32> };\n' +
          '@vertex fn omui_blit_vs(@builtin(vertex_index) i : u32) -> V {\n' +
          '  let p = vec2<f32>(f32((i << 1u) & 2u), f32(i & 2u));\n' +
          '  var o : V;\n  o.pos = vec4<f32>(p * vec2<f32>(2.0, -2.0) + vec2<f32>(-1.0, 1.0), 0.0, 1.0);\n  o.uv = p;\n  return o;\n}\n' +
          '@fragment fn omui_blit_fs(v : V) -> @location(0) vec4<f32> { return textureSample(frame, smp, v.uv); }\n';
        var m = G.module(code);
        C.blit = device.createRenderPipeline({
          label: 'omui.rhi.present', layout: 'auto',
          vertex: { module: m, entryPoint: 'omui_blit_vs' },
          fragment: { module: m, entryPoint: 'omui_blit_fs', targets: [{ format: C.format }] },
          primitive: { topology: 'triangle-list' },
        });
        C.nearest = device.createSampler({ minFilter: 'nearest', magFilter: 'nearest' });
        C.linear = device.createSampler({ minFilter: 'linear', magFilter: 'linear' });
      }
      var frame = G.textures[id];
      var same = frame.width === el.width && frame.height === el.height;
      var bind = device.createBindGroup({
        layout: C.blit.getBindGroupLayout(0),
        entries: [{ binding: 0, resource: frame.view }, { binding: 1, resource: same ? C.nearest : C.linear }],
      });
      var encoder = device.createCommandEncoder();
      var pass = encoder.beginRenderPass({
        colorAttachments: [{ view: C.context.getCurrentTexture().createView(), loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 0 } }],
      });
      pass.setPipeline(C.blit);
      pass.setBindGroup(0, bind);
      pass.draw(3);
      pass.end();
      device.queue.submit([encoder.finish()]);
      return 1;
    },
  },

  // 1 and the adapter's [software, largest texture, depth32float-stencil8]
  // in `out` (3 words), or 0: no device (omui_webgpu_why says why).
  omui_webgpu_device__deps: ['$omuiGpu'],
  omui_webgpu_device: function (out) {
    var G = omuiGpu;
    if (!G.device) return 0;
    HEAPU32[out >> 2] = G.info.software ? 1 : 0;
    HEAPU32[(out >> 2) + 1] = G.info.maxTexture;
    HEAPU32[(out >> 2) + 2] = G.info.depthStencil ? 1 : 0;
    return 1;
  },

  omui_webgpu_name__deps: ['$omuiGpu', '$stringToNewUTF8'],
  omui_webgpu_name: function () { return stringToNewUTF8(omuiGpu.info ? omuiGpu.info.name : ''); },

  omui_webgpu_why__deps: ['$omuiGpu', '$stringToNewUTF8'],
  omui_webgpu_why: function () { return stringToNewUTF8(omuiGpu.why || 'no WebGPU device'); },

  // Whether this program was linked to suspend for WebGPU's promises.
  omui_webgpu_suspends: function () {
#if ASYNCIFY
    return 1;
#else
    return 0;
#endif
  },

  omui_webgpu_buffer__deps: ['$omuiGpu', '$UTF8ToString'],
  omui_webgpu_buffer: function (id, size, usage, label) {
    var G = omuiGpu;
    try {
      G.buffers[id] = { buffer: G.device.createBuffer({ size: G.align(size, 4), usage: G.bufferUsage(usage), label: UTF8ToString(label) }), size: size };
      return 1;
    } catch (e) { return G.fail(e); }
  },

  omui_webgpu_texture__deps: ['$omuiGpu', '$UTF8ToString'],
  omui_webgpu_texture: function (id, format, width, height, usage, samples, label) {
    var G = omuiGpu;
    try {
      var f = G.FORMATS[format];
      var texture = G.device.createTexture({
        size: { width: width, height: height }, format: f, usage: G.textureUsage(usage, samples), sampleCount: samples, label: UTF8ToString(label),
      });
      G.textures[id] = { texture: texture, view: texture.createView(), format: f, formatIndex: format, width: width, height: height, samples: samples };
      return 1;
    } catch (e) { return G.fail(e); }
  },

  omui_webgpu_sampler__deps: ['$omuiGpu'],
  omui_webgpu_sampler: function (id, min, mag, u, v) {
    var G = omuiGpu;
    try {
      G.samplers[id] = G.device.createSampler({
        minFilter: G.FILTERS[min], magFilter: G.FILTERS[mag], mipmapFilter: 'nearest', addressModeU: G.ADDRESS[u], addressModeV: G.ADDRESS[v],
      });
      return 1;
    } catch (e) { return G.fail(e); }
  },

  omui_webgpu_graphics__deps: ['$omuiGpu', '$UTF8ToString'],
  omui_webgpu_graphics: function (id, words, count, vs, vsLength, vsEntry, fs, fsLength, fsEntry, label) {
    var G = omuiGpu;
    try {
      var w = HEAPU32.slice(words >> 2, (words >> 2) + count);
      G.pipelines[id] = G.graphics(w, UTF8ToString(vs, vsLength), UTF8ToString(vsEntry), UTF8ToString(fs, fsLength), UTF8ToString(fsEntry),
                                   UTF8ToString(label));
      return 1;
    } catch (e) { return G.fail(e); }
  },

  omui_webgpu_compute__deps: ['$omuiGpu', '$UTF8ToString'],
  omui_webgpu_compute: function (id, words, count, cs, csLength, entry, label) {
    var G = omuiGpu;
    try {
      var w = HEAPU32.slice(words >> 2, (words >> 2) + count);
      G.computes[id] = G.compute(w, UTF8ToString(cs, csLength), UTF8ToString(entry), UTF8ToString(label));
      return 1;
    } catch (e) { return G.fail(e); }
  },

  omui_webgpu_destroy__deps: ['$omuiGpu'],
  omui_webgpu_destroy: function (kind, id) {
    var G = omuiGpu, K = G.KIND;
    if (kind === K.buffer && G.buffers[id]) { G.buffers[id].buffer.destroy(); G.buffers[id] = null; }
    else if (kind === K.texture && G.textures[id]) { G.textures[id].texture.destroy(); G.textures[id] = null; }
    else if (kind === K.sampler) G.samplers[id] = null;
    else if (kind === K.graphics) G.pipelines[id] = null;
    else if (kind === K.compute) G.computes[id] = null;
  },

  omui_webgpu_run__deps: ['$omuiGpu'],
  omui_webgpu_run: function (words, count) {
    var G = omuiGpu;
    try {
      return G.run(HEAPU32.subarray(words >> 2, (words >> 2) + count));
    } catch (e) { return G.fail(e); }
  },

  // What WebGPU reported since the last call (one message), or 0.
  omui_webgpu_error__deps: ['$omuiGpu', '$stringToNewUTF8'],
  omui_webgpu_error: function () {
    var G = omuiGpu;
    if (G.lost) return stringToNewUTF8('the device was lost: ' + G.lost);
    if (!G.errors.length) return 0;
    var e = G.errors.join('; ');
    G.errors = [];
    return stringToNewUTF8(e);
  },

  omui_webgpu_lost__deps: ['$omuiGpu'],
  omui_webgpu_lost: function () { return omuiGpu.lost ? 1 : 0; },

  // 1 once every submission so far has run; -1: this program cannot wait.
  omui_webgpu_wait__deps: ['$omuiGpu'],
  omui_webgpu_wait__async: 'auto',
  omui_webgpu_wait: function () {
#if ASYNCIFY
    return omuiGpu.device.queue.onSubmittedWorkDone().then(function () { return omuiGpu.lost ? 0 : 1; }, function (e) { return omuiGpu.fail(e); });
#else
    return -1;
#endif
  },

  // The texels of a region, rows packed tightly, into `out`: 1, 0 (failed:
  // omui_webgpu_error says why), or -1 (this program cannot wait for them).
  omui_webgpu_read_texture__deps: ['$omuiGpu'],
  omui_webgpu_read_texture__async: 'auto',
  omui_webgpu_read_texture: function (id, x, y, width, height, out) {
#if ASYNCIFY
    var G = omuiGpu, t = G.textures[id];
    var row = width * G.BYTES[t.formatIndex], pitch = G.align(row, 256), size = pitch * height;
    var buffer;
    try {
      if (G.readbackSize < size || !G.readback) {
        if (G.readback) G.readback.destroy();
        G.readbackSize = G.align(size, 4096);
        G.readback = G.device.createBuffer({ size: G.readbackSize, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ, label: 'omui.rhi.readback' });
      }
      buffer = G.readback;
      var encoder = G.device.createCommandEncoder();
      var source = { texture: t.texture, origin: { x: x, y: y } };
      if (t.format === 'depth32float') source.aspect = 'depth-only';
      encoder.copyTextureToBuffer(source, { buffer: buffer, bytesPerRow: pitch, rowsPerImage: height }, { width: width, height: height });
      G.device.queue.submit([encoder.finish()]);
    } catch (e) { return G.fail(e); }
    return buffer.mapAsync(GPUMapMode.READ, 0, size).then(function () {
      var src = new Uint8Array(buffer.getMappedRange(0, size));
      for (var r = 0; r < height; ++r) HEAPU8.set(src.subarray(r * pitch, r * pitch + row), out + r * row);
      buffer.unmap();
      return 1;
    }, function (e) { return G.fail(e); });
#else
    return -1;
#endif
  },

  // `length` bytes of a buffer from `offset`, into `out`; as above.
  omui_webgpu_read_buffer__deps: ['$omuiGpu'],
  omui_webgpu_read_buffer__async: 'auto',
  omui_webgpu_read_buffer: function (id, offset, length, out) {
#if ASYNCIFY
    var G = omuiGpu, b = G.buffers[id];
    var from = offset - offset % 4, size = G.align(offset + length, 4) - from;
    var buffer;
    try {
      buffer = G.device.createBuffer({ size: size, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ, label: 'omui.rhi.readback' });
      var encoder = G.device.createCommandEncoder();
      encoder.copyBufferToBuffer(b.buffer, from, buffer, 0, size);
      G.device.queue.submit([encoder.finish()]);
    } catch (e) { return G.fail(e); }
    return buffer.mapAsync(GPUMapMode.READ).then(function () {
      HEAPU8.set(new Uint8Array(buffer.getMappedRange()).subarray(offset - from, offset - from + length), out);
      buffer.unmap();
      buffer.destroy();
      return 1;
    }, function (e) { buffer.destroy(); return G.fail(e); });
#else
    return -1;
#endif
  },

  // The canvas present() shows frames on: configured for the device.
  omui_webgpu_canvas__deps: ['$omuiGpu', '$UTF8ToString'],
  omui_webgpu_canvas: function (selector) {
    var G = omuiGpu;
    try {
      var s = UTF8ToString(selector);
      var element = (typeof Module !== 'undefined' && Module['canvas']) || document.querySelector(s);
      if (!element) return G.fail('no canvas matches ' + s);
      var context = element.getContext('webgpu');
      if (!context) return G.fail('the canvas ' + s + ' has another kind of context');
      var format = navigator.gpu.getPreferredCanvasFormat();
      context.configure({ device: G.device, format: format, alphaMode: 'premultiplied' });
      G.canvas = { element: element, context: context, format: format, blit: null };
      return 1;
    } catch (e) { return G.fail(e); }
  },

  // 1 shown, 0 nothing to show (a canvas of no size), -1 failed.
  omui_webgpu_present__deps: ['$omuiGpu'],
  omui_webgpu_present: function (id) {
    var G = omuiGpu;
    try {
      return G.present(id);
    } catch (e) { G.fail(e); return -1; }
  },
});
