#include "prologue_translation.h"

#include <cstring>
#include <string_view>

namespace {

struct Entry {
    const char* resource;
    size_t index;
    const char* spanish;
    const char* portuguese;
};

const Entry kPrologue[] = {
#include "prologue_translation_table.inc"
};

static_assert(sizeof(kPrologue) / sizeof(*kPrologue) == 41, "the prologue map has 41 records");

const char* const kResources[] = {"a0_2_1", "b0_2_1", "c0_2_1", "d0_2_1", "e0_2_1", "f0_1_0"};

int g_active = 0;

std::string_view ResourceOf(std::string_view resource) {
    const size_t slash = resource.rfind('/');
    const size_t dot = resource.rfind('.');
    const size_t begin = slash == std::string_view::npos ? 0 : slash + 1;
    const size_t end = dot == std::string_view::npos || dot < begin ? resource.size() : dot;
    return resource.substr(begin, end - begin);
}

bool ResourceMatches(std::string_view resource, std::string_view key) {
    if (resource.size() != key.size()) return false;
    for (size_t i = 0; i < key.size(); ++i) {
        char a = resource[i];
        const char b = key[i];
        if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

} // namespace

void osl_prologue_set_active(int active) { g_active = active ? 1 : 0; }
int osl_prologue_active(void) { return g_active; }
size_t osl_prologue_record_count(void) { return sizeof(kPrologue) / sizeof(*kPrologue); }
size_t osl_prologue_book_count(void) { return sizeof(kResources) / sizeof(*kResources); }

const char* osl_prologue_translate(const char* resource, size_t index, const char* text, size_t length) {
    if (!g_active || !resource || !text) return text;
    const size_t n = length == static_cast<size_t>(-1) ? std::strlen(text) : length;
    const std::string_view wanted = ResourceOf(resource);
    const std::string_view source(text, n);
    for (const Entry& entry : kPrologue) {
        if (entry.index != index || !ResourceMatches(wanted, entry.resource)) continue;
        return std::string_view(entry.spanish) == source ? entry.portuguese : text;
    }
    return text;
}

int osl_prologue_is_resource(const char* resource) {
    if (!resource) return 0;
    const std::string_view wanted = ResourceOf(resource);
    for (const char* key : kResources)
        if (ResourceMatches(wanted, key)) return 1;
    return 0;
}
