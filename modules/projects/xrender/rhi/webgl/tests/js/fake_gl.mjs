// A WebGL2 context that remembers what it is asked, for the page's half of
// openmui.rhi.webgl under node: every call lands in `log` as
// [name, ...arguments] with GL's enumerations as their names (`raw` keeps
// the numbers), objects carry
// what they were made as, buffers keep their bytes (bufferSubData, then
// getBufferSubData reads them back), and readPixels writes a known pattern
// (pixel (x, y), channel c of the region: x + 16·y + c).
const ENUMS = {
  NO_ERROR: 0, INVALID_ENUM: 0x0500, INVALID_OPERATION: 0x0502, TEXTURE_2D: 0x0DE1, UNSIGNED_BYTE: 0x1401, UNSIGNED_SHORT: 0x1403,
  UNSIGNED_INT: 0x1405, FLOAT: 0x1406, HALF_FLOAT: 0x140B, RED: 0x1903, RGBA: 0x1908, DEPTH_COMPONENT: 0x1902, NEAREST: 0x2600, LINEAR: 0x2601,
  TEXTURE_MAG_FILTER: 0x2800, TEXTURE_MIN_FILTER: 0x2801, TEXTURE_WRAP_S: 0x2802, TEXTURE_WRAP_T: 0x2803, REPEAT: 0x2901, CLAMP_TO_EDGE: 0x812F,
  MIRRORED_REPEAT: 0x8370, TRIANGLES: 0x0004, TRIANGLE_STRIP: 0x0005, LINES: 0x0001, ZERO: 0, ONE: 1, SRC_COLOR: 0x0300,
  ONE_MINUS_SRC_COLOR: 0x0301, SRC_ALPHA: 0x0302, ONE_MINUS_SRC_ALPHA: 0x0303, DST_ALPHA: 0x0304, ONE_MINUS_DST_ALPHA: 0x0305,
  DST_COLOR: 0x0306, ONE_MINUS_DST_COLOR: 0x0307, FUNC_ADD: 0x8006, MIN: 0x8007, MAX: 0x8008, FUNC_SUBTRACT: 0x800A,
  FUNC_REVERSE_SUBTRACT: 0x800B, NEVER: 0x0200, LESS: 0x0201, EQUAL: 0x0202, LEQUAL: 0x0203, GREATER: 0x0204, NOTEQUAL: 0x0205,
  GEQUAL: 0x0206, ALWAYS: 0x0207, BLEND: 0x0BE2, DEPTH_TEST: 0x0B71, SCISSOR_TEST: 0x0C11, ARRAY_BUFFER: 0x8892,
  ELEMENT_ARRAY_BUFFER: 0x8893, COPY_READ_BUFFER: 0x8F36, COPY_WRITE_BUFFER: 0x8F37, UNIFORM_BUFFER: 0x8A11, DYNAMIC_DRAW: 0x88E8,
  FRAMEBUFFER: 0x8D40, READ_FRAMEBUFFER: 0x8CA8, DRAW_FRAMEBUFFER: 0x8CA9, RENDERBUFFER: 0x8D41, COLOR_ATTACHMENT0: 0x8CE0,
  DEPTH_ATTACHMENT: 0x8D00, DEPTH_STENCIL_ATTACHMENT: 0x821A, FRAMEBUFFER_COMPLETE: 0x8CD5, COLOR_BUFFER_BIT: 0x4000, COLOR: 0x1800,
  DEPTH: 0x1801, DEPTH_STENCIL: 0x84F9, RGBA8: 0x8058, R8: 0x8229, RGBA16F: 0x881A, R32F: 0x822E, R32UI: 0x8236, RED_INTEGER: 0x8D94,
  RGBA_INTEGER: 0x8D99, DEPTH_COMPONENT32F: 0x8CAC, DEPTH32F_STENCIL8: 0x8CAD, FLOAT_32_UNSIGNED_INT_24_8_REV: 0x8DAD,
  VERTEX_SHADER: 0x8B31, FRAGMENT_SHADER: 0x8B30, COMPILE_STATUS: 0x8B81, LINK_STATUS: 0x8B82, INVALID_INDEX: 0xFFFFFFFF,
  UNIFORM_BLOCK_DATA_SIZE: 0x8A40, TEXTURE0: 0x84C0, UNPACK_ALIGNMENT: 0x0CF5, PACK_ALIGNMENT: 0x0D05, UNPACK_ROW_LENGTH: 0x0CF2,
  MAX_TEXTURE_SIZE: 0x0D33, MAX_SAMPLES: 0x8D57, RENDERER: 0x1F01, UNIFORM_BUFFER_OFFSET_ALIGNMENT: 0x8A34, MAX_UNIFORM_BLOCK_SIZE: 0x8A30,
};
const NAMES = new Map(Object.entries(ENUMS).map(([k, v]) => [v, k]));

export class FakeGl {
  constructor({ renderer = 'ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device (Subzero)), SwiftShader driver)', extensions = ['EXT_color_buffer_float'],
                compiles = true, links = true, blocks = { omui_vs_uniform0: 16, omui_fs_uniform0: 32 } } = {}) {
    this.log = [];
    this.raw = [];
    this.errors = [];
    this.bound = {};
    const gl = this;
    let next = 1;
    const make = (kind) => () => ({ kind, id: next++ });
    // Numbers that are GL's enumerations as their names (in `log`), and as
    // they were (in `raw`: a size can be an enumeration's number).
    const record = (name, args) => {
      gl.log.push([name, ...args.map((a) => (typeof a === 'number' && NAMES.has(a) && a > 0x100 ? NAMES.get(a) : a))]);
      gl.raw.push([name, ...args]);
    };
    const params = { MAX_TEXTURE_SIZE: 4096, MAX_SAMPLES: 4, RENDERER: renderer, UNIFORM_BUFFER_OFFSET_ALIGNMENT: 256, MAX_UNIFORM_BLOCK_SIZE: 16384 };
    const special = {
      getError: () => gl.errors.shift() ?? 0,
      getExtension: (name) => (extensions.includes(name) ? {} : null),
      getParameter: (p) => params[NAMES.get(p)],
      getShaderParameter: () => compiles,
      getShaderInfoLog: () => 'ERROR: 0:3: syntax error',
      getProgramParameter: () => links,
      getProgramInfoLog: () => 'link failed',
      getUniformLocation: (program, name) => ({ uniform: name }),
      getUniformBlockIndex: (program, name) => (name in blocks ? Object.keys(blocks).indexOf(name) : ENUMS.INVALID_INDEX),
      getActiveUniformBlockParameter: (program, index) => Object.values(blocks)[index],
      checkFramebufferStatus: () => ENUMS.FRAMEBUFFER_COMPLETE,
      createBuffer: make('buffer'), createTexture: make('texture'), createRenderbuffer: make('renderbuffer'), createSampler: make('sampler'),
      createFramebuffer: make('framebuffer'), createVertexArray: make('vertexArray'), createShader: make('shader'), createProgram: make('program'),
      bindBuffer: (target, b) => { gl.bound[NAMES.get(target)] = b; },
      bufferData: (target, size) => { gl.bound[NAMES.get(target)].bytes = new Uint8Array(size); },
      bufferSubData: (target, offset, data, srcOffset = 0, length = data.length - srcOffset) => {
        gl.bound[NAMES.get(target)].bytes.set(data.subarray(srcOffset, srcOffset + length), offset);
      },
      getBufferSubData: (target, offset, dst, dstOffset = 0, length = dst.length - dstOffset) => {
        dst.set(gl.bound[NAMES.get(target)].bytes.subarray(offset, offset + length), dstOffset);
      },
      readPixels: (x, y, w, h, format, type, dst, dstOffset = 0) => {
        const channels = format === ENUMS.RGBA || format === ENUMS.RGBA_INTEGER ? 4 : 1;
        for (let j = 0; j < h; ++j)
          for (let i = 0; i < w; ++i)
            for (let c = 0; c < channels; ++c) {
              const v = (x + i) + 16 * (y + j) + c;
              dst[dstOffset + (j * w + i) * channels + c] = type === ENUMS.FLOAT ? v / 256 : v & 255;
            }
      },
    };
    return new Proxy(this, {
      get(target, key) {
        if (key in target) return target[key];
        if (typeof key === 'string' && key in ENUMS) return ENUMS[key];
        return (...args) => {
          record(key, args);
          return special[key] ? special[key](...args) : undefined;
        };
      },
    });
  }

  calls(name, raw = false) { return (raw ? this.raw : this.log).filter((c) => c[0] === name); }
}

// A canvas whose WebGL2 context is `gl` (null: none).
export function canvas(gl, width = 8, height = 8) {
  const c = { width, height, listeners: {}, attributes: null };
  c.getContext = (kind, attributes) => {
    c.attributes = attributes;
    return kind === 'webgl2' ? gl : null;
  };
  c.addEventListener = (type, f) => { c.listeners[type] = f; };
  return c;
}
