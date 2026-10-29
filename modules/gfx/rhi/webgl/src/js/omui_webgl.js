// openmui.rhi.webgl's half in the page: omui.rhi on the browser's WebGL2,
// the compatibility path. An Emscripten library (--js-library, build.mcpp):
// each omui_webgl_* function is linked into the program's JavaScript by
// name, and src/native/browser/page.cpp calls it.
//
// The context is made when the device is (the canvas present() shows
// frames on, or one of the page's own that nobody sees); everything in
// WebGL2 is synchronous, readback too. Resources are created one call each,
// by the slot index the C++ side gave them; a submission crosses in one
// call, as rhi's wire words (openmui.rhi:wire), and becomes GL calls in its
// order. The state rhi keeps per pass and pipeline is WebGL's to keep here:
// a framebuffer per (target, depth), cleared as the pass begins with the
// scissor off; a pipeline's program, blend and depth state set with it;
// vertex attributes pointed at the bound buffers when a draw finds them
// changed (a draw's vertex offset and first instance moved into the
// pointers: WebGL2 has neither), textures and samplers on units (a
// fragment stage's slot i on unit i, a vertex stage's on 16 + i), uniforms
// in one buffer at the context's offset alignment, bound by range (a vertex
// stage's slot i at point i, a fragment stage's at 4 + i).
//
// rhi's textures are top-down — row 0 the top, as on every other backend —
// and GL's bottom-up: openmui.shader's ESSL flips y at the vertex stage's
// end, so GL's row 0 is the top too, and only present() flips (the canvas
// is GL's). bgra8 is kept as RGBA8 (WebGL2 has no BGRA), its bytes swapped
// where they cross: uploads and readback.
addToLibrary({
  $omuiGl__deps: ['$UTF8ToString'],
  $omuiGl: {
    // rhi's Format: internal format, format, type, bytes a texel, kind, R/B swapped.
    FORMATS: [
      ['RGBA8', 'RGBA', 'UNSIGNED_BYTE', 4, 'unorm', false],
      ['RGBA8', 'RGBA', 'UNSIGNED_BYTE', 4, 'unorm', true],   // bgra8_unorm
      ['R8', 'RED', 'UNSIGNED_BYTE', 1, 'unorm', false],
      ['RGBA16F', 'RGBA', 'HALF_FLOAT', 8, 'float', false],
      ['R32F', 'RED', 'FLOAT', 4, 'float', false],
      ['R32UI', 'RED_INTEGER', 'UNSIGNED_INT', 4, 'uint', false],
      ['DEPTH_COMPONENT32F', 'DEPTH_COMPONENT', 'FLOAT', 4, 'depth', false],
      ['DEPTH32F_STENCIL8', 'DEPTH_STENCIL', 'FLOAT_32_UNSIGNED_INT_24_8_REV', 8, 'depth', false],
    ],
    // rhi's VertexFormat: components, type, normalised, integer.
    VERTEX_FORMATS: [[1, 'FLOAT', false, false], [2, 'FLOAT', false, false], [3, 'FLOAT', false, false], [4, 'FLOAT', false, false],
                     [4, 'UNSIGNED_BYTE', true, false], [1, 'UNSIGNED_INT', false, true]],
    BLEND_FACTORS: ['ZERO', 'ONE', 'SRC_COLOR', 'ONE_MINUS_SRC_COLOR', 'DST_COLOR', 'ONE_MINUS_DST_COLOR', 'SRC_ALPHA', 'ONE_MINUS_SRC_ALPHA',
                    'DST_ALPHA', 'ONE_MINUS_DST_ALPHA'],
    BLEND_OPS: ['FUNC_ADD', 'FUNC_SUBTRACT', 'FUNC_REVERSE_SUBTRACT', 'MIN', 'MAX'],
    TOPOLOGIES: ['TRIANGLES', 'TRIANGLE_STRIP', 'LINES'],
    COMPARES: ['NEVER', 'LESS', 'EQUAL', 'LEQUAL', 'GREATER', 'NOTEQUAL', 'GEQUAL', 'ALWAYS'],
    ADDRESS: ['CLAMP_TO_EDGE', 'REPEAT', 'MIRRORED_REPEAT'],
    FILTERS: ['NEAREST', 'LINEAR'],
    TEX: { sampled: 1, color_target: 2, storage_read: 4, compute_read: 8, compute_write: 16, depth_target: 32 },
    BUF: { vertex: 1, index: 2, indirect: 4, storage_read: 8, compute_read: 16, compute_write: 32 },
    LOAD: { clear: 0, load: 1, dont_care: 2 },
    STAGE: { vertex: 0, fragment: 1, compute: 2 },
    OP: {
      upload_buffer: 0, upload_texture: 1, copy_texture: 2, begin_render: 3, end_render: 4, set_pipeline: 5, set_viewport: 6,
      set_scissor: 7, bind_vertex_buffer: 8, bind_index_buffer: 9, bind_texture: 10, bind_storage_texture: 11,
      bind_storage_buffer: 12, draw: 13, draw_indexed: 14, begin_compute: 15, end_compute: 16, set_compute_pipeline: 17,
      dispatch: 18, push_uniforms: 19,
    },
    NONE: 0xffffffff,
    KIND: { buffer: 0, texture: 1, sampler: 2, graphics: 3, compute: 4 },
    VERTEX_UNITS: 16,     // a vertex stage's sampler slot i is on unit 16 + i
    FRAGMENT_POINTS: 4,   // a fragment stage's uniform slot i is at point 4 + i

    gl: null,
    canvas: null,
    shown: false,         // the context is the canvas present() shows on
    info: null,           // {name, software, maxTexture, maxSamples, colorFloat}
    why: '',
    errors: [],
    lost: '',
    buffers: [],          // by slot: {buffer, size, index}
    textures: [],         // by slot: {texture | renderbuffer, f (FORMATS entry), width, height, samples}
    samplers: [],
    pipelines: [],
    framebuffers: null,   // "color:depth" -> WebGLFramebuffer
    vao: null,
    ubo: null,
    uboSize: 0,
    uboBytes: null,
    uboAlign: 256,
    blockSlack: 16384,

    align: function (v, a) { return Math.ceil(v / a) * a; },
    fail: function (e) { omuiGl.errors.push(e && e.message || String(e)); return 0; },
    c: function (name) { return omuiGl.gl[name]; },

    open: function (selector) {
      var G = omuiGl;
      G.framebuffers = new Map();
      var canvas = null;
      if (selector) {
        canvas = (typeof Module !== 'undefined' && Module['canvas']) || (typeof document !== 'undefined' && document.querySelector(selector));
        if (!canvas) { G.why = 'no canvas matches ' + selector; return 0; }
      } else if (typeof document !== 'undefined' && document.createElement) {
        canvas = document.createElement('canvas');
      } else if (typeof OffscreenCanvas !== 'undefined') {
        canvas = new OffscreenCanvas(1, 1);
      } else {
        G.why = 'this page has no canvas for WebGL2';
        return 0;
      }
      var gl = canvas.getContext && canvas.getContext('webgl2', {
        alpha: true, premultipliedAlpha: true, antialias: false, depth: false, stencil: false, preserveDrawingBuffer: false,
      });
      if (!gl) { G.why = 'this page has no WebGL2'; return 0; }
      G.gl = gl;
      G.canvas = canvas;
      G.shown = !!selector;
      var colorFloat = !!gl.getExtension('EXT_color_buffer_float');
      var halfFloat = colorFloat || !!gl.getExtension('EXT_color_buffer_half_float');
      gl.getExtension('EXT_float_blend');
      gl.getExtension('OES_texture_float_linear');
      var debug = gl.getExtension('WEBGL_debug_renderer_info');
      var name = (debug && gl.getParameter(debug.UNMASKED_RENDERER_WEBGL)) || gl.getParameter(gl.RENDERER) || 'WebGL2';
      G.info = {
        name: String(name),
        software: /swiftshader|llvmpipe|lavapipe|warp|basic render|software/i.test(String(name)),
        maxTexture: gl.getParameter(gl.MAX_TEXTURE_SIZE),
        maxSamples: gl.getParameter(gl.MAX_SAMPLES),
        colorFloat: colorFloat,
        halfFloat: halfFloat,
      };
      G.uboAlign = gl.getParameter(gl.UNIFORM_BUFFER_OFFSET_ALIGNMENT) || 256;
      G.blockSlack = gl.getParameter(gl.MAX_UNIFORM_BLOCK_SIZE) || 16384;
      G.vao = gl.createVertexArray();
      gl.bindVertexArray(G.vao);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
      gl.pixelStorei(gl.PACK_ALIGNMENT, 1);
      if (canvas.addEventListener) {
        canvas.addEventListener('webglcontextlost', function (e) { G.lost = 'the WebGL2 context was lost'; if (e.preventDefault) e.preventDefault(); });
      }
      return 1;
    },

    close: function () {
      var G = omuiGl, gl = G.gl;
      if (!gl) return;
      G.framebuffers.forEach(function (f) { gl.deleteFramebuffer(f); });
      if (G.ubo) gl.deleteBuffer(G.ubo);
      gl.deleteVertexArray(G.vao);
      G.gl = null; G.canvas = null; G.ubo = null; G.uboSize = 0; G.framebuffers = null;
      G.buffers = []; G.textures = []; G.samplers = []; G.pipelines = []; G.errors = []; G.lost = '';
    },

    // What GL says went wrong since the last look, as an error of `what`.
    check: function (what) {
      var gl = omuiGl.gl, e = gl.getError();
      if (e === gl.NO_ERROR) return true;
      var names = { 0x0500: 'INVALID_ENUM', 0x0501: 'INVALID_VALUE', 0x0502: 'INVALID_OPERATION', 0x0505: 'OUT_OF_MEMORY',
                    0x0506: 'INVALID_FRAMEBUFFER_OPERATION', 0x9242: 'CONTEXT_LOST_WEBGL' };
      omuiGl.fail('GL ' + (names[e] || '0x' + e.toString(16)) + ' in ' + what);
      while (gl.getError() !== gl.NO_ERROR) { /* the rest of the same */ }
      return false;
    },

    // The framebuffer of a colour target and a depth target (ids; NONE:
    // none). Making one binds it (to FRAMEBUFFER, read and draw): a caller
    // that binds two gets both first.
    framebuffer: function (color, depth) {
      var G = omuiGl, gl = G.gl, key = color + ':' + depth;
      var f = G.framebuffers.get(key);
      if (f) return f;
      f = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, f);
      var attach = function (point, t) {
        if (t.renderbuffer) gl.framebufferRenderbuffer(gl.FRAMEBUFFER, point, gl.RENDERBUFFER, t.renderbuffer);
        else gl.framebufferTexture2D(gl.FRAMEBUFFER, point, gl.TEXTURE_2D, t.texture, 0);
      };
      if (color !== G.NONE) attach(gl.COLOR_ATTACHMENT0, G.textures[color]);
      if (depth !== G.NONE) {
        var z = G.textures[depth];
        attach(z.f[1] === 'DEPTH_STENCIL' ? gl.DEPTH_STENCIL_ATTACHMENT : gl.DEPTH_ATTACHMENT, z);
      }
      var status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
      if (status !== gl.FRAMEBUFFER_COMPLETE) {
        gl.deleteFramebuffer(f);
        throw new Error('the framebuffer of ' + key + ' is incomplete (0x' + status.toString(16) + ')');
      }
      G.framebuffers.set(key, f);
      return f;
    },

    forget: function (id) {
      var G = omuiGl;
      G.framebuffers.forEach(function (f, key) {
        var parts = key.split(':');
        if (Number(parts[0]) === id || Number(parts[1]) === id) { G.gl.deleteFramebuffer(f); G.framebuffers.delete(key); }
      });
    },

    shader: function (type, code, label) {
      var gl = omuiGl.gl, s = gl.createShader(type);
      gl.shaderSource(s, code);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
        var log = gl.getShaderInfoLog(s);
        gl.deleteShader(s);
        throw new Error('compiling ' + label + ': ' + log);
      }
      return s;
    },

    // A graphics pipeline from its words (page.cpp: graphics_words — the
    // same as rhi.webgpu's) and its ESSL.
    graphics: function (w, vs, fs, label) {
      var G = omuiGl, gl = G.gl;
      var v = G.shader(gl.VERTEX_SHADER, vs, label + ' (vertex)');
      var f = G.shader(gl.FRAGMENT_SHADER, fs, label + ' (fragment)');
      var program = gl.createProgram();
      gl.attachShader(program, v);
      gl.attachShader(program, f);
      gl.linkProgram(program);
      gl.deleteShader(v);
      gl.deleteShader(f);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
        var log = gl.getProgramInfoLog(program);
        gl.deleteProgram(program);
        throw new Error('linking ' + label + ': ' + log);
      }
      var counts = function (at) { return { samplers: w[at], storageTextures: w[at + 1], storageBuffers: w[at + 2], uniforms: w[at + 3] }; };
      var p = { program: program, vertex: counts(0), fragment: counts(4), blocks: [[], []], buffers: [] };
      gl.useProgram(program);
      var stages = [['vs', p.vertex, G.VERTEX_UNITS, 0], ['fs', p.fragment, 0, G.FRAGMENT_POINTS]];
      for (var s = 0; s < 2; ++s) {
        var st = stages[s];
        for (var i = 0; i < st[1].samplers; ++i) {
          var loc = gl.getUniformLocation(program, 'omui_' + st[0] + '_sampler' + i);
          if (loc) gl.uniform1i(loc, st[2] + i);
        }
        for (var u = 0; u < st[1].uniforms; ++u) {
          var index = gl.getUniformBlockIndex(program, 'omui_' + st[0] + '_uniform' + u);
          if (index === gl.INVALID_INDEX) { p.blocks[s].push(0); continue; }
          gl.uniformBlockBinding(program, index, st[3] + u);
          p.blocks[s].push(gl.getActiveUniformBlockParameter(program, index, gl.UNIFORM_BLOCK_DATA_SIZE));
        }
      }
      p.base = gl.getUniformLocation(program, 'omui_base');
      p.blend = w[9] ? {
        src: gl[G.BLEND_FACTORS[w[10]]], dst: gl[G.BLEND_FACTORS[w[11]]], op: gl[G.BLEND_OPS[w[12]]],
        srcAlpha: gl[G.BLEND_FACTORS[w[13]]], dstAlpha: gl[G.BLEND_FACTORS[w[14]]], opAlpha: gl[G.BLEND_OPS[w[15]]],
      } : null;
      p.mode = gl[G.TOPOLOGIES[w[16]]];
      p.depth = w[17] !== G.NONE ? { test: w[18] !== 0, write: w[18] !== 0 && w[19] !== 0, func: gl[G.COMPARES[w[20]]] } : null;
      var at = 22, n = w[at++];
      for (var b = 0; b < n; ++b) {
        var layout = { stride: w[at++], instance: w[at++] !== 0, attributes: [] }, count = w[at++];
        for (var a = 0; a < count; ++a) {
          var vf = G.VERTEX_FORMATS[w[at + 1]];
          layout.attributes.push({ location: w[at], size: vf[0], type: gl[vf[1]], normalized: vf[2], integer: vf[3], offset: w[at + 2] });
          at += 3;
        }
        p.buffers.push(layout);
      }
      return p;
    },

    // One submission: rhi's wire words, already validated.
    run: function (w) {
      var G = omuiGl, gl = G.gl, O = G.OP, NONE = G.NONE;
      var f32 = new Float32Array(w.buffer, w.byteOffset, w.length);
      var i, op, len;
      // The uniforms the list pushes, written once at their offsets.
      var pushed = 0;
      for (i = 0; i < w.length; i += 1 + len) {
        op = w[i] & 0xff;
        len = w[i] >>> 8;
        if (op === O.push_uniforms) pushed += G.align(w[i + 5], G.uboAlign);
      }
      if (pushed) {
        if (!G.ubo || G.uboSize < pushed + G.blockSlack) {
          if (G.ubo) gl.deleteBuffer(G.ubo);
          G.uboSize = Math.max(G.align(pushed + G.blockSlack, 4096), G.uboSize * 2);
          G.ubo = gl.createBuffer();
          gl.bindBuffer(gl.UNIFORM_BUFFER, G.ubo);
          gl.bufferData(gl.UNIFORM_BUFFER, G.uboSize, gl.DYNAMIC_DRAW);
        }
        if (!G.uboBytes || G.uboBytes.length < pushed) G.uboBytes = new Uint8Array(G.align(pushed, 4096));
        var at0 = 0;
        for (i = 0; i < w.length; i += 1 + len) {
          op = w[i] & 0xff;
          len = w[i] >>> 8;
          if (op === O.push_uniforms) {
            G.uboBytes.set(HEAPU8.subarray(w[i + 3], w[i + 3] + w[i + 5]), at0);
            at0 += G.align(w[i + 5], G.uboAlign);
          }
        }
        gl.bindBuffer(gl.UNIFORM_BUFFER, G.ubo);
        gl.bufferSubData(gl.UNIFORM_BUFFER, 0, G.uboBytes, 0, pushed);
      }

      var target = null, resolve = null, pipeline = null, indexWide = false, indexOffset = 0, pushAt = 0;
      var vertexBuffers = [], textures = [[], []], uniforms = [[], []];
      var attributesDirty = true, texturesDirty = true, uniformsDirty = true, lastBase = null;
      var enabled = G.enabled || (G.enabled = new Set());
      var points = function (draw) {
        // Vertex attributes: pointed at the bound buffers, a draw's vertex
        // offset (indexed) and first instance moved into the pointers.
        var vbase = draw.indexed ? draw.vertexOffset : 0, ibase = draw.firstInstance;
        var key = vbase + ':' + ibase;
        if (attributesDirty || key !== lastBase) {
          var want = new Set();
          for (var s = 0; s < pipeline.buffers.length; ++s) {
            var l = pipeline.buffers[s], vb = vertexBuffers[s];
            gl.bindBuffer(gl.ARRAY_BUFFER, vb.buffer);
            for (var a = 0; a < l.attributes.length; ++a) {
              var at = l.attributes[a];
              var offset = vb.offset + at.offset + (l.instance ? ibase : vbase) * l.stride;
              if (offset < 0) throw new Error('a vertex offset of ' + vbase + ' points before vertex buffer ' + s + "'s start");
              gl.enableVertexAttribArray(at.location);
              if (at.integer) gl.vertexAttribIPointer(at.location, at.size, at.type, l.stride, offset);
              else gl.vertexAttribPointer(at.location, at.size, at.type, at.normalized, l.stride, offset);
              gl.vertexAttribDivisor(at.location, l.instance ? 1 : 0);
              want.add(at.location);
            }
          }
          enabled.forEach(function (location) { if (!want.has(location)) gl.disableVertexAttribArray(location); });
          G.enabled = enabled = want;
          attributesDirty = false;
          lastBase = key;
        }
        if (texturesDirty) {
          var stages = [[pipeline.vertex, G.VERTEX_UNITS], [pipeline.fragment, 0]];
          for (var t = 0; t < 2; ++t)
            for (var k = 0; k < stages[t][0].samplers; ++k) {
              var unit = stages[t][1] + k, bound = textures[t][k];
              gl.activeTexture(gl.TEXTURE0 + unit);
              gl.bindTexture(gl.TEXTURE_2D, bound.texture);
              gl.bindSampler(unit, bound.sampler);
            }
          texturesDirty = false;
        }
        if (uniformsDirty) {
          var blocks = [[pipeline.vertex, 0], [pipeline.fragment, G.FRAGMENT_POINTS]];
          for (var b2 = 0; b2 < 2; ++b2)
            for (var u = 0; u < blocks[b2][0].uniforms; ++u) {
              var p = uniforms[b2][u];
              if (!p) continue;  // never pushed: as on any backend, what the block holds is undefined
              gl.bindBufferRange(gl.UNIFORM_BUFFER, blocks[b2][1] + u, G.ubo, p.offset, Math.max(p.size, pipeline.blocks[b2][u] || 0));
            }
          uniformsDirty = false;
        }
        if (pipeline.base) {
          gl.uniform4i(pipeline.base, draw.indexed ? draw.vertexOffset : 0, draw.indexed ? draw.vertexOffset : draw.firstVertex,
                       draw.firstInstance, draw.firstInstance);
        }
      };

      for (i = 0; i < w.length; i += 1 + len) {
        op = w[i] & 0xff;
        len = w[i] >>> 8;
        var k = i + 1;
        switch (op) {
          case O.upload_buffer: {
            gl.bindBuffer(gl.COPY_WRITE_BUFFER, G.buffers[w[k]].buffer);
            gl.bufferSubData(gl.COPY_WRITE_BUFFER, w[k + 1], HEAPU8, w[k + 2], w[k + 4]);
            break;
          }
          case O.upload_texture:
            G.upload(G.textures[w[k]], w[k + 1], w[k + 2], w[k + 3], w[k + 4], w[k + 5], w[k + 8]);
            break;
          case O.copy_texture: {
            var source = G.framebuffer(w[k], NONE), destination = G.framebuffer(w[k + 5], NONE);
            gl.bindFramebuffer(gl.READ_FRAMEBUFFER, source);
            gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, destination);
            gl.disable(gl.SCISSOR_TEST);
            var x = w[k + 1], y = w[k + 2], cw = w[k + 3], ch = w[k + 4], dx = w[k + 6], dy = w[k + 7];
            gl.blitFramebuffer(x, y, x + cw, y + ch, dx, dy, dx + cw, dy + ch, gl.COLOR_BUFFER_BIT, gl.NEAREST);
            break;
          }
          case O.begin_render: {
            target = G.textures[w[k]];
            resolve = w[k + 6] !== NONE ? { id: w[k + 6], from: w[k] } : null;
            gl.bindFramebuffer(gl.FRAMEBUFFER, G.framebuffer(w[k], w[k + 7]));
            gl.viewport(0, 0, target.width, target.height);
            gl.disable(gl.SCISSOR_TEST);
            if (w[k + 1] === G.LOAD.clear) {
              gl.colorMask(true, true, true, true);
              var clear = [f32[k + 2], f32[k + 3], f32[k + 4], f32[k + 5]];
              if (target.f[4] === 'uint') gl.clearBufferuiv(gl.COLOR, 0, new Uint32Array(clear));
              else gl.clearBufferfv(gl.COLOR, 0, clear);
            }
            if (w[k + 7] !== NONE && w[k + 8] === G.LOAD.clear) {
              gl.depthMask(true);
              if (G.textures[w[k + 7]].f[1] === 'DEPTH_STENCIL') gl.clearBufferfi(gl.DEPTH_STENCIL, 0, f32[k + 9], w[k + 10]);
              else gl.clearBufferfv(gl.DEPTH, 0, [f32[k + 9]]);
            }
            pipeline = null;
            textures = [[], []];
            vertexBuffers = [];
            attributesDirty = texturesDirty = uniformsDirty = true;
            break;
          }
          case O.end_render:
            if (resolve) {
              var samples = G.framebuffer(resolve.from, NONE), resolved = G.framebuffer(resolve.id, NONE);
              gl.bindFramebuffer(gl.READ_FRAMEBUFFER, samples);
              gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, resolved);
              gl.disable(gl.SCISSOR_TEST);
              gl.blitFramebuffer(0, 0, target.width, target.height, 0, 0, target.width, target.height, gl.COLOR_BUFFER_BIT, gl.NEAREST);
            }
            target = null;
            resolve = null;
            break;
          case O.set_pipeline: {
            pipeline = G.pipelines[w[k]];
            gl.useProgram(pipeline.program);
            var bl = pipeline.blend;
            if (bl) {
              gl.enable(gl.BLEND);
              gl.blendFuncSeparate(bl.src, bl.dst, bl.srcAlpha, bl.dstAlpha);
              gl.blendEquationSeparate(bl.op, bl.opAlpha);
            } else gl.disable(gl.BLEND);
            var d = pipeline.depth;
            if (d && d.test) {
              gl.enable(gl.DEPTH_TEST);
              gl.depthFunc(d.func);
            } else gl.disable(gl.DEPTH_TEST);
            gl.depthMask(!!(d && d.write));
            gl.colorMask(true, true, true, true);
            attributesDirty = texturesDirty = uniformsDirty = true;
            break;
          }
          case O.set_viewport:
            gl.viewport(f32[k], f32[k + 1], f32[k + 2], f32[k + 3]);
            break;
          case O.set_scissor:
            gl.enable(gl.SCISSOR_TEST);
            gl.scissor(w[k], w[k + 1], Math.min(w[k + 2], target.width - w[k]), Math.min(w[k + 3], target.height - w[k + 1]));
            break;
          case O.bind_vertex_buffer:
            vertexBuffers[w[k]] = { buffer: G.buffers[w[k + 1]].buffer, offset: w[k + 2] };
            attributesDirty = true;
            break;
          case O.bind_index_buffer:
            gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, G.buffers[w[k]].buffer);
            indexOffset = w[k + 1];
            indexWide = w[k + 2] !== 0;
            break;
          case O.bind_texture:
            textures[w[k]][w[k + 1]] = { texture: G.textures[w[k + 2]].texture, sampler: G.samplers[w[k + 3]] };
            texturesDirty = true;
            break;
          case O.draw:
            points({ indexed: false, firstVertex: w[k + 2], firstInstance: w[k + 3] });
            gl.drawArraysInstanced(pipeline.mode, w[k + 2], w[k], w[k + 1]);
            break;
          case O.draw_indexed: {
            points({ indexed: true, vertexOffset: w[k + 3] | 0, firstVertex: 0, firstInstance: w[k + 4] });
            var size = indexWide ? 4 : 2;
            gl.drawElementsInstanced(pipeline.mode, w[k], indexWide ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT, indexOffset + w[k + 2] * size, w[k + 1]);
            break;
          }
          case O.push_uniforms: {
            if (w[k] !== G.STAGE.compute) uniforms[w[k]][w[k + 1]] = { offset: pushAt, size: w[k + 4] };
            pushAt += G.align(w[k + 4], G.uboAlign);
            uniformsDirty = true;
            break;
          }
          default:
            // Storage and compute: refused before the page sees them (page.cpp).
            throw new Error('WebGL2 has no ' + Object.keys(O).find(function (n) { return O[n] === op; }));
        }
      }
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      return G.check('the submission') ? 1 : 0;
    },

    // Rows of a region from the program's memory, `pitch` bytes apart (0: tight).
    upload: function (t, x, y, width, height, at, pitch) {
      var G = omuiGl, gl = G.gl, f = t.f, bpp = f[3], row = width * bpp;
      pitch = pitch || row;
      gl.bindTexture(gl.TEXTURE_2D, t.texture);
      var data, offset = 0;
      if (f[5] || pitch % bpp || at % Math.min(bpp, 4)) {
        // bgra8's bytes swapped, an odd pitch, data a typed array cannot start at: copied tight.
        data = new Uint8Array(row * height);
        for (var r = 0; r < height; ++r) data.set(HEAPU8.subarray(at + r * pitch, at + r * pitch + row), r * row);
        if (f[5]) for (var p = 0; p < data.length; p += 4) { var b = data[p]; data[p] = data[p + 2]; data[p + 2] = b; }
        pitch = row;
      } else {
        data = HEAPU8;
        offset = at;
      }
      gl.pixelStorei(gl.UNPACK_ROW_LENGTH, pitch / bpp);
      var type = gl[f[2]];
      var view = type === gl.UNSIGNED_BYTE ? data
               : type === gl.HALF_FLOAT ? new Uint16Array(data.buffer, data.byteOffset)
               : type === gl.FLOAT ? new Float32Array(data.buffer, data.byteOffset)
               : new Uint32Array(data.buffer, data.byteOffset);
      var unit = type === gl.UNSIGNED_BYTE ? 1 : type === gl.HALF_FLOAT ? 2 : 4;
      gl.texSubImage2D(gl.TEXTURE_2D, 0, x, y, width, height, gl[f[1]], type, view, offset / unit);
      gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);
    },

    // A half's bits of a float (round to nearest even; what a GPU writes).
    half: function (v) {
      var f = new Float32Array([v]), u = new Uint32Array(f.buffer)[0];
      var sign = (u >>> 16) & 0x8000, e = ((u >>> 23) & 0xff) - 127 + 15, m = u & 0x7fffff;
      if (((u >>> 23) & 0xff) === 0xff) return sign | 0x7c00 | (m ? 0x200 : 0);
      if (e >= 0x1f) return sign | 0x7c00;
      if (e <= 0) {
        if (e < -10) return sign;
        m |= 0x800000;
        var shift = 14 - e, h = m >>> shift, rest = m & ((1 << shift) - 1), halfway = 1 << (shift - 1);
        if (rest > halfway || (rest === halfway && (h & 1))) ++h;
        return sign | h;
      }
      var hm = m >>> 13, r2 = m & 0x1fff;
      var out = sign | (e << 10) | hm;
      if (r2 > 0x1000 || (r2 === 0x1000 && (hm & 1))) ++out;
      return out;
    },

    read: function (id, x, y, width, height, out) {
      var G = omuiGl, gl = G.gl, t = G.textures[id], f = t.f, n = width * height;
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, G.framebuffer(id, G.NONE));
      if (f[4] === 'unorm' && f[0] === 'RGBA8') {
        gl.readPixels(x, y, width, height, gl.RGBA, gl.UNSIGNED_BYTE, HEAPU8, out);
        if (f[5]) for (var p = out; p < out + n * 4; p += 4) { var b = HEAPU8[p]; HEAPU8[p] = HEAPU8[p + 2]; HEAPU8[p + 2] = b; }
      } else if (f[4] === 'unorm') {  // r8: read as RGBA, its red kept
        var bytes = new Uint8Array(n * 4);
        gl.readPixels(x, y, width, height, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
        for (var i = 0; i < n; ++i) HEAPU8[out + i] = bytes[i * 4];
      } else if (f[4] === 'float') {
        var floats = new Float32Array(n * 4);
        gl.readPixels(x, y, width, height, gl.RGBA, gl.FLOAT, floats);
        if (f[0] === 'RGBA16F') {
          var halves = new Uint16Array(n * 4);
          for (var h = 0; h < n * 4; ++h) halves[h] = G.half(floats[h]);
          HEAPU8.set(new Uint8Array(halves.buffer), out);
        } else {
          var reds = new Float32Array(n);
          for (var r = 0; r < n; ++r) reds[r] = floats[r * 4];
          HEAPU8.set(new Uint8Array(reds.buffer), out);
        }
      } else {  // r32ui
        var uints = new Uint32Array(n * 4);
        gl.readPixels(x, y, width, height, gl.RGBA_INTEGER, gl.UNSIGNED_INT, uints);
        var ur = new Uint32Array(n);
        for (var q = 0; q < n; ++q) ur[q] = uints[q * 4];
        HEAPU8.set(new Uint8Array(ur.buffer), out);
      }
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
      return G.check('reading the texture back') ? 1 : 0;
    },
  },

  // 1 and [software, largest texture, most samples, float colour targets,
  // half-float colour targets] in `out` (5 words), or 0 (omui_webgl_why).
  // `selector`: the canvas present() shows on; "": one nobody sees.
  omui_webgl_open__deps: ['$omuiGl', '$UTF8ToString'],
  omui_webgl_open: function (selector, out) {
    var G = omuiGl;
    try {
      if (!G.open(UTF8ToString(selector))) return 0;
    } catch (e) { G.why = 'WebGL2: ' + (e && e.message || e); return 0; }
    HEAPU32[out >> 2] = G.info.software ? 1 : 0;
    HEAPU32[(out >> 2) + 1] = G.info.maxTexture;
    HEAPU32[(out >> 2) + 2] = G.info.maxSamples;
    HEAPU32[(out >> 2) + 3] = G.info.colorFloat ? 1 : 0;
    HEAPU32[(out >> 2) + 4] = G.info.halfFloat ? 1 : 0;
    return 1;
  },

  omui_webgl_close__deps: ['$omuiGl'],
  omui_webgl_close: function () { omuiGl.close(); },

  omui_webgl_name__deps: ['$omuiGl', '$stringToNewUTF8'],
  omui_webgl_name: function () { return stringToNewUTF8(omuiGl.info ? omuiGl.info.name : ''); },

  omui_webgl_why__deps: ['$omuiGl', '$stringToNewUTF8'],
  omui_webgl_why: function () { return stringToNewUTF8(omuiGl.why || 'no WebGL2 context'); },

  omui_webgl_buffer__deps: ['$omuiGl'],
  omui_webgl_buffer: function (id, size, usage) {
    var G = omuiGl, gl = G.gl;
    try {
      var b = gl.createBuffer();
      // An index buffer is bound as one from its first binding on (WebGL2's
      // rule); every other kind takes its data through COPY_WRITE_BUFFER.
      var index = (usage & G.BUF.index) !== 0;
      var target = index ? gl.ELEMENT_ARRAY_BUFFER : gl.COPY_WRITE_BUFFER;
      gl.bindBuffer(target, b);
      gl.bufferData(target, size, gl.DYNAMIC_DRAW);
      G.buffers[id] = { buffer: b, size: size, index: index };
      return G.check('creating a buffer') ? 1 : 0;
    } catch (e) { return G.fail(e); }
  },

  omui_webgl_texture__deps: ['$omuiGl'],
  omui_webgl_texture: function (id, format, width, height, samples) {
    var G = omuiGl, gl = G.gl;
    try {
      var f = G.FORMATS[format];
      var t = { f: f, width: width, height: height, samples: samples };
      if (samples > 1) {
        t.renderbuffer = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, t.renderbuffer);
        gl.renderbufferStorageMultisample(gl.RENDERBUFFER, samples, gl[f[0]], width, height);
      } else {
        t.texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, t.texture);
        gl.texStorage2D(gl.TEXTURE_2D, 1, gl[f[0]], width, height);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      }
      G.textures[id] = t;
      return G.check('creating a texture') ? 1 : 0;
    } catch (e) { return G.fail(e); }
  },

  omui_webgl_sampler__deps: ['$omuiGl'],
  omui_webgl_sampler: function (id, min, mag, u, v) {
    var G = omuiGl, gl = G.gl;
    try {
      var s = gl.createSampler();
      gl.samplerParameteri(s, gl.TEXTURE_MIN_FILTER, gl[G.FILTERS[min]]);
      gl.samplerParameteri(s, gl.TEXTURE_MAG_FILTER, gl[G.FILTERS[mag]]);
      gl.samplerParameteri(s, gl.TEXTURE_WRAP_S, gl[G.ADDRESS[u]]);
      gl.samplerParameteri(s, gl.TEXTURE_WRAP_T, gl[G.ADDRESS[v]]);
      G.samplers[id] = s;
      return G.check('creating a sampler') ? 1 : 0;
    } catch (e) { return G.fail(e); }
  },

  omui_webgl_graphics__deps: ['$omuiGl', '$UTF8ToString'],
  omui_webgl_graphics: function (id, words, count, vs, vsLength, fs, fsLength, label) {
    var G = omuiGl;
    try {
      var w = HEAPU32.slice(words >> 2, (words >> 2) + count);
      G.pipelines[id] = G.graphics(w, UTF8ToString(vs, vsLength), UTF8ToString(fs, fsLength), UTF8ToString(label));
      return G.check('creating the pipeline') ? 1 : 0;
    } catch (e) { return G.fail(e); }
  },

  omui_webgl_destroy__deps: ['$omuiGl'],
  omui_webgl_destroy: function (kind, id) {
    var G = omuiGl, gl = G.gl, K = G.KIND;
    if (!gl) return;
    if (kind === K.buffer && G.buffers[id]) { gl.deleteBuffer(G.buffers[id].buffer); G.buffers[id] = null; }
    else if (kind === K.texture && G.textures[id]) {
      G.forget(id);
      var t = G.textures[id];
      if (t.renderbuffer) gl.deleteRenderbuffer(t.renderbuffer);
      else gl.deleteTexture(t.texture);
      G.textures[id] = null;
    } else if (kind === K.sampler && G.samplers[id]) { gl.deleteSampler(G.samplers[id]); G.samplers[id] = null; }
    else if (kind === K.graphics && G.pipelines[id]) { gl.deleteProgram(G.pipelines[id].program); G.pipelines[id] = null; }
  },

  omui_webgl_run__deps: ['$omuiGl'],
  omui_webgl_run: function (words, count) {
    var G = omuiGl;
    try {
      return G.run(HEAPU32.subarray(words >> 2, (words >> 2) + count));
    } catch (e) {
      G.gl.bindFramebuffer(G.gl.FRAMEBUFFER, null);
      return G.fail(e);
    }
  },

  omui_webgl_read_texture__deps: ['$omuiGl'],
  omui_webgl_read_texture: function (id, x, y, width, height, out) {
    var G = omuiGl;
    try {
      return G.read(id, x, y, width, height, out);
    } catch (e) { return G.fail(e); }
  },

  omui_webgl_read_buffer__deps: ['$omuiGl'],
  omui_webgl_read_buffer: function (id, offset, length, out) {
    var G = omuiGl, gl = G.gl;
    try {
      gl.bindBuffer(gl.COPY_READ_BUFFER, G.buffers[id].buffer);
      gl.getBufferSubData(gl.COPY_READ_BUFFER, offset, HEAPU8, out, length);
      return G.check('reading the buffer back') ? 1 : 0;
    } catch (e) { return G.fail(e); }
  },

  omui_webgl_error__deps: ['$omuiGl', '$stringToNewUTF8'],
  omui_webgl_error: function () {
    var G = omuiGl;
    if (G.lost) return stringToNewUTF8(G.lost);
    if (!G.errors.length) return 0;
    var e = G.errors.join('; ');
    G.errors = [];
    return stringToNewUTF8(e);
  },

  omui_webgl_lost__deps: ['$omuiGl'],
  omui_webgl_lost: function () { return omuiGl.lost ? 1 : 0; },

  // The frame onto the canvas the context is: flipped (the canvas is GL's,
  // bottom-up), texel for pixel at the same size, filtered when scaled.
  // 1 shown, 0 nothing to show (a canvas of no size), -1 failed.
  omui_webgl_present__deps: ['$omuiGl'],
  omui_webgl_present: function (id) {
    var G = omuiGl, gl = G.gl;
    try {
      var c = G.canvas, t = G.textures[id];
      if (!c.width || !c.height) return 0;
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, G.framebuffer(id, G.NONE));
      gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
      gl.disable(gl.SCISSOR_TEST);
      var same = t.width === c.width && t.height === c.height;
      gl.blitFramebuffer(0, 0, t.width, t.height, 0, c.height, c.width, 0, gl.COLOR_BUFFER_BIT, same ? gl.NEAREST : gl.LINEAR);
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
      return G.check('presenting') ? 1 : -1;
    } catch (e) { G.fail(e); return -1; }
  },
});
