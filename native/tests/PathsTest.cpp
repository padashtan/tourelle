#include "Paths.h"
#include "Test.h"

using tourelle::core::IsPathInside;

TEST(paths_inside_and_outside) {
    const wchar_t* folder = L"C:\\Program Files\\Tourelle";
    EXPECT(IsPathInside(L"C:\\Program Files\\Tourelle\\TourelleCamera.dll", folder));
    EXPECT(IsPathInside(L"c:/program files/tourelle/TourelleCamera.dll", folder));  // case and separators
    EXPECT(IsPathInside(L"C:\\Program Files\\Tourelle\\TourelleCamera.dll", L"C:\\Program Files\\Tourelle\\"));
    EXPECT(!IsPathInside(L"C:\\Program Files\\Tourelle", folder));                    // the folder itself
    EXPECT(!IsPathInside(L"C:\\Program Files\\Tourelle\\", folder));
    EXPECT(!IsPathInside(L"C:\\Program Files\\TourelleX\\a.dll", folder));            // sibling prefix
    EXPECT(!IsPathInside(L"C:\\Users\\me\\a.dll", folder));
    EXPECT(!IsPathInside(L"C:\\Program Files\\Tourelle\\..\\Other\\a.dll", folder));  // traversal backstop
    EXPECT(IsPathInside(L"C:\\Program Files\\Tourelle\\a..b.dll", folder));            // dots inside a name are fine
    EXPECT(!IsPathInside(L"", folder));
    EXPECT(!IsPathInside(L"C:\\a.dll", L""));
}
