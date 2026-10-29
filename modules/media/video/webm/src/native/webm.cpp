// The WebM encoder and decoder over libvpx, libopus and libwebm (imported as
// modules through mwrap, here only: D-83).
module openmui.video.webm;

import std;
import openmui.video;
import mcpplibs.mwrap.libvpx;
import mcpplibs.mwrap.opus;

namespace omui::video {

namespace {

namespace vm = mcpplibs::mwrap::libvpx::macros;
namespace vpx = mcpplibs::mwrap::libvpx;
namespace om = mcpplibs::mwrap::opus::macros;

constexpr std::int32_t opus_rate = 48000;
constexpr std::int32_t opus_frame = 960;  // 20 ms
constexpr std::int64_t ns = 1'000'000'000;

// mkvmuxer writes through this: an openmui.video File (seekable: the sizes,
// cues and duration are written last).
class Writer final : public mkvmuxer::IMkvWriter {
public:
    explicit Writer(File& file) : file_(file) {}
    mkvmuxer::int32 Write(const void* buf, mkvmuxer::uint32 len) override {
        return file_.write(std::span(static_cast<const std::byte*>(buf), len)) ? 0 : -1;
    }
    mkvmuxer::int64 Position() const override { return file_.position(); }
    mkvmuxer::int32 Position(mkvmuxer::int64 position) override { return file_.seek(position) ? 0 : -1; }
    bool Seekable() const override { return true; }
    void ElementStartNotify(mkvmuxer::uint64, mkvmuxer::int64) override {}

private:
    File& file_;
};

struct Packet {
    std::vector<unsigned char> data;
    std::uint64_t              time = 0;  // ns
    bool                       key = false, audio = false;
    std::uint64_t              discard = 0;  // ns of padding at its end (the last sound packet)
};

std::array<unsigned char, 19> opus_head(std::int32_t channels, std::int32_t pre_skip) {
    return {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, static_cast<unsigned char>(channels), static_cast<unsigned char>(pre_skip & 0xff),
            static_cast<unsigned char>(pre_skip >> 8), opus_rate & 0xff, (opus_rate >> 8) & 0xff, (opus_rate >> 16) & 0xff, 0, 0, 0, 0};
}

class WebmEncoder final : public Encoder {
public:
    WebmEncoder(std::string name, WebmOptions options) : name_(std::move(name)), options_(options) {}
    ~WebmEncoder() override { release(); }
    std::string_view name() const noexcept override { return "webm"; }

    std::expected<void, std::string> begin(const Format& format, Output& out) override {
        format_ = format;
        out_ = &out;
        if (format.channels > 0 && (format.timing.rate != opus_rate || format.channels > 2))
            return std::unexpected(std::format("Opus takes 48 kHz mono or stereo sound, not {} Hz × {}", format.timing.rate, format.channels));
        vpx_codec_enc_cfg_t cfg;
        if (vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &cfg, 0) != VPX_CODEC_OK) return std::unexpected("libvpx: no VP9 encoder");
        cfg.g_w = static_cast<unsigned>(format.width);
        cfg.g_h = static_cast<unsigned>(format.height);
        cfg.g_timebase = {static_cast<int>(format.timing.fps.den), static_cast<int>(format.timing.fps.num)};
        cfg.g_threads = static_cast<unsigned>(std::max(1, options_.threads));
        if (options_.realtime) cfg.g_lag_in_frames = 0;
        if (options_.bitrate > 0) {
            cfg.rc_end_usage = VPX_VBR;
            cfg.rc_target_bitrate = static_cast<unsigned>(options_.bitrate);
        } else cfg.rc_end_usage = VPX_Q;
        if (options_.keyframes > 0) cfg.kf_max_dist = static_cast<unsigned>(options_.keyframes);
        if (vm::vpx_codec_enc_init(&vpx_, vpx_codec_vp9_cx(), &cfg, 0) != VPX_CODEC_OK)
            return std::unexpected(std::format("libvpx: {}", vpx_codec_error(&vpx_)));
        vpx_open_ = true;
        vpx::vpx_codec_control_VP8E_SET_CPUUSED(&vpx_, VP8E_SET_CPUUSED, std::clamp(options_.speed, 0, 9));
        if (options_.bitrate <= 0) vpx::vpx_codec_control_VP8E_SET_CQ_LEVEL(&vpx_, VP8E_SET_CQ_LEVEL, static_cast<unsigned>(std::clamp(options_.quality, 0, 63)));
        vpx::vpx_codec_control_VP9E_SET_COLOR_SPACE(&vpx_, VP9E_SET_COLOR_SPACE, VPX_CS_BT_709);
        vpx::vpx_codec_control_VP9E_SET_COLOR_RANGE(&vpx_, VP9E_SET_COLOR_RANGE, static_cast<int>(VPX_CR_STUDIO_RANGE));
        if (options_.lossless) vpx::vpx_codec_control_VP9E_SET_LOSSLESS(&vpx_, VP9E_SET_LOSSLESS, 1u);
        if (options_.threads > 1) {
            vpx::vpx_codec_control_VP9E_SET_TILE_COLUMNS(&vpx_, VP9E_SET_TILE_COLUMNS, std::min(6, std::bit_width(static_cast<unsigned>(options_.threads)) - 1));
            vpx::vpx_codec_control_VP9E_SET_ROW_MT(&vpx_, VP9E_SET_ROW_MT, 1u);
        }
        deadline_ = options_.realtime ? vm::VPX_DL_REALTIME : vm::VPX_DL_GOOD_QUALITY;
        if (format.channels > 0) {
            int error = 0;
            opus_ = opus_encoder_create(opus_rate, format.channels, om::OPUS_APPLICATION_AUDIO, &error);
            if (!opus_ || error != om::OPUS_OK) return std::unexpected(std::format("libopus: {}", opus_strerror(error)));
            opus_encoder_ctl(opus_, om::OPUS_SET_BITRATE(options_.audio_bitrate * 1000));
            opus_int32 lookahead = 0;
            opus_encoder_ctl(opus_, om::OPUS_GET_LOOKAHEAD(&lookahead));
            pre_skip_ = lookahead;
        }
        packets_.clear();
        pending_.clear();
        sound_packets_ = 0;
        samples_in_ = 0;
        return {};
    }

    std::expected<void, std::string> frame(std::int64_t index, const imageio::View& pixels, std::span<const float> samples) override {
        if (pixels.width != format_.width || pixels.height != format_.height)
            return std::unexpected(std::format("a frame of {} × {} in a video of {} × {}", pixels.width, pixels.height, format_.width, format_.height));
        to_yuv420(pixels, picture_);
        vpx_image_t img;
        vpx_img_wrap(&img, VPX_IMG_FMT_I420, static_cast<unsigned>(format_.width), static_cast<unsigned>(format_.height), 1, picture_.y.data());
        img.planes[vm::VPX_PLANE_Y] = picture_.y.data();
        img.planes[vm::VPX_PLANE_U] = picture_.u.data();
        img.planes[vm::VPX_PLANE_V] = picture_.v.data();
        img.stride[vm::VPX_PLANE_Y] = format_.width;
        img.stride[vm::VPX_PLANE_U] = img.stride[vm::VPX_PLANE_V] = picture_.chroma_width();
        img.cs = VPX_CS_BT_709;
        img.range = VPX_CR_STUDIO_RANGE;
        if (vpx_codec_encode(&vpx_, &img, index, 1, 0, deadline_) != VPX_CODEC_OK) return std::unexpected(std::format("libvpx: {}", vpx_codec_error(&vpx_)));
        drain_video();
        if (opus_) {
            pending_.insert(pending_.end(), samples.begin(), samples.end());
            samples_in_ += static_cast<std::int64_t>(samples.size()) / format_.channels;
            if (auto r = encode_sound(false); !r) return r;
        }
        return {};
    }

    std::expected<void, std::string> finish() override {
        // What libvpx's look-ahead still holds.
        for (std::size_t before = std::numeric_limits<std::size_t>::max(); before != packets_.size();) {
            before = packets_.size();
            if (vpx_codec_encode(&vpx_, nullptr, -1, 1, 0, deadline_) != VPX_CODEC_OK) return std::unexpected(std::format("libvpx: {}", vpx_codec_error(&vpx_)));
            drain_video();
        }
        if (opus_) {
            // The look-ahead's worth of silence pushes the last samples out; the
            // last packet is filled to its 20 ms, and says how much of it to drop.
            pending_.insert(pending_.end(), static_cast<std::size_t>(pre_skip_ * format_.channels), 0.0f);
            const auto per = static_cast<std::size_t>(opus_frame * format_.channels);
            const std::size_t fill = (per - pending_.size() % per) % per;
            pending_.insert(pending_.end(), fill, 0.0f);
            if (auto r = encode_sound(true); !r) return r;
            if (!packets_.empty())
                for (auto it = packets_.rbegin(); it != packets_.rend(); ++it)
                    if (it->audio) {
                        it->discard = static_cast<std::uint64_t>(fill / static_cast<std::size_t>(format_.channels)) * ns / opus_rate;
                        break;
                    }
        }
        return write();
    }

private:
    void drain_video() {
        vpx_codec_iter_t it = nullptr;
        while (const vpx_codec_cx_pkt_t* p = vpx_codec_get_cx_data(&vpx_, &it)) {
            if (p->kind != VPX_CODEC_CX_FRAME_PKT) continue;
            const auto* b = static_cast<const unsigned char*>(p->data.frame.buf);
            packets_.push_back({{b, b + p->data.frame.sz}, static_cast<std::uint64_t>(format_.timing.time_of(p->data.frame.pts, ns)),
                                (p->data.frame.flags & vm::VPX_FRAME_IS_KEY) != 0, false, 0});
        }
    }

    std::expected<void, std::string> encode_sound(bool all) {
        const auto per = static_cast<std::size_t>(opus_frame * format_.channels);
        std::array<unsigned char, 4000> buffer{};
        std::size_t at = 0;
        while (pending_.size() - at >= per) {
            const opus_int32 n = opus_encode_float(opus_, pending_.data() + at, opus_frame, buffer.data(), static_cast<opus_int32>(buffer.size()));
            if (n < 0) return std::unexpected(std::format("libopus: {}", opus_strerror(n)));
            packets_.push_back({{buffer.begin(), buffer.begin() + n}, static_cast<std::uint64_t>(sound_packets_) * opus_frame * ns / opus_rate, true, true, 0});
            ++sound_packets_;
            at += per;
        }
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(at));
        if (all && !pending_.empty()) return std::unexpected("libopus: samples left over");
        return {};
    }

    std::expected<void, std::string> write() {
        auto file = out_->create(name_);
        if (!file) return std::unexpected(file.error());
        Writer writer(**file);
        mkvmuxer::Segment segment;
        if (!segment.Init(&writer)) return std::unexpected("libwebm: cannot start the file");
        segment.set_mode(mkvmuxer::Segment::kFile);
        segment.OutputCues(true);
        segment.GetSegmentInfo()->set_writing_app("openmui.video");
        const auto video = segment.AddVideoTrack(format_.width, format_.height, 1);
        auto* v = static_cast<mkvmuxer::VideoTrack*>(segment.GetTrackByNumber(video));
        if (!v) return std::unexpected("libwebm: no video track");
        v->set_codec_id(mkvmuxer::Tracks::kVp9CodecId);
        v->set_uid(options_.video_uid);
        v->set_frame_rate(format_.timing.fps.fps());
        mkvmuxer::Colour colour;
        colour.set_matrix_coefficients(mkvmuxer::Colour::kBt709);
        colour.set_range(mkvmuxer::Colour::kBroadcastRange);
        colour.set_transfer_characteristics(mkvmuxer::Colour::kIturBt709Tc);
        colour.set_primaries(mkvmuxer::Colour::kIturBt709P);
        colour.set_chroma_siting_horz(mkvmuxer::Colour::kHalfCsh);  // between its pixels: each 2 × 2 block's mean
        colour.set_chroma_siting_vert(mkvmuxer::Colour::kHalfCsv);
        v->SetColour(colour);
        segment.CuesTrack(video);
        mkvmuxer::uint64 audio = 0;
        if (opus_) {
            audio = segment.AddAudioTrack(opus_rate, format_.channels, 2);
            auto* a = static_cast<mkvmuxer::AudioTrack*>(segment.GetTrackByNumber(audio));
            if (!a) return std::unexpected("libwebm: no audio track");
            a->set_codec_id(mkvmuxer::Tracks::kOpusCodecId);
            a->set_uid(options_.audio_uid);
            const auto head = opus_head(format_.channels, pre_skip_);
            a->SetCodecPrivate(head.data(), head.size());
            a->set_codec_delay(static_cast<std::uint64_t>(pre_skip_) * ns / opus_rate);
            a->set_seek_pre_roll(80'000'000);
        }
        // In time order (the sound before a picture at the same time).
        std::ranges::stable_sort(packets_, [](const Packet& a, const Packet& b) { return a.time != b.time ? a.time < b.time : a.audio > b.audio; });
        for (const Packet& p : packets_) {
            mkvmuxer::Frame f;
            if (!f.Init(p.data.data(), p.data.size())) return std::unexpected("libwebm: a frame");
            f.set_track_number(p.audio ? audio : video);
            f.set_timestamp(p.time);
            f.set_is_key(p.key);
            if (p.discard) f.set_discard_padding(static_cast<std::int64_t>(p.discard));
            if (!segment.AddGenericFrame(&f)) return std::unexpected("libwebm: cannot add a frame");
        }
        segment.set_duration(format_.seconds() * 1000.0);  // in the segment's timecode scale: milliseconds
        if (!segment.Finalize()) return std::unexpected("libwebm: cannot finish the file");
        release();
        return out_->close(*file);
    }

    void release() {
        if (vpx_open_) vpx_codec_destroy(&vpx_);
        vpx_open_ = false;
        if (opus_) opus_encoder_destroy(opus_);
        opus_ = nullptr;
    }

    std::string         name_;
    WebmOptions         options_;
    Format              format_;
    Output*             out_ = nullptr;
    vpx_codec_ctx_t     vpx_{};
    bool                vpx_open_ = false;
    vpx_enc_deadline_t  deadline_ = 0;
    OpusEncoder*        opus_ = nullptr;
    std::int32_t        pre_skip_ = 0;
    std::vector<float>  pending_;
    std::int64_t        sound_packets_ = 0, samples_in_ = 0;
    std::vector<Packet> packets_;
    Yuv420              picture_;
};

class Reader final : public mkvparser::IMkvReader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}
    int Read(long long pos, long len, unsigned char* buf) override {
        if (pos < 0 || len < 0 || static_cast<std::size_t>(pos + len) > bytes_.size()) return -1;
        std::memcpy(buf, bytes_.data() + pos, static_cast<std::size_t>(len));
        return 0;
    }
    int Length(long long* total, long long* available) override {
        if (total) *total = static_cast<long long>(bytes_.size());
        if (available) *available = static_cast<long long>(bytes_.size());
        return 0;
    }

private:
    std::span<const std::byte> bytes_;
};

}  // namespace

std::unique_ptr<Encoder> webm_encoder(std::string name, WebmOptions options) { return std::make_unique<WebmEncoder>(std::move(name), options); }

std::expected<DecodedWebm, std::string> decode_webm(std::span<const std::byte> file) {
    Reader reader(file);
    long long pos = 0;
    mkvparser::EBMLHeader header;
    if (header.Parse(&reader, pos) != 0) return std::unexpected("not a WebM file (no EBML header)");
    mkvparser::Segment* raw = nullptr;
    if (mkvparser::Segment::CreateInstance(&reader, pos, raw) != 0 || !raw) return std::unexpected("not a WebM file (no segment)");
    std::unique_ptr<mkvparser::Segment> segment(raw);
    if (segment->Load() < 0) return std::unexpected("the WebM file does not load");
    DecodedWebm d;
    d.duration_ns = segment->GetDuration();
    const mkvparser::Tracks* tracks = segment->GetTracks();
    const mkvparser::VideoTrack* video = nullptr;
    const mkvparser::AudioTrack* audio = nullptr;
    for (unsigned long i = 0; tracks && i < tracks->GetTracksCount(); ++i) {
        const mkvparser::Track* t = tracks->GetTrackByIndex(i);
        if (t && t->GetType() == mkvparser::Track::kVideo && !video) video = static_cast<const mkvparser::VideoTrack*>(t);
        if (t && t->GetType() == mkvparser::Track::kAudio && !audio) audio = static_cast<const mkvparser::AudioTrack*>(t);
    }
    if (!video) return std::unexpected("the WebM file has no video track");
    d.video_codec = video->GetCodecId() ? video->GetCodecId() : "";
    if (d.video_codec != "V_VP9") return std::unexpected(std::format("video `{}`: only VP9 is read", d.video_codec));
    d.width = static_cast<std::int32_t>(video->GetWidth());
    d.height = static_cast<std::int32_t>(video->GetHeight());
    if (const mkvparser::Colour* c = video->GetColour()) {
        d.bt709 = c->matrix_coefficients == mkvmuxer::Colour::kBt709;
        d.limited_range = c->range == mkvmuxer::Colour::kBroadcastRange;
    }

    vpx_codec_dec_cfg_t vcfg{};
    vpx_codec_ctx_t vctx;
    if (vm::vpx_codec_dec_init(&vctx, vpx_codec_vp9_dx(), &vcfg, 0) != VPX_CODEC_OK) return std::unexpected("libvpx: no VP9 decoder");
    OpusDecoder* opus = nullptr;
    std::int64_t pre_skip = 0, discard = 0;
    if (audio) {
        d.audio_codec = audio->GetCodecId() ? audio->GetCodecId() : "";
        if (d.audio_codec != "A_OPUS") {
            vpx_codec_destroy(&vctx);
            return std::unexpected(std::format("audio `{}`: only Opus is read", d.audio_codec));
        }
        d.rate = opus_rate;
        d.channels = static_cast<std::int32_t>(audio->GetChannels());
        int error = 0;
        opus = opus_decoder_create(opus_rate, d.channels, &error);
        if (!opus) {
            vpx_codec_destroy(&vctx);
            return std::unexpected(std::format("libopus: {}", opus_strerror(error)));
        }
        pre_skip = static_cast<std::int64_t>((audio->GetCodecDelay() * opus_rate + ns / 2) / ns);
    }
    std::string problem;
    std::vector<float> pcm(static_cast<std::size_t>(5760 * std::max(1, d.channels)));
    for (const mkvparser::Cluster* c = segment->GetFirst(); c && !c->EOS() && problem.empty(); c = segment->GetNext(c)) {
        const mkvparser::BlockEntry* e = nullptr;
        if (c->GetFirst(e) != 0) break;
        while (e && !e->EOS() && problem.empty()) {
            const mkvparser::Block* b = e->GetBlock();
            const bool is_video = b->GetTrackNumber() == video->GetNumber();
            const bool is_audio = audio && b->GetTrackNumber() == audio->GetNumber();
            for (int k = 0; k < b->GetFrameCount() && (is_video || is_audio); ++k) {
                const mkvparser::Block::Frame& f = b->GetFrame(k);
                std::vector<unsigned char> data(static_cast<std::size_t>(f.len));
                if (f.Read(&reader, data.data()) != 0) {
                    problem = "a frame cannot be read";
                    break;
                }
                if (is_video) {
                    if (vpx_codec_decode(&vctx, data.data(), static_cast<unsigned>(data.size()), nullptr, 0) != VPX_CODEC_OK) {
                        problem = std::format("libvpx: {}", vpx_codec_error(&vctx));
                        break;
                    }
                    vpx_codec_iter_t it = nullptr;
                    while (vpx_image_t* img = vpx_codec_get_frame(&vctx, &it)) {
                        Yuv420 y;
                        y.width = static_cast<std::int32_t>(img->d_w);
                        y.height = static_cast<std::int32_t>(img->d_h);
                        for (int plane = 0; plane < 3; ++plane) {
                            auto& dst = plane == 0 ? y.y : plane == 1 ? y.u : y.v;
                            const std::int32_t w = plane ? y.chroma_width() : y.width, h = plane ? y.chroma_height() : y.height;
                            dst.resize(static_cast<std::size_t>(w) * h);
                            for (std::int32_t r = 0; r < h; ++r)
                                std::memcpy(dst.data() + static_cast<std::size_t>(r) * w, img->planes[plane] + static_cast<std::ptrdiff_t>(r) * img->stride[plane], static_cast<std::size_t>(w));
                        }
                        d.frames.push_back(std::move(y));
                        d.frame_ns.push_back(b->GetTime(c));
                    }
                } else {
                    const int n = opus_decode_float(opus, data.data(), static_cast<opus_int32>(data.size()), pcm.data(), 5760, 0);
                    if (n < 0) {
                        problem = std::format("libopus: {}", opus_strerror(n));
                        break;
                    }
                    d.samples.insert(d.samples.end(), pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(n) * d.channels);
                    if (const long long pad = b->GetDiscardPadding(); pad > 0) discard = (pad * opus_rate + ns / 2) / ns;
                }
            }
            if (c->GetNext(e, e) != 0) break;
        }
    }
    vpx_codec_destroy(&vctx);
    if (opus) opus_decoder_destroy(opus);
    if (!problem.empty()) return std::unexpected(problem);
    if (audio) {
        const auto ch = static_cast<std::size_t>(d.channels);
        const auto total = d.samples.size() / ch;
        const auto keep = static_cast<std::size_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(total) - pre_skip - discard));
        d.samples.erase(d.samples.begin(), d.samples.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(total, static_cast<std::size_t>(pre_skip)) * ch));
        d.samples.resize(keep * ch);
    }
    return d;
}

}  // namespace omui::video
