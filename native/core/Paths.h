#pragma once
// Path containment check used before the elevated setup deletes or reports a file.
// Pure code: the caller passes already-normalized absolute paths (GetFullPathName).

#include <string_view>

namespace tourelle::core {

// True when file is strictly inside folder (case-insensitive, either separator).
// "C:\A\B" is not inside "C:\A\B", and "C:\AB\x" is not inside "C:\A".
bool IsPathInside(std::wstring_view file, std::wstring_view folder);

}  // namespace tourelle::core
