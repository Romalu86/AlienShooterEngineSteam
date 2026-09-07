#include "win/main_sw.h"

#ifdef _WIN32
#include "win/dialog_item.h"
#include "win/resources/resource.h"
#include "core/configuration.h"
#include "core/log.h"
#include "core/as_string.h"
#include "graph.h"
#include "file_data.h"

#include <string>
#include <cstring>
#include <cstdlib>
#include <new>
#include <cstdint>

namespace as1 { namespace win
{
    namespace
    {

        alignas(STRING) unsigned char g_dialogBloodPasswordStorage[sizeof(STRING)];
        unsigned char g_dialogBloodPasswordInitFlags = 0;

        STRING& dialogBloodPassword()
        {
            if ((g_dialogBloodPasswordInitFlags & 1u) == 0u)
            {
                new (g_dialogBloodPasswordStorage) STRING();
                g_dialogBloodPasswordInitFlags |= 1u;
                std::atexit(destroyDialogBloodPasswordStorage);
            }
            return *reinterpret_cast<STRING*>(g_dialogBloodPasswordStorage);
        }

        void refreshPasswordGate(HWND dialog)
        {
            STRING editText;
            readDialogItemText(DialogItemRef{dialog, IDC_START_BLOOD_PASSWORD}, editText);
            const STRING& password = dialogBloodPassword();
            SetDlgItemEnabled(dialog, IDC_START_BLOOD_MODE,
                              std::strcmp(password.c_str(), editText.c_str()) == 0);

            const bool passwordIsEmpty = password.isEmpty();
            SetDlgItemVisible(dialog, IDC_START_BLOOD_DISABLE_TXT, !passwordIsEmpty);
            SetDlgItemVisible(dialog, IDC_START_BLOOD_ENABLE_TXT, passwordIsEmpty);
        }

        void hideRetailDisabledStartControls(HWND dialog)
        {

            if ((core::StartupSettings().flags & 0x04u) != 0)
                return;
            SetDlgItemVisible(dialog, IDC_START_FULLSCREEN, false);
            SetDlgItemVisible(dialog, IDC_START_NO_START_DIALOG, false);
            SetDlgItemVisible(dialog, IDC_START_TRIPLE_BUFFER, false);
            SetDlgItemVisible(dialog, IDC_START_USE_PALETTE, false);
            SetDlgItemVisible(dialog, IDC_START_LOW_DETAIL, false);
            SetDlgItemVisible(dialog, IDC_START_SAVE_DEMO, false);
            SetDlgItemVisible(dialog, IDC_START_SOUND_HQ, false);
        }

        void syncGraphDialog(HWND dialog)
        {
            GRAPH* const graph = GRAPH::CurrentGraph();
            const DialogItemRef deviceRef{dialog, IDC_START_VIDEO_DEVICE};
            const DialogItemRef modeRef{dialog, IDC_START_VIDEO_MODE};
            const DialogItemRef fullscreenRef{dialog, IDC_START_FULLSCREEN};
            graph->syncDisplayModeDialog(deviceRef, modeRef, &fullscreenRef);
        }
    }

    INT_PTR CALLBACK DialogFunc(HWND dialog, UINT message, WPARAM wparam, LPARAM)
    {

        (void)dialogBloodPassword();
        refreshPasswordGate(dialog);

        if (message == WM_INITDIALOG)
        {
            SetWindowTextA(dialog, core::StartupSettings().title);
            hideRetailDisabledStartControls(dialog);

            dialogBloodPassword() = FileDataLoad(STRING("options:\\Password"), STRING(""));
            const int highQuality = std::atoi(FileDataLoad(STRING("options:\\sound\\SoundHighQuality"), STRING("1")).c_str());
            SendDlgItemMessageA(dialog, IDC_START_SOUND_HQ, BM_SETCHECK,
                                highQuality != 0 ? BST_CHECKED : BST_UNCHECKED, 0);

            syncGraphDialog(dialog);

            SendDlgItemMessageA(dialog, IDC_START_BLOOD_MODE, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>("Green Blood"));
            SendDlgItemMessageA(dialog, IDC_START_BLOOD_MODE, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>("Red Blood"));
            const int blood = std::atoi(FileDataLoad(STRING("save:\\common\\Blood"), STRING("0")).c_str());
            SendDlgItemMessageA(dialog, IDC_START_BLOOD_MODE, CB_SETCURSEL,
                                static_cast<WPARAM>(blood), 0);
            if (SendDlgItemMessageA(dialog, IDC_START_BLOOD_MODE, CB_GETCURSEL, 0, 0) != 0)
            {

                assignStringFromCString(dialogBloodPassword(), "");
                SetDlgItemTextA(dialog, IDC_START_BLOOD_PASSWORD, dialogBloodPassword().c_str());
            }
            return TRUE;
        }

        if (message != WM_COMMAND)
            return FALSE;

        const WORD controlId = LOWORD(wparam);
        if (controlId == IDC_START_VIDEO_DEVICE || controlId == IDC_START_VIDEO_MODE)
        {
            if (HIWORD(wparam) == CBN_SELENDOK)
                syncGraphDialog(dialog);
            return FALSE;
        }

        if (controlId == IDOK)
        {
            syncGraphDialog(dialog);
            FileDataSave(
                STRING("options:\\sound\\SoundHighQuality"),
                STRING(SendDlgItemMessageA(dialog, IDC_START_SOUND_HQ, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0"));
            FileDataSave(
                STRING("save:\\common\\Blood"),
                STRING(std::to_string(static_cast<unsigned char>(
                    SendDlgItemMessageA(dialog, IDC_START_BLOOD_MODE, CB_GETCURSEL, 0, 0)))));

            STRING editText;
            readDialogItemText(DialogItemRef{dialog, IDC_START_BLOOD_PASSWORD}, editText);

            const STRING storedPassword = FileDataLoad(STRING("options:\\Password"), STRING(""));
            if (std::strcmp(storedPassword.c_str(), editText.c_str()) != 0)
            {
                STRING editTextForWrite;
                readDialogItemText(DialogItemRef{dialog, IDC_START_BLOOD_PASSWORD}, editTextForWrite);
                FileDataSave(STRING("options:\\Password"), editTextForWrite);
            }

            EndDialog(dialog, 1);
            return FALSE;
        }

        if (controlId == IDCANCEL)
        {
            EndDialog(dialog, 0);
            return FALSE;
        }

        if (controlId == IDC_START_BLOOD_PASSWORD)
        {
            STRING editTextForClass;
            readDialogItemText(DialogItemRef{dialog, IDC_START_BLOOD_PASSWORD}, editTextForClass);

            if (!editTextForClass.isEmpty())
            {
                STRING editTextForPassword;
                readDialogItemText(DialogItemRef{dialog, IDC_START_BLOOD_PASSWORD}, editTextForPassword);
                if (std::strcmp(dialogBloodPassword().c_str(), editTextForPassword.c_str()) != 0)
                    SendDlgItemMessageA(dialog, IDC_START_BLOOD_MODE, CB_SETCURSEL, 0, 0);
            }
            return FALSE;
        }

        if (controlId == IDC_START_BLOOD_MODE &&
            HIWORD(wparam) == CBN_SELENDOK &&
            SendDlgItemMessageA(dialog, IDC_START_BLOOD_MODE, CB_GETCURSEL, 0, 0) != 0)
        {

            assignStringFromCString(dialogBloodPassword(), "");
            SetDlgItemTextA(dialog, IDC_START_BLOOD_PASSWORD, dialogBloodPassword().c_str());
        }
        return FALSE;
    }

    void __cdecl destroyDialogBloodPasswordStorage()
    {

        STRING& owner = *reinterpret_cast<STRING*>(g_dialogBloodPasswordStorage);
        char* const ownedText = const_cast<char*>(owner.c_str());
        if (ownedText != STRING::SharedEmptyText())
            ::operator delete(ownedText);
    }

    INT_PTR CALLBACK AboutDialogFunc(HWND dialog, UINT message, WPARAM wparam, LPARAM)
    {
        if (message == WM_COMMAND && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL))
        {
            EndDialog(dialog, LOWORD(wparam));
            return TRUE;
        }
        return FALSE;
    }

    bool ShowStartDialog(HINSTANCE instance)
    {

        const INT_PTR result = DialogBoxParamA(instance, "START_DIALOG", nullptr, DialogFunc, 0);
        return result != 0;
    }

    void ShowAboutDialog(HINSTANCE instance, HWND owner)
    {
        DialogBoxParamA(instance, "APPABOUT", owner, AboutDialogFunc, 0);
    }
} }
#endif
