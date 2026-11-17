#include "Paths.h"

#include <cwctype>

namespace tourelle::core {
namespace {

wchar_t Fold(wchar_t c) { return c == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(c)); }

}  // namespace

bool IsPathInside(std::wstring_view file, std::wstring_view folder) {
    while (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/')) folder.remove_suffix(1);
    if (folder.empty() || file.size() <= folder.size() + 1) return false;
    for (size_t i = 0; i < folder.size(); ++i) {
        if (Fold(file[i]) != Fold(folder[i])) return false;
    }
    if (Fold(file[folder.size()]) != L'\\') return false;
    // Reject any ".." segment left in the remainder (callers normalize, this is a backstop).
    const std::wstring_view rest = file.substr(folder.size());
    for (size_t i = 0; i + 1 < rest.size(); ++i) {
        if (rest[i] == L'.' && rest[i + 1] == L'.') {
            const bool startsSegment = i == 0 || Fold(rest[i - 1]) == L'\\';
            const bool endsSegment = i + 2 == rest.size() || Fold(rest[i + 2]) == L'\\';
            if (startsSegment && endsSegment) return false;
        }
    }
    return true;
}

}  // namespace tourelle::core
