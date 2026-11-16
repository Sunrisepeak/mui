// gzip through zlib's module wrap (D-83: a wrapped C library only in a native/ unit).
module omui.devtools.cdp;

import std;
import mcpplibs.mwrap.zlib;

namespace omui::devtools::cdp {

namespace zlib = mcpplibs::mwrap::zlib::macros;

std::string gzip(std::string_view bytes, int level) {
    z_stream stream{};
    // windowBits 15 + 16: a gzip member; memLevel 8, the default strategy (node's zlib.gzipSync).
    if (deflateInit2_(&stream, level, zlib::Z_DEFLATED, zlib::MAX_WBITS + 16, 8,
                      zlib::Z_DEFAULT_STRATEGY, zlib::ZLIB_VERSION,
                      static_cast<int>(sizeof(z_stream))) != zlib::Z_OK)
        return {};
    std::string out(deflateBound(&stream, static_cast<uLong>(bytes.size())) + 32, '\0');
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));
    stream.avail_in = static_cast<uInt>(bytes.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    const int status = deflate(&stream, zlib::Z_FINISH);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return status == zlib::Z_STREAM_END ? out : std::string{};
}

std::expected<std::string, std::string> gunzip(std::string_view bytes) {
    z_stream stream{};
    if (inflateInit2_(&stream, zlib::MAX_WBITS + 16, zlib::ZLIB_VERSION,
                      static_cast<int>(sizeof(z_stream))) != zlib::Z_OK)
        return std::unexpected("inflate did not start");
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));
    stream.avail_in = static_cast<uInt>(bytes.size());
    std::string out;
    std::array<char, 65536> chunk{};
    int status = zlib::Z_OK;
    while (status == zlib::Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        status = inflate(&stream, zlib::Z_NO_FLUSH);
        out.append(chunk.data(), chunk.size() - stream.avail_out);
        if (status == zlib::Z_BUF_ERROR && stream.avail_in == 0)
            break;
    }
    inflateEnd(&stream);
    if (status != zlib::Z_STREAM_END)
        return std::unexpected(std::format("not a whole gzip member (zlib {})", status));
    return out;
}

}  // namespace omui::devtools::cdp
