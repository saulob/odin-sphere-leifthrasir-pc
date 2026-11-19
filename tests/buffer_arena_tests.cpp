// Vulkan buffer arena tests: sparse and fully resident layouts, BDA page-table entries and the fully resident migration
// (the same ResidentArenaFor the buffer cache runs) on fake arenas, plus the sources that must agree. No Vulkan device.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

// The vendored interval_set.h compares signed and unsigned sizes.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "video_core/buffer_cache/arena_layout.h"
#pragma GCC diagnostic pop

namespace {

using namespace VideoCore::ArenaLayout;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition)                                                                                       \
    do {                                                                                                       \
        ++g_checks;                                                                                            \
        if (!(condition)) {                                                                                    \
            ++g_failures;                                                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
        }                                                                                                      \
    } while (0)

std::string ReadAll(const std::string& relative) {
    std::ifstream file(std::string(OSL_SOURCE_DIR) + "/" + relative, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// Body of the function whose definition starts with `signature`, up to its closing brace at column 0.
std::string Body(const std::string& source, const std::string& signature) {
    const auto start = source.find(signature);
    if (start == std::string::npos) {
        return {};
    }
    const auto end = source.find("\n}\n", start);
    return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

bool Has(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

constexpr u64 GiB = u64{1} << 30;
constexpr u64 MiB = u64{1} << 20;

// The sparse page sizing the layout must keep.
u64 OriginalSparsePageBits(u64 max_buffer_size) {
    u64 bits = 32;
    while (bits > 30 && (u64{2} << bits) > max_buffer_size) {
        --bits;
    }
    return bits;
}

u64 OriginalSparseBlockSize(u64 alignment) {
    return Common::AlignUp(std::max<u64>(alignment, 16_KB), alignment);
}

void TestSparseLayoutUnchanged() {
    for (const u64 max_buffer : {GiB, 2 * GiB, 3 * GiB, 4 * GiB, 8 * GiB, 16 * GiB, u64{1} << 40, ~u64{0}}) {
        for (const u64 max_allocation : {GiB, 4 * GiB, ~u64{0}}) {
            CHECK(PageBits(true, max_buffer, max_allocation) == OriginalSparsePageBits(max_buffer));
        }
    }
    CHECK(PageBits(true, ~u64{0}, ~u64{0}) == 32);
    CHECK(PageBits(true, 2 * GiB, ~u64{0}) == 30);
    for (u64 alignment = 1_KB; alignment <= 2_MB; alignment <<= 1) {
        CHECK(BlockSize(true, alignment) == OriginalSparseBlockSize(alignment));
    }
    CHECK(BlockSize(true, 64_KB) == 64_KB);
    CHECK(NumPages(32) == 256);
    CHECK(NumPages(30) == 1024);
}

void TestResidentLayout() {
    CHECK(PageBits(false, ~u64{0}, ~u64{0}) == 24);
    CHECK(PageBits(false, GiB, GiB) == 24);
    CHECK(PageBits(false, 4 * GiB, 2 * GiB) == 24);
    CHECK(PageBits(false, 8 * MiB, GiB) == 23);
    CHECK(PageBits(false, GiB, 4 * MiB) == 22);
    CHECK(PageBits(false, 64_KB, 64_KB) == MIN_RESIDENT_PAGE_BITS);
    CHECK(NumPages(24) == 65536);
    for (u64 alignment = 1; alignment <= 64_KB; alignment <<= 1) {
        CHECK(BlockSize(false, alignment) == 64_KB);
    }
    CHECK(BlockSize(false, 256_KB) == 256_KB);
}

// The page table covers the whole 40-bit address space whatever the arena page size.
void TestBdaTableSize() {
    for (u64 block_shift = 14; block_shift <= 20; ++block_shift) {
        for (u64 page_bits = 20; page_bits <= 32; ++page_bits) {
            const u64 blocks_per_page = u64{1} << (page_bits - block_shift);
            CHECK(blocks_per_page * NumPages(page_bits) * sizeof(u64) == BdaTableBytes(block_shift));
            // FaultManager: one bit per block.
            CHECK(blocks_per_page * NumPages(page_bits) / 8 == BdaTableBytes(block_shift) / 64);
        }
    }
    CHECK(BdaTableBytes(16) == 128 * MiB);
    CHECK(BdaTableBytes(14) == 512 * MiB);
    std::printf("BDA page table: %llu MiB with 64 KiB blocks (sparse at 64 KiB alignment and fully resident), "
                "%llu MiB with 16 KiB blocks; fault bitmap %llu MiB\n",
                static_cast<unsigned long long>(BdaTableBytes(16) / MiB),
                static_cast<unsigned long long>(BdaTableBytes(14) / MiB),
                static_cast<unsigned long long>(BdaTableBytes(16) / 64 / MiB));
}

void TestBdaArithmetic() {
    constexpr u64 shift = 16;
    const u64 cpu = u64{10} << 24;
    const u64 device = 0x7f00'0000'0000;
    CHECK(BlockDeviceAddress(device, cpu, cpu >> shift, shift) == device);
    CHECK(BlockDeviceAddress(device, cpu, (cpu >> shift) + 3, shift) == device + 3 * 64_KB);
    const u64 guest = cpu + 5 * 64_KB + 0x1234;
    // The shader adds the offset inside the block to the entry.
    CHECK(BlockDeviceAddress(device, cpu, guest >> shift, shift) + (guest & (64_KB - 1)) == device + (guest - cpu));
}

struct FakeArena {
    u64 cpu_addr;
    u64 size;
    u64 device_address;
    mutable std::map<u64, u8> bytes; // written offsets only
    u64 CpuAddr() const {
        return cpu_addr;
    }
    u64 SizeBytes() const {
        return size;
    }
};

// Like the buffer cache's fully resident Backing: adjacent ranges stay separate.
struct ResidentRange : Interval {
    constexpr bool CanMergeWith(const ResidentRange&) const noexcept {
        return false;
    }
    constexpr ResidentRange SubRange(u64 a, u64 b) const noexcept {
        return {{a, b}};
    }
};

// The fully resident buffer cache on fake arenas: GetResidentArena + EnsureResident without Vulkan.
struct World {
    static constexpr u64 PageBitsValue = RESIDENT_PAGE_BITS;
    static constexpr u64 BlockShift = 16;
    static constexpr u64 Mask = (u64{1} << BlockShift) - 1;

    std::list<FakeArena> arenas;
    std::vector<const FakeArena*> address_space = std::vector<const FakeArena*>(NumPages(PageBitsValue));
    IntervalList<ResidentRange> resident;
    std::unordered_map<u64, u64> bda;
    std::vector<const FakeArena*> retired;
    u64 next_device_address = 0x7000'0000'0000;
    u64 migrations = 0;
    u64 copied_bytes = 0;

    const FakeArena* Create(u64 cpu_addr, u64 size) {
        arenas.push_back({cpu_addr, size, next_device_address, {}});
        next_device_address += size + GiB;
        return &arenas.back();
    }
    void Copy(const FakeArena& from, const FakeArena& to, u64 start, u64 end) {
        const u64 first = (start << BlockShift) - from.cpu_addr;
        const u64 last = (end << BlockShift) - from.cpu_addr;
        for (auto it = from.bytes.lower_bound(first); it != from.bytes.end() && it->first < last; ++it) {
            to.bytes[it->first + from.cpu_addr - to.cpu_addr] = it->second;
        }
        copied_bytes += last - first;
    }
    void Publish(const FakeArena& arena, u64 start, u64 end) {
        for (u64 block = start; block < end; ++block) {
            bda[block] = BlockDeviceAddress(arena.device_address, arena.cpu_addr, block, BlockShift);
        }
    }
    void Retire(const FakeArena& arena) {
        retired.push_back(&arena);
    }

    const FakeArena* Obtain(u64 addr, u64 size) {
        const u64 first_block = addr >> BlockShift;
        const u64 last_block = (addr + size - 1) >> BlockShift;
        const size_t retired_before = retired.size();
        const FakeArena* arena = ResidentArenaFor<FakeArena>(address_space, resident, PageBitsValue, BlockShift,
                                                             first_block, last_block, *this);
        migrations += retired.size() != retired_before;
        std::vector<Interval> gaps;
        resident.ForEachGap(first_block, last_block + 1, [&](u64 start, u64 end) { gaps.push_back({start, end}); });
        for (const auto& gap : gaps) {
            resident.Add(ResidentRange{gap});
            Publish(*arena, gap.start, gap.end);
        }
        return arena;
    }
    void Write(u64 addr, u8 value) {
        const FakeArena* arena = Obtain(addr, 1);
        arena->bytes[addr - arena->cpu_addr] = value;
    }
    // What a shader reads through the page table.
    const FakeArena* BdaOwner(u64 addr) const {
        const auto entry = bda.find(addr >> BlockShift);
        if (entry == bda.end()) {
            return nullptr;
        }
        const u64 device = entry->second + (addr & Mask);
        for (const auto& arena : arenas) {
            if (device >= arena.device_address && device < arena.device_address + arena.size) {
                return &arena;
            }
        }
        return nullptr;
    }
    int BdaRead(u64 addr) const {
        const FakeArena* arena = BdaOwner(addr);
        if (!arena) {
            return -1;
        }
        const u64 offset = bda.at(addr >> BlockShift) + (addr & Mask) - arena->device_address;
        const auto byte = arena->bytes.find(offset);
        return byte == arena->bytes.end() ? 0 : byte->second;
    }
    bool IsRetired(const FakeArena* arena) const {
        return std::find(retired.begin(), retired.end(), arena) != retired.end();
    }
    // Every resident block of [first_page, end_page) has its entry inside `arena` at the right offset.
    bool PublishedIn(const FakeArena* arena, u64 first_page, u64 end_page) const {
        constexpr u64 shift = PageBitsValue - BlockShift;
        bool ok = true;
        u64 blocks = 0;
        resident.ForEachInRange(first_page << shift, end_page << shift, [&](const Interval& range) {
            for (u64 block = std::max(range.start, first_page << shift);
                 block < std::min(range.end, end_page << shift); ++block) {
                const auto entry = bda.find(block);
                ok &= entry != bda.end() &&
                      entry->second == arena->device_address + (block << BlockShift) - arena->cpu_addr;
                ++blocks;
            }
        });
        return ok && blocks > 0;
    }
    bool Owns(const FakeArena* arena, u64 first_page, u64 end_page) const {
        for (u64 page = first_page; page < end_page; ++page) {
            if (address_space[page] != arena) {
                return false;
            }
        }
        return true;
    }
};

constexpr u64 Page(u64 index) {
    return index << World::PageBitsValue;
}

void TestFirstResidency() {
    World world;
    const FakeArena* a = world.Obtain(Page(10) + 0x100, 0x100);
    CHECK(world.arenas.size() == 1 && a->cpu_addr == Page(10) && a->size == 16 * MiB);
    CHECK(world.Owns(a, 10, 11));
    CHECK(world.address_space[9] == nullptr && world.address_space[11] == nullptr);
    CHECK(world.PublishedIn(a, 10, 11));
    CHECK(world.bda.size() == 1);
    CHECK(world.resident.Contains(Page(10) >> World::BlockShift));
    CHECK(!world.resident.Contains((Page(10) >> World::BlockShift) + 1));
    CHECK(world.Obtain(Page(10) + 8 * MiB, 64) == a);
    CHECK(world.bda.size() == 2 && world.arenas.size() == 1 && world.migrations == 0);
    // A request spanning fresh pages gets one arena covering them, nothing to migrate.
    const FakeArena* wide = world.Obtain(Page(20) + 0x10, 40 * MiB);
    CHECK(wide->cpu_addr == Page(20) && wide->size == 48 * MiB && world.Owns(wide, 20, 23));
    CHECK(world.retired.empty() && world.migrations == 0 && world.copied_bytes == 0);
    CHECK(world.PublishedIn(wide, 20, 23));
}

void TestCrossArenaMigration() {
    World world;
    const FakeArena* a = world.Obtain(Page(10), 1);
    const FakeArena* b = world.Obtain(Page(11), 1);
    CHECK(a != b && world.arenas.size() == 2);
    const u64 a_addr = Page(10) + 0x300000 + 5;
    const u64 b_addr = Page(11) + 0x700000 + 9;
    world.Write(a_addr, 0xA1);
    world.Write(b_addr, 0xB1);
    world.Write(Page(11) - 3, 0xA9); // last block of A
    CHECK(world.BdaOwner(a_addr) == a && world.BdaOwner(b_addr) == b);

    const FakeArena* ab = world.Obtain(Page(11) - 0x80, 0x100);
    CHECK(world.arenas.size() == 3 && world.migrations == 1);
    CHECK(ab != a && ab != b && ab->cpu_addr == Page(10) && ab->size == 32 * MiB);
    CHECK(world.Owns(ab, 10, 12));
    CHECK(world.IsRetired(a) && world.IsRetired(b) && world.retired.size() == 2);
    // Bytes of both old arenas, at their guest offsets.
    CHECK(ab->bytes.at(a_addr - Page(10)) == 0xA1);
    CHECK(ab->bytes.at(b_addr - Page(10)) == 0xB1);
    CHECK(ab->bytes.at(Page(11) - 3 - Page(10)) == 0xA9);
    // Only resident blocks were copied.
    CHECK(world.copied_bytes == 5 * 64_KB);
    // Every moved block, not only the new ones, points into the new arena.
    CHECK(world.PublishedIn(ab, 10, 12));
    CHECK(world.BdaOwner(a_addr) == ab && world.BdaOwner(b_addr) == ab);
    CHECK(world.BdaRead(a_addr) == 0xA1 && world.BdaRead(b_addr) == 0xB1);
    CHECK(world.BdaRead(Page(11) - 3) == 0xA9);
}

// A one-time copy is not enough: a later write lands in the new arena, and the page table must lead there.
void TestWriteAfterMigration() {
    World world;
    const FakeArena* a = world.Obtain(Page(10), 1);
    const FakeArena* b = world.Obtain(Page(11), 1);
    const u64 a_addr = Page(10) + 0x1234;
    const u64 b_addr = Page(11) + 0x4321;
    world.Write(a_addr, 0x11);
    world.Write(b_addr, 0x22);
    const FakeArena* ab = world.Obtain(Page(11) - 16, 32);
    CHECK(ab != a && ab != b);

    world.Write(a_addr, 0x33);
    world.Write(b_addr, 0x44);
    CHECK(ab->bytes.at(a_addr - ab->cpu_addr) == 0x33);
    CHECK(ab->bytes.at(b_addr - ab->cpu_addr) == 0x44);
    CHECK(world.BdaOwner(a_addr) == ab && world.BdaRead(a_addr) == 0x33);
    CHECK(world.BdaOwner(b_addr) == ab && world.BdaRead(b_addr) == 0x44);
    // The old arenas keep the stale bytes, and nothing resolves to them any more.
    CHECK(a->bytes.at(a_addr - a->cpu_addr) == 0x11);
    CHECK(b->bytes.at(b_addr - b->cpu_addr) == 0x22);
    bool reaches_old = false;
    for (const auto& [block, device] : world.bda) {
        reaches_old |= world.BdaOwner(block << World::BlockShift) == a ||
                       world.BdaOwner(block << World::BlockShift) == b;
    }
    CHECK(!reaches_old);
}

void TestRepeatedMigration() {
    World world;
    const FakeArena* a = world.Obtain(Page(10) + 0x10, 1);
    const FakeArena* b = world.Obtain(Page(11) + 0x10, 1);
    world.Write(Page(10) + 0x20, 1);
    world.Write(Page(11) + 0x20, 2);
    const FakeArena* ab = world.Obtain(Page(11) - 1, 2);
    const FakeArena* c = world.Obtain(Page(12) + 0x10, 1);
    world.Write(Page(12) + 0x20, 3);
    CHECK(c != ab && world.Owns(ab, 10, 12) && world.Owns(c, 12, 13));

    const FakeArena* abc = world.Obtain(Page(12) - 1, 2);
    CHECK(world.migrations == 2 && abc->cpu_addr == Page(10) && abc->size == 48 * MiB);
    CHECK(world.Owns(abc, 10, 13));
    CHECK(world.IsRetired(a) && world.IsRetired(b) && world.IsRetired(ab) && world.IsRetired(c));
    CHECK(!world.IsRetired(abc));
    CHECK(world.BdaRead(Page(10) + 0x20) == 1 && world.BdaRead(Page(11) + 0x20) == 2 &&
          world.BdaRead(Page(12) + 0x20) == 3);
    CHECK(world.PublishedIn(abc, 10, 13));
    world.Write(Page(11) + 0x20, 4);
    CHECK(world.BdaOwner(Page(11) + 0x20) == abc && world.BdaRead(Page(11) + 0x20) == 4);

    // Logical residency survives the moves: same blocks, no new gaps.
    const u64 resident_blocks = [&] {
        u64 count = 0;
        for (const auto& range : world.resident) {
            count += range.end - range.start;
        }
        return count;
    }();
    CHECK(resident_blocks == 5);
    u64 gaps = 0;
    world.resident.ForEachGap(Page(10) >> World::BlockShift, (Page(10) + 0x30) >> World::BlockShift,
                              [&](u64, u64) { ++gaps; });
    CHECK(gaps == 0);

    // A request over an owned arena and fresh pages on both sides (the fault path merges any span).
    world.Write(Page(14) + 7, 5);
    const FakeArena* wide = world.Obtain(Page(9) + 0x100, 6 * 16 * MiB);
    CHECK(world.migrations == 3 && wide->cpu_addr == Page(9) && wide->size == 7 * 16 * MiB);
    CHECK(world.Owns(wide, 9, 16));
    CHECK(world.BdaRead(Page(10) + 0x20) == 1 && world.BdaRead(Page(11) + 0x20) == 4 &&
          world.BdaRead(Page(12) + 0x20) == 3 && world.BdaRead(Page(14) + 7) == 5);
    CHECK(world.PublishedIn(wide, 9, 16));
    CHECK(world.Obtain(Page(15), 16 * MiB) == wide && world.migrations == 3);

    // Live memory once the retired arenas are freed: only the current owners.
    u64 live = 0;
    for (const auto& arena : world.arenas) {
        live += world.IsRetired(&arena) ? 0 : arena.size;
    }
    CHECK(live == 7 * 16 * MiB);
}

// A binding of one command, resolved by ObtainBuffer and recorded later (descriptor, vertex, index or indirect).
struct Binding {
    const FakeArena* arena;
    u64 offset;
    u64 guest;
};

Binding Bind(World& world, u64 addr, u64 size) {
    const FakeArena* arena = world.Obtain(addr, size);
    return {arena, addr - arena->cpu_addr, addr};
}

// Rasterizer::RefreshArenaBindings: every binding of the command, after its last ObtainBuffer.
void Refresh(const World& world, std::initializer_list<Binding*> bindings) {
    for (Binding* binding : bindings) {
        std::tie(binding->arena, binding->offset) = CurrentBinding<FakeArena>(
            world.address_space, world.retired, World::PageBitsValue, binding->arena, binding->offset);
    }
}

void GpuWrite(const Binding& binding, u64 at, u8 value) {
    binding.arena->bytes[binding.offset + at] = value;
}

int GpuRead(const Binding& binding, u64 at) {
    const auto byte = binding.arena->bytes.find(binding.offset + at);
    return byte == binding.arena->bytes.end() ? 0 : byte->second;
}

const FakeArena* Owner(const World& world, u64 addr) {
    return world.address_space[addr >> World::PageBitsValue];
}

bool Current(const World& world, const Binding& binding) {
    return binding.arena == Owner(world, binding.guest) && binding.arena->cpu_addr + binding.offset == binding.guest;
}

// Writable binding A, then binding B migrates A within the same command.
void TestStaleWriteBinding() {
    World world;
    const u64 a_addr = Page(10) + 0x100;
    world.Write(a_addr, 0x11);
    Binding a = Bind(world, a_addr, 0x100);
    const FakeArena* old_a = a.arena;
    Binding b = Bind(world, Page(11) - 0x80, 0x100);
    CHECK(world.IsRetired(old_a));
    Refresh(world, {&a, &b});

    GpuWrite(a, 0, 0x5A);
    CHECK(Current(world, a) && Current(world, b));
    CHECK(Owner(world, a_addr)->bytes.count(a_addr - Owner(world, a_addr)->cpu_addr) &&
          Owner(world, a_addr)->bytes.at(a_addr - Owner(world, a_addr)->cpu_addr) == 0x5A);
    CHECK(world.BdaOwner(a_addr) == a.arena && world.BdaRead(a_addr) == 0x5A);
    CHECK(old_a->bytes.at(a_addr - old_a->cpu_addr) == 0x11);
}

// A read binding resolved before the migration sees data written to the current arena after it.
void TestStaleReadBinding() {
    World world;
    const u64 r_addr = Page(10) + 0x200;
    world.Write(r_addr, 0x21);
    Binding r = Bind(world, r_addr, 0x10);
    Binding crossing = Bind(world, Page(11) - 0x40, 0x80);
    world.Write(r_addr, 0x77);
    Refresh(world, {&r, &crossing});
    CHECK(Current(world, r));
    CHECK(GpuRead(r, 0) == 0x77 && world.BdaRead(r_addr) == 0x77);
}

void TestVertexAndIndexBindings() {
    World world;
    world.Write(Page(20) + 0x40, 0x31);
    world.Write(Page(21) + 0x40, 0x32);
    Binding vertex = Bind(world, Page(20) + 0x40, 0x100);
    Binding index = Bind(world, Page(21) + 0x40, 0x100);
    Binding storage = Bind(world, Page(21) - 0x10, 0x20);
    Refresh(world, {&vertex, &index, &storage});
    CHECK(Current(world, vertex) && Current(world, index) && Current(world, storage));
    CHECK(vertex.arena == index.arena && index.arena == storage.arena);
    CHECK(GpuRead(vertex, 0) == 0x31 && GpuRead(index, 0) == 0x32);
}

// Two writable storage ranges, and a third binding that migrates both of their arenas.
void TestTwoWritableBindings() {
    World world;
    Binding first = Bind(world, Page(10) + 0x1000, 0x100);
    Binding second = Bind(world, Page(11) + 0x2000, 0x100);
    Binding third = Bind(world, Page(11) - 0x10, 0x20);
    Refresh(world, {&first, &second, &third});
    GpuWrite(first, 0, 0x41);
    GpuWrite(second, 0, 0x42);
    CHECK(Current(world, first) && Current(world, second) && Current(world, third));
    CHECK(world.BdaRead(first.guest) == 0x41 && world.BdaRead(second.guest) == 0x42);
    CHECK(world.BdaOwner(first.guest) == first.arena && world.BdaOwner(second.guest) == second.arena);
}

void TestReadAndWriteBindings() {
    World world;
    world.Write(Page(10) + 0x3000, 0x51);
    Binding read = Bind(world, Page(10) + 0x3000, 0x10);
    Binding write = Bind(world, Page(11) + 0x3000, 0x10);
    Binding crossing = Bind(world, Page(11) - 0x10, 0x20);
    Refresh(world, {&read, &write, &crossing});
    GpuWrite(write, 0, 0x52);
    CHECK(Current(world, read) && Current(world, write));
    CHECK(GpuRead(read, 0) == 0x51 && world.BdaRead(write.guest) == 0x52);
}

// Two migrations between resolving a binding and recording it.
void TestRepeatedMigrationBeforeRecording() {
    World world;
    Binding a = Bind(world, Page(30) + 0x500, 0x10);
    Binding c = Bind(world, Page(32) + 0x500, 0x10);
    Binding ab = Bind(world, Page(31) - 0x10, 0x20);
    Binding abc = Bind(world, Page(32) - 0x10, 0x20);
    CHECK(world.migrations == 2);
    Refresh(world, {&a, &c, &ab, &abc});
    CHECK(Current(world, a) && Current(world, c) && Current(world, ab) && Current(world, abc));
    CHECK(a.arena == abc.arena && c.arena == abc.arena && a.arena->size == 48 * MiB);
    GpuWrite(a, 0, 0x61);
    GpuWrite(c, 0, 0x63);
    CHECK(world.BdaRead(a.guest) == 0x61 && world.BdaRead(c.guest) == 0x63);
}

// Bindings already current, and buffers that are not arenas (stream, GDS), stay as they are.
void TestCurrentBindingsUntouched() {
    World world;
    Binding a = Bind(world, Page(40) + 0x10, 0x10);
    Binding crossing = Bind(world, Page(41) - 0x10, 0x20);
    Binding after = Bind(world, Page(40) + 0x20, 0x10);
    const Binding after_before = after;
    const FakeArena stream{0, 128 * MiB, 0x1000, {}};
    Binding host{&stream, 0x40, 0x40};
    Refresh(world, {&a, &crossing, &after, &host});
    CHECK(after.arena == after_before.arena && after.offset == after_before.offset);
    CHECK(host.arena == &stream && host.offset == 0x40);
    CHECK(Current(world, a));
}

void TestSources() {
    const std::string instance = ReadAll("gpu/shadps4/video_core/renderer_vulkan/vk_instance.h");
    CHECK(Has(Body(instance, "    bool IsSparseSupported() const {"),
              "return features.sparseBinding && features.sparseResidencyBuffer;"));

    const std::string cache = ReadAll("gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp");
    const std::string header = ReadAll("gpu/shadps4/video_core/buffer_cache/buffer_cache.h");
    CHECK(Has(cache, "sparse_supported = instance.IsSparseSupported();"));
    CHECK(Has(cache, ".flags = sparse_supported ? vk::BufferCreateFlagBits::eSparseBinding |"));
    CHECK(Has(cache, "address_space.resize(num_arena_pages);"));
    CHECK(Has(cache, "\"Vulkan buffer cache: sparse buffers unavailable, using {} MiB fully resident \""));

    // Sparse devices keep the sparse arenas, binds and their submission.
    const std::string get_arena = Body(cache, "const Buffer* BufferCache::GetArena(");
    CHECK(Has(get_arena, "if (!sparse_supported) {\n        return GetResidentArena(first_block, last_block);"));
    CHECK(Has(get_arena, "num_pages << arena_page_bits, MemoryType::Sparse);"));
    CHECK(Has(get_arena, "return &arenas.emplace_back(instance, first_addr, total_size, MemoryType::Sparse);"));
    CHECK(Has(get_arena, "auto* bind = BindsForArena(new_arena);"));
    const std::string ensure = Body(cache, "void BufferCache::EnsureResident(");
    const auto resident_branch = ensure.find("if (!sparse_supported) {");
    const auto sparse_part = ensure.find("ArenaBinds* binds = BindsForArena(arena);");
    CHECK(resident_branch != std::string::npos && sparse_part != std::string::npos && resident_branch < sparse_part);
    const std::string resident_part = ensure.substr(resident_branch, ensure.find("return;", resident_branch) - resident_branch);
    CHECK(Has(resident_part, "PublishArenaBda(arena, new_ranges);"));
    CHECK(!Has(resident_part, "Bind") && !Has(resident_part, "allocateMemory"));
    CHECK(Has(ensure.substr(sparse_part), "PublishArenaBda(arena, new_ranges);"));
    CHECK(Has(Body(cache, "void BufferCache::SubmitPendingArenaBinds("), "instance.GetSparseQueue().bindSparse(sparse_info)"));
    CHECK(Has(header, "ArenaBinds* BindsForArena(const Buffer* arena) {\n        ASSERT(sparse_supported);"));

    // Fully resident arenas: DeviceLocal buffers, no sparse flags, no binds.
    const std::string resident = Body(cache, "const Buffer* BufferCache::GetResidentArena(");
    CHECK(Has(resident, "MemoryType::DeviceLocal"));
    CHECK(!Has(resident, "Sparse") && !Has(resident, "Bind"));
    CHECK(Has(resident, "PublishArenaBda(arena, {ops.published.data(), ops.published.size()});"));
    CHECK(!Has(Body(cache, "void BufferCache::FollowMigration("), "Bind"));
    const std::string buffer = ReadAll("gpu/shadps4/video_core/buffer_cache/buffer.cpp");
    CHECK(Has(buffer, "if (mem_type != MemoryType::Sparse) {"));
    CHECK(Has(Body(buffer, "void UniqueBuffer::Create("), "    } else {\n        buffer_ci.flags |=\n            vk::BufferCreateFlagBits::eSparseBinding"));

    // One BDA formula for both modes.
    CHECK(Has(Body(cache, "void BufferCache::PublishArenaBda("), "ArenaLayout::BlockDeviceAddress("));
    CHECK(!Has(cache, "arena->BufferDeviceAddress() + bind.resourceOffset"));

    // Retired arenas are freed once the GPU passes the tick, from the command list boundary.
    CHECK(Has(Body(cache, "void BufferCache::RetireMigratedArenas("), "scheduler.DeferOperation("));
    CHECK(Has(header, "std::list<Buffer> arenas;"));
    CHECK(Has(header, "std::vector<const Buffer*> address_space;"));
    const std::string rasterizer = ReadAll("gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp");
    CHECK(Has(Body(rasterizer, "void Rasterizer::OnSubmit("), "buffer_cache.RetireMigratedArenas();"));
    CHECK(!Has(cache, "vkDeviceWaitIdle") && !Has(cache, "waitIdle"));

    // Every command refreshes its bindings after its last ObtainBuffer and before recording them.
    CHECK(Has(Body(rasterizer, "bool Rasterizer::BindResources("), "bind_generation = buffer_cache.ArenaGeneration();"));
    const auto refreshed_between = [&](const std::string& body, const std::string& last_obtain,
                                       const std::string& first_record) {
        const auto obtain = body.rfind(last_obtain);
        const auto refresh = body.find("RefreshArenaBindings();");
        const auto record = body.find(first_record);
        return obtain != std::string::npos && refresh != std::string::npos && record != std::string::npos &&
               obtain < refresh && refresh < record;
    };
    const std::string draw = Body(rasterizer, "void Rasterizer::DrawRecord(");
    CHECK(refreshed_between(draw, "ResolveIndexBuffer(index_offset);", "EmitVertexBuffers();"));
    CHECK(refreshed_between(draw, "BindResources(pipeline)", "pipeline->BindResources(set_writes, push_data);"));
    const std::string indirect = Body(rasterizer, "void Rasterizer::DrawIndirectRecord(");
    CHECK(refreshed_between(indirect, "buffer_cache.ObtainBuffer(count_address, 4, false)", "EmitVertexBuffers();"));
    CHECK(Has(indirect, "buffer_cache.RefreshBinding(buffer, base);"));
    CHECK(Has(indirect, "buffer_cache.RefreshBinding(count_buffer, count_offset);"));
    const std::string dispatch = Body(rasterizer, "void Rasterizer::DispatchRecord(");
    CHECK(refreshed_between(dispatch, "BindResources(pipeline)", "pipeline->BindResources(set_writes, push_data);"));
    const std::string dispatch_indirect = Body(rasterizer, "void Rasterizer::DispatchIndirectRecord(");
    CHECK(refreshed_between(dispatch_indirect, "buffer_cache.ObtainBuffer(address + offset, size, false)",
                            "pipeline->BindResources(set_writes, push_data);"));
    CHECK(Has(dispatch_indirect, "buffer_cache.RefreshBinding(buffer, base);"));
    const std::string refresh = Body(rasterizer, "void Rasterizer::RefreshArenaBindings(");
    for (const char* binding : {"buffer_infos", "v.host_buffers[i], v.host_offsets[i]", "index_bind.handle",
                                "bound.buffer, bound.offset", "needs_barrier |="}) {
        CHECK(Has(refresh, binding));
    }
    CHECK(!Has(rasterizer, "BindVertexBuffers(") && !Has(rasterizer, "BindIndexBuffer("));
    const std::string dma = Body(rasterizer, "void Rasterizer::CopyBuffer(");
    CHECK(dma.find("buffer_cache.RefreshBinding(src_buffer, src_offset);") >
              dma.find("ObtainBuffer(dst, num_bytes, true, true)") &&
          dma.find("buffer_cache.RefreshBinding(src_buffer, src_offset);") < dma.find("runtime.CopyBuffer("));
    const std::string hle = ReadAll("gpu/shadps4/video_core/renderer_vulkan/vk_shader_hle.cpp");
    CHECK(hle.find("buffer_cache.RefreshBinding(src_buf, src_buf_offset);") > hle.find("dst_offset_max - dst_offset_min, true)") &&
          hle.find("buffer_cache.RefreshBinding(src_buf, src_buf_offset);") < hle.find("MultiCopy(rasterizer, src_buf"));
    CHECK(Has(Body(cache, "void BufferCache::RefreshBinding(const Buffer*& buffer"), "ArenaLayout::CurrentBinding<Buffer>("));
    CHECK(Has(Body(cache, "void BufferCache::RefreshBinding(vk::Buffer& handle"), "for (const Buffer* arena : retired_arenas)"));

    // Host renderer capability only: no game build, region or version gate.
    const std::string layout = ReadAll("gpu/shadps4/video_core/buffer_cache/arena_layout.h");
    for (const std::string& text : {cache, header, layout}) {
        for (const char* gate : {"CUSA", "GameBuild", "eboot", "title_id", "OSL_LANGUAGE", "osl_build"}) {
            CHECK(!Has(text, gate));
        }
    }
    // The OpenGL and Direct3D 11 backends do not use the Vulkan buffer cache.
    for (const char* path : {"gpu/shadps4/video_core/renderer_opengl/gl_rasterizer.cpp",
                             "gpu/shadps4/video_core/renderer_opengl/gl_rasterizer.h"}) {
        const std::string gl = ReadAll(path);
        CHECK(!gl.empty() && !Has(gl, "arena_layout.h") && !Has(gl, "RetireMigratedArenas"));
    }
}

} // namespace

int main() {
    TestSparseLayoutUnchanged();
    TestResidentLayout();
    TestBdaTableSize();
    TestBdaArithmetic();
    TestFirstResidency();
    TestCrossArenaMigration();
    TestWriteAfterMigration();
    TestRepeatedMigration();
    TestStaleWriteBinding();
    TestStaleReadBinding();
    TestVertexAndIndexBindings();
    TestTwoWritableBindings();
    TestReadAndWriteBindings();
    TestRepeatedMigrationBeforeRecording();
    TestCurrentBindingsUntouched();
    TestSources();
    std::printf("buffer arena tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
