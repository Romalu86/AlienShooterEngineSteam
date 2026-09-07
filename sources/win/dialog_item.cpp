#include "win/dialog_item.h"

#ifdef _WIN32
namespace as1 { namespace win
{
    bool DialogItemRef::isChecked() const
    {
        return ::SendDlgItemMessageA(dialog, controlId, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    LRESULT DialogItemRef::currentSelection() const
    {
        return ::SendDlgItemMessageA(dialog, controlId, CB_GETCURSEL, 0, 0);
    }

    STRING& readDialogItemText(const DialogItemRef& item, STRING& out)
    {
        char buffer[0x200];
        ::GetDlgItemTextA(item.dialog, item.controlId, buffer, 0x200);
        out.AssignAllocatedCopyWithoutRelease(buffer);
        return out;
    }
} }
#endif
