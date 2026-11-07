// A WebGPU that remembers what it is asked, for the page's half of
// openmui.rhi.webgpu under node: every call lands in `log` as
// [name, ...arguments] (descriptors as given), objects carry what they were
// made from, buffers keep their bytes so a readback has something to read,
// and a copy from a texture to a buffer writes a known pattern
// (texel (x, y), byte c: x + 16·y + c, wrapping).
export const GPUBufferUsage = { MAP_READ: 1, MAP_WRITE: 2, COPY_SRC: 4, COPY_DST: 8, INDEX: 16, VERTEX: 32, UNIFORM: 64, STORAGE: 128, INDIRECT: 256 };
export const GPUTextureUsage = { COPY_SRC: 1, COPY_DST: 2, TEXTURE_BINDING: 4, STORAGE_BINDING: 8, RENDER_ATTACHMENT: 16 };
export const GPUMapMode = { READ: 1, WRITE: 2 };

const BYTES = { rgba8unorm: 4, bgra8unorm: 4, r8unorm: 1, rgba16float: 8, r32float: 4, r32uint: 4, depth32float: 4, 'depth32float-stencil8': 8 };

export class FakeGpu {
  constructor({ adapter = true, features = ['depth32float-stencil8'], maxTexture = 16384, info = { vendor: 'fake', architecture: 'test' } } = {}) {
    this.log = [];
    this.listeners = {};
    this.loseDevice = null;
    const gpu = this;
    const record = (name, ...args) => { gpu.log.push([name, ...args]); };
    this.navigator = {
      gpu: {
        requestAdapter(options) {
          record('requestAdapter', options);
          if (!adapter) return Promise.resolve(null);
          return Promise.resolve({
            features: new Set(features),
            limits: { maxTextureDimension2D: maxTexture },
            info,
            requestDevice(desc) {
              record('requestDevice', desc);
              return Promise.resolve(gpu.device(desc));
            },
          });
        },
        getPreferredCanvasFormat() { return 'bgra8unorm'; },
      },
    };
  }

  // What was called by that name, in order.
  calls(name) { return this.log.filter((c) => c[0] === name); }

  // The listeners' 'uncapturederror', as the browser would raise it.
  raise(message) { for (const f of this.listeners.uncapturederror || []) f({ error: { message } }); }

  device(desc) {
    const gpu = this;
    const record = (name, ...args) => { gpu.log.push([name, ...args]); };
    const buffer = (d) => {
      const b = {
        kind: 'buffer', desc: d, bytes: new Uint8Array(d.size), destroyed: false, mapped: false,
        destroy() { b.destroyed = true; record('destroyBuffer', d.label); },
        mapAsync(mode, offset, size) { record('mapAsync', mode, offset, size); b.mapped = true; return Promise.resolve(); },
        getMappedRange(offset = 0, size) { return b.bytes.buffer.slice(offset, size === undefined ? undefined : offset + size); },
        unmap() { b.mapped = false; record('unmap'); },
      };
      return b;
    };
    const pass = (kind, d) => {
      const p = { kind, desc: d };
      for (const m of ['setPipeline', 'setViewport', 'setScissorRect', 'setVertexBuffer', 'setIndexBuffer', 'setBindGroup', 'draw', 'drawIndexed',
                       'dispatchWorkgroups', 'end']) p[m] = (...args) => record(m, ...args);
      return p;
    };
    let lost;
    gpu.loseDevice = (message) => lost({ message });
    return {
      features: new Set(desc.requiredFeatures || []),
      limits: { maxTextureDimension2D: (desc.requiredLimits || {}).maxTextureDimension2D || 8192 },
      lost: new Promise((r) => { lost = r; }),
      addEventListener(type, f) { (gpu.listeners[type] ||= []).push(f); },
      queue: {
        writeBuffer(b, offset, data, dataOffset, size) {
          record('writeBuffer', b.desc.label, offset, size);
          b.bytes.set(data.subarray(dataOffset, dataOffset + size), offset);
        },
        // What a command buffer does happens when it is submitted (after
        // the writeBuffer calls made before it), as on the queue's timeline.
        submit(list) {
          record('submit', list.length);
          for (const c of list) for (const effect of c.effects) effect();
        },
        onSubmittedWorkDone() { record('onSubmittedWorkDone'); return Promise.resolve(); },
      },
      createBuffer(d) { record('createBuffer', d); return buffer(d); },
      createTexture(d) {
        record('createTexture', d);
        const t = { kind: 'texture', desc: d, destroy() { record('destroyTexture', d.label); } };
        t.createView = () => ({ kind: 'view', texture: t });
        return t;
      },
      createSampler(d) { record('createSampler', d); return { kind: 'sampler', desc: d }; },
      createShaderModule(d) { record('createShaderModule', d.code); return { kind: 'module', code: d.code }; },
      createRenderPipeline(d) {
        record('createRenderPipeline', d);
        return { kind: 'pipeline', desc: d, getBindGroupLayout: (g) => ({ kind: 'layout', group: g }) };
      },
      createComputePipeline(d) {
        record('createComputePipeline', d);
        return { kind: 'compute', desc: d, getBindGroupLayout: (g) => ({ kind: 'layout', group: g }) };
      },
      createBindGroup(d) { record('createBindGroup', d); return { kind: 'bindGroup', desc: d }; },
      createCommandEncoder() {
        const effects = [];
        return {
          copyBufferToBuffer(src, srcOffset, dst, dstOffset, size) {
            record('copyBufferToBuffer', src, srcOffset, dst, dstOffset, size);
            effects.push(() => dst.bytes.set(src.bytes.subarray(srcOffset, srcOffset + size), dstOffset));
          },
          copyBufferToTexture: (...a) => record('copyBufferToTexture', ...a),
          copyTextureToTexture: (...a) => record('copyTextureToTexture', ...a),
          copyTextureToBuffer(source, destination, size) {
            record('copyTextureToBuffer', source, destination, size);
            effects.push(() => {
              const bpp = BYTES[source.texture.desc.format];
              for (let y = 0; y < size.height; ++y)
                for (let x = 0; x < size.width * bpp; ++x)
                  destination.buffer.bytes[y * destination.bytesPerRow + x] =
                    (Math.floor(x / bpp) + source.origin.x + 16 * (y + source.origin.y) + x % bpp) & 255;
            });
          },
          beginRenderPass(d) { record('beginRenderPass', d); return pass('render', d); },
          beginComputePass(d) { record('beginComputePass', d); return pass('compute', d); },
          finish() { return { kind: 'commands', effects }; },
        };
      },
    };
  }
}
