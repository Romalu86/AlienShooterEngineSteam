#pragma once
#include <algorithm>

namespace as1
{
    struct RECTI
    {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;

        int width() const { return right - left; }
        int height() const { return bottom - top; }
        bool empty() const { return width() <= 0 || height() <= 0; }
    };

    inline bool intersectRect(RECTI& out, const RECTI& a, const RECTI& b)
    {
        out.left = (std::max)(a.left, b.left);
        out.top = (std::max)(a.top, b.top);
        out.right = (std::min)(a.right, b.right);
        out.bottom = (std::min)(a.bottom, b.bottom);
        return !out.empty();
    }
}
