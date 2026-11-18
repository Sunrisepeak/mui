// Reading an ELF program's sections and symbol table (B3): where a desktop program's bytes go, by owner.
module omui.gen.size;

import std;

namespace omui::gen {

namespace {

template <class T>
std::optional<T> at(std::span<const std::uint8_t> bytes, std::uint64_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) return std::nullopt;
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

struct Section {
    std::string   name;
    std::uint32_t type = 0;
    std::uint64_t flags = 0, offset = 0, size = 0;
    std::uint32_t link = 0;
};

constexpr std::uint32_t symbol_table = 2, no_bits = 8;
constexpr std::uint64_t allocated = 0x2, executable = 0x4;

}  // namespace

std::expected<SizeReport, std::string> elf_size(std::span<const std::uint8_t> elf, std::size_t top) {
    if (elf.size() < 64 || elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') return std::unexpected("not an ELF file");
    if (elf[4] != 2 || elf[5] != 1) return std::unexpected("not a 64-bit little-endian ELF file");
    const auto table = at<std::uint64_t>(elf, 0x28);
    const auto entry = at<std::uint16_t>(elf, 0x3a), count = at<std::uint16_t>(elf, 0x3c), names_at = at<std::uint16_t>(elf, 0x3e);
    if (!table || !entry || !count || !names_at || *entry < 64 || *names_at >= *count) return std::unexpected("its section headers cannot be read");
    std::vector<Section> sections(*count);
    for (std::uint16_t index = 0; index < *count; ++index) {
        const std::uint64_t header = *table + std::uint64_t{index} * *entry;
        const auto name = at<std::uint32_t>(elf, header), type = at<std::uint32_t>(elf, header + 4), link = at<std::uint32_t>(elf, header + 40);
        const auto flags = at<std::uint64_t>(elf, header + 8), offset = at<std::uint64_t>(elf, header + 24), size = at<std::uint64_t>(elf, header + 32);
        if (!name || !type || !link || !flags || !offset || !size) return std::unexpected(std::format("section header {} runs past the end of the file", index));
        sections[index] = {std::to_string(*name), *type, *flags, *offset, *size, *link};
    }
    const auto string_at = [&](const Section& strings, std::uint64_t at_offset) -> std::string_view {
        if (strings.type == no_bits || at_offset >= strings.size || strings.offset + strings.size > elf.size()) return {};
        const auto* begin = reinterpret_cast<const char*>(elf.data() + strings.offset + at_offset);
        const std::string_view rest(begin, static_cast<std::size_t>(strings.size - at_offset));
        return rest.substr(0, rest.find('\0'));
    };
    const Section names = sections[*names_at];
    for (auto& section : sections) section.name = std::string(string_at(names, std::stoull(section.name)));

    SizeReport report;
    report.total = elf.size();
    for (const auto& section : sections) {
        if (section.name.empty()) continue;
        report.sections.push_back({section.name, static_cast<std::size_t>(section.size), 0});
        if (!(section.flags & allocated) || section.type == no_bits) continue;  // .bss and .tbss: memory, not bytes in the file
        report.shipped += section.size;
        (section.flags & executable ? report.code : report.data) += section.size;
    }
    std::ranges::sort(report.sections, std::greater{}, &SizeEntry::bytes);

    std::map<std::string, SizeEntry> owners;
    std::vector<SizeEntry> each;
    for (const auto& section : sections) {
        if (section.type != symbol_table || section.link >= sections.size() || section.offset + section.size > elf.size()) continue;
        const Section& strings = sections[section.link];
        for (std::uint64_t symbol = section.offset; symbol + 24 <= section.offset + section.size; symbol += 24) {
            const auto name = at<std::uint32_t>(elf, symbol);
            const auto info = at<std::uint8_t>(elf, symbol + 4);
            const auto index = at<std::uint16_t>(elf, symbol + 6);
            const auto size = at<std::uint64_t>(elf, symbol + 16);
            if (!name || !info || !index || !size || *size == 0 || *index == 0 || *index >= sections.size()) continue;
            const int kind = *info & 0xf;  // 1 an object, 2 a function
            if (kind != 1 && kind != 2) continue;
            if (!(sections[*index].flags & allocated) || sections[*index].type == no_bits) continue;  // in memory, not in the file
            const std::string_view symbol_name = string_at(strings, *name);
            ++report.functions;
            if (!symbol_name.empty()) ++report.named;
            const std::string owner = symbol_name.empty() ? std::string("(no name)") : owner_of(symbol_name);
            auto& entry = owners[owner];
            entry.name = owner;
            entry.bytes += static_cast<std::size_t>(*size);
            ++entry.functions;
            each.push_back({std::string(symbol_name), static_cast<std::size_t>(*size), 1});
        }
    }
    for (auto& [owner, entry] : owners) report.owners.push_back(std::move(entry));
    std::ranges::sort(report.owners, std::greater{}, &SizeEntry::bytes);
    std::ranges::sort(each, std::greater{}, &SizeEntry::bytes);
    each.resize(std::min(each.size(), top));
    report.largest = std::move(each);
    return report;
}

std::expected<SizeReport, std::string> program_size(std::span<const std::uint8_t> program, std::size_t top) {
    if (program.size() >= 4 && program[0] == 0x7f && program[1] == 'E' && program[2] == 'L' && program[3] == 'F') return elf_size(program, top);
    return wasm_size(program, top);
}

}  // namespace omui::gen
