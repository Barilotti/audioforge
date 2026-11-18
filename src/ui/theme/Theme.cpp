#include "ui/theme/Theme.h"

namespace af::ui
{

const Theme& Theme::current()
{
    static const Theme dark;
    return dark;
}

} // namespace af::ui
