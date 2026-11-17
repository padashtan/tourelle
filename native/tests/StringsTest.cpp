#include <cwchar>
#include <string>

#include "Strings.h"
#include "Test.h"

using tourelle::ui::Texts;

namespace {

// Texts holds only const wchar_t* members: walk them as an array.
constexpr size_t kFieldCount = sizeof(Texts) / sizeof(const wchar_t*);
static_assert(sizeof(Texts) % sizeof(const wchar_t*) == 0);

const wchar_t* Field(const Texts& texts, size_t index) {
    return reinterpret_cast<const wchar_t* const*>(&texts)[index];
}

size_t Count(const wchar_t* text, const wchar_t* pattern) {
    size_t count = 0;
    for (const wchar_t* p = std::wcsstr(text, pattern); p; p = std::wcsstr(p + 1, pattern)) ++count;
    return count;
}

}  // namespace

TEST(strings_every_language_is_complete) {
    for (const Texts* language : tourelle::ui::kLanguages) {
        for (size_t i = 0; i < kFieldCount; ++i) {
            const wchar_t* text = Field(*language, i);
            EXPECT(text != nullptr);
            // licenseNote is empty in English only (the license is in English).
            if (text && language != &tourelle::ui::kEnglish) EXPECT(text[0] != L'\0');
        }
    }
}

TEST(strings_format_placeholders_match) {
    // These are passed to swprintf_s with exactly one unsigned argument.
    for (const Texts* language : tourelle::ui::kLanguages) {
        EXPECT_EQ(Count(language->motorFailed, L"%"), 1u);
        EXPECT_EQ(Count(language->motorFailed, L"%08X"), 1u);
        EXPECT_EQ(Count(language->registrationFailed, L"%"), 1u);
        EXPECT_EQ(Count(language->registrationFailed, L"%08X"), 1u);
        EXPECT_EQ(Count(language->presetSaved, L"%"), 1u);
        EXPECT_EQ(Count(language->presetSaved, L"%d"), 1u);
    }
}

TEST(strings_language_codes_unique) {
    const auto& all = tourelle::ui::kLanguages;
    for (size_t i = 0; i < std::size(all); ++i)
        for (size_t j = i + 1; j < std::size(all); ++j) EXPECT(std::wcscmp(all[i]->code, all[j]->code) != 0);
}
