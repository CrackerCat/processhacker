/*
 * Process Hacker - 
 *   information dialog
 * 
 * Copyright (C) 2010 wj32
 * 
 * This file is part of Process Hacker.
 * 
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <phapp.h>
#include <windowsx.h>

static INT_PTR CALLBACK PhpInformationDlgProc(      
    __in HWND hwndDlg,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            PWSTR string = (PWSTR)lParam;
            PPH_LAYOUT_MANAGER layoutManager;

            PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            SetDlgItemText(hwndDlg, IDC_TEXT, string);

            layoutManager = PhAllocate(sizeof(PH_LAYOUT_MANAGER));
            PhInitializeLayoutManager(layoutManager, hwndDlg);
            PhAddLayoutItem(layoutManager, GetDlgItem(hwndDlg, IDC_TEXT), NULL,
                PH_ANCHOR_ALL);
            PhAddLayoutItem(layoutManager, GetDlgItem(hwndDlg, IDOK), NULL,
                PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM);

            SetProp(hwndDlg, L"LayoutManager", (HANDLE)layoutManager);
            SetProp(hwndDlg, L"String", (HANDLE)string);
        }
        break;
    case WM_DESTROY:
        {
            PPH_LAYOUT_MANAGER layoutManager;

            layoutManager = (PPH_LAYOUT_MANAGER)GetProp(hwndDlg, L"LayoutManager");
            PhDeleteLayoutManager(layoutManager);
            PhFree(layoutManager);
            RemoveProp(hwndDlg, L"String");
            RemoveProp(hwndDlg, L"LayoutManager");
        }
        break;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDCANCEL:
            case IDOK:
                EndDialog(hwndDlg, IDOK);
                break;
            case IDC_COPY:
                {
                    HWND editControl;
                    LONG selStart;
                    LONG selEnd;
                    PWSTR buffer;
                    PH_STRINGREF string;

                    editControl = GetDlgItem(hwndDlg, IDC_TEXT);
                    SendMessage(editControl, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
                    buffer = (PWSTR)GetProp(hwndDlg, L"String");

                    if (selStart == selEnd)
                    {
                        // Select and copy the entire string.
                        PhInitializeStringRef(&string, buffer);
                        Edit_SetSel(editControl, 0, -1);
                    }
                    else
                    {
                        string.Buffer = buffer + selStart;
                        string.Length = (USHORT)((selEnd - selStart) * 2);
                    }

                    PhSetClipboardString(hwndDlg, &string);
                    SetFocus(editControl);
                }
                break;
            case IDC_SAVE:
                {
                    static PH_FILETYPE_FILTER filters[] =
                    {
                        { L"Text files (*.txt)", L"*.txt" },
                        { L"All files (*.*)", L"*.*" }
                    };
                    PVOID fileDialog;

                    fileDialog = PhCreateSaveFileDialog();

                    PhSetFileDialogFilter(fileDialog, filters, sizeof(filters) / sizeof(PH_FILETYPE_FILTER));
                    PhSetFileDialogFileName(fileDialog, L"Information.txt");

                    if (PhShowFileDialog(hwndDlg, fileDialog))
                    {
                        NTSTATUS status;
                        PPH_STRING fileName;
                        PPH_FILE_STREAM fileStream;

                        fileName = PhGetFileDialogFileName(fileDialog);
                        PhaDereferenceObject(fileName);

                        if (NT_SUCCESS(status = PhCreateFileStream(
                            &fileStream,
                            fileName->Buffer,
                            FILE_GENERIC_WRITE,
                            FILE_SHARE_READ,
                            FILE_OVERWRITE_IF,
                            0
                            )))
                        {
                            PH_STRINGREF string;

                            PhInitializeStringRef(&string, (PWSTR)GetProp(hwndDlg, L"String"));
                            PhWriteStringAsAnsiFileStream(fileStream, &string);
                            PhDereferenceObject(fileStream);
                        }

                        if (!NT_SUCCESS(status))
                            PhShowStatus(hwndDlg, L"Unable to create the file", status, 0);
                    }

                    PhFreeFileDialog(fileDialog);
                }
                break;
            }
        }
        break;
    case WM_SIZE:
        {
            PPH_LAYOUT_MANAGER layoutManager;

            layoutManager = (PPH_LAYOUT_MANAGER)GetProp(hwndDlg, L"LayoutManager");
            PhLayoutManagerLayout(layoutManager);
        }
        break;
    case WM_SIZING:
        {
            PhResizingMinimumSize((PRECT)lParam, wParam, 180, 160);
        }
        break;
    }

    return FALSE;
}

VOID PhShowInformationDialog(
    __in HWND ParentWindowHandle,
    __in PWSTR String
    )
{
    DialogBoxParam(
        PhInstanceHandle,
        MAKEINTRESOURCE(IDD_INFORMATION),
        ParentWindowHandle,
        PhpInformationDlgProc,
        (LPARAM)String
        );
}
