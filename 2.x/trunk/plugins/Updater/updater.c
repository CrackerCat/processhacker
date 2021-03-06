/*
 * Process Hacker Update Checker -
 *   main window
 *
 * Copyright (C) 2011-2012 dmex
 * Copyright (C) 2011 wj32
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker. If not, see <http://www.gnu.org/licenses/>.
*/

#include "updater.h"

#define WM_UPDATE (WM_APP + 1)

static PPH_PLUGIN PluginInstance;
static PH_CALLBACK_REGISTRATION PluginMenuItemCallbackRegistration;
static PH_CALLBACK_REGISTRATION MainWindowShowingCallbackRegistration;
static PH_CALLBACK_REGISTRATION PluginShowOptionsCallbackRegistration;

static PH_EVENT InitializedEvent = PH_EVENT_INIT;
static HWND UpdateDialogHandle = NULL;
static HANDLE UpdaterDialogThreadHandle = NULL;
static HANDLE UpdateCheckThreadHandle = NULL;
static HANDLE DownloadThreadHandle = NULL;
static HFONT FontHandle = NULL;
static PH_UPDATER_STATE PhUpdaterState = Download;
static PPH_STRING SetupFilePath = NULL;
static UPDATER_XML_DATA UpdateData;
static PH_QUEUED_LOCK Lock = PH_QUEUED_LOCK_INIT;

static DWORD contentLength = 0;
static DWORD bytesDownloaded = 0, timeTransferred = 0, LastUpdateTime = 0;
static BOOLEAN IsUpdating = FALSE;

LOGICAL DllMain(
    __in HINSTANCE Instance,
    __in ULONG Reason,
    __reserved PVOID Reserved
    )
{
    switch (Reason)
    {
    case DLL_PROCESS_ATTACH:
        {
            PPH_PLUGIN_INFORMATION info;

            PluginInstance = PhRegisterPlugin(L"ProcessHacker.UpdateChecker", Instance, &info);

            if (!PluginInstance)
                return FALSE;

            info->DisplayName = L"Update Checker";
            info->Author = L"dmex";
            info->Description = L"Plugin for checking new Process Hacker releases via the Help menu.";
            info->HasOptions = TRUE;

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackMainWindowShowing),
                MainWindowShowingCallback,
                NULL,
                &MainWindowShowingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetPluginCallback(PluginInstance, PluginCallbackMenuItem),
                MenuItemCallback,
                NULL,
                &PluginMenuItemCallbackRegistration
                );
            PhRegisterCallback(
                PhGetPluginCallback(PluginInstance, PluginCallbackShowOptions),
                ShowOptionsCallback,
                NULL,
                &PluginShowOptionsCallbackRegistration
                );

            {
                PH_SETTING_CREATE settings[] =
                {
                    { IntegerSettingType, SETTING_AUTO_CHECK, L"1" },
                };

                PhAddSettings(settings, _countof(settings));
            }
        }
        break;
    }

    return TRUE;
}

static VOID AsyncUpdate(
    VOID
    )
{
    if (!IsUpdating)
    {
        IsUpdating = TRUE;

        if (!PostMessage(UpdateDialogHandle, WM_UPDATE, 0, 0))
        {
            IsUpdating = FALSE;
        }
    }
}

static VOID NTAPI MainWindowShowingCallback(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    // Add our menu item, 4 = Help menu.
    PhPluginAddMenuItem(PluginInstance, 4, NULL, UPDATE_MENUITEM, L"Check for Updates", NULL);

    // Check if the user want's us to auto-check for updates.
    if (PhGetIntegerSetting(SETTING_AUTO_CHECK))
    {
        // All good, queue up our update check.
        StartInitialCheck();
    }
}

static VOID NTAPI MenuItemCallback(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    PPH_PLUGIN_MENU_ITEM menuItem = (PPH_PLUGIN_MENU_ITEM)Parameter;

    if (menuItem != NULL)
    {
        switch (menuItem->Id)
        {
        case UPDATE_MENUITEM:
            {
                ShowUpdateDialog();
            }
            break;
        }
    }
}

static VOID NTAPI ShowOptionsCallback(
    __in_opt PVOID Parameter,
    __in_opt PVOID Context
    )
{
    DialogBox(
        (HINSTANCE)PluginInstance->DllBase,
        MAKEINTRESOURCE(IDD_OPTIONS),
        (HWND)Parameter,
        OptionsDlgProc
        );
}

static INT CompareVersions(
    __in ULONG MajorVersion1,
    __in ULONG MinorVersion1,
    __in ULONG MajorVersion2,
    __in ULONG MinorVersion2
    )
{
    INT result = intcmp(MajorVersion1, MajorVersion2);

    if (result == 0)
        result = intcmp(MinorVersion1, MinorVersion2);

    return result;
}

static BOOL ConnectionAvailable(
    VOID
    )
{
    if (WindowsVersion > WINDOWS_XP)
    {
        INetworkListManager *pNetworkListManager;

        // Create an instance of the INetworkListManger COM object.
        if (SUCCEEDED(CoCreateInstance(&CLSID_NetworkListManager, NULL, CLSCTX_ALL, &IID_INetworkListManager, &pNetworkListManager)))
        {
            VARIANT_BOOL isConnected = VARIANT_FALSE;
            VARIANT_BOOL isConnectedInternet = VARIANT_FALSE;

            // Query the relevant properties.
            INetworkListManager_get_IsConnected(pNetworkListManager, &isConnected);
            INetworkListManager_get_IsConnectedToInternet(pNetworkListManager, &isConnectedInternet);

            // Cleanup the INetworkListManger COM object.
            INetworkListManager_Release(pNetworkListManager);
            pNetworkListManager = NULL;

            // Check if Windows is connected to a network and it's connected to the internet.
            if (isConnected == VARIANT_TRUE && isConnectedInternet == VARIANT_TRUE)
            {
                // We're online and connected to the internet.
                return TRUE;
            }

            // We're not connected to anything.
            return FALSE;
        }

        // If we reached here, we were unable to init the INetworkListManager, fall back to InternetGetConnectedState.
        goto NOT_SUPPORTED;
    }
    else
NOT_SUPPORTED:
    {
        DWORD dwType;

        if (InternetGetConnectedState(&dwType, 0))
        {
            return TRUE;
        }
        else
        {
            LogEvent(NULL, PhFormatString(L"Updater: (ConnectionAvailable) InternetGetConnectedState failed to detect an active Internet connection (%d)", GetLastError()));
        }

        //if (!InternetCheckConnection(NULL, FLAG_ICC_FORCE_CONNECTION, 0))
        //{
        // LogEvent(PhFormatString(L"Updater: (ConnectionAvailable) InternetCheckConnection failed connection to Sourceforge.net (%d)", GetLastError()));
        // return FALSE;
        //}
    }

    return FALSE;
}

static BOOL ReadRequestString(
    __in HINTERNET Handle,
    __out_z PSTR *Data,
    __out_opt PULONG DataLength
    )
{
    BYTE buffer[PAGE_SIZE];
    PSTR data;
    ULONG allocatedLength;
    ULONG dataLength;
    ULONG returnLength;

    allocatedLength = sizeof(buffer);
    data = (PSTR)PhAllocate(allocatedLength);
    dataLength = 0;

    while (InternetReadFile(Handle, buffer, PAGE_SIZE, &returnLength))
    {
        if (returnLength == 0)
            break;

        if (allocatedLength < dataLength + returnLength)
        {
            allocatedLength *= 2;
            data = (PSTR)PhReAllocate(data, allocatedLength);
        }

        // Copy the returned buffer into our pointer
        RtlCopyMemory(data + dataLength, buffer, returnLength);
        // Zero the returned buffer for the next loop
        RtlZeroMemory(buffer, PAGE_SIZE);

        dataLength += returnLength;
    }

    if (allocatedLength < dataLength + 1)
    {
        allocatedLength++;
        data = (PSTR)PhReAllocate(data, allocatedLength);
    }

    // Ensure that the buffer is null-terminated.
    data[dataLength] = 0;

    *Data = data;

    if (DataLength)
        *DataLength = dataLength;

    return TRUE;
}

static BOOL PhInstalledUsingSetup(
    VOID
    )
{
    static PH_STRINGREF keyName = PH_STRINGREF_INIT(L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Process_Hacker2_is1");

    HANDLE keyHandle = NULL;

    // Check uninstall entries for the 'Process_Hacker2_is1' registry key.
    if (NT_SUCCESS(PhOpenKey(
        &keyHandle,
        KEY_READ,
        PH_KEY_LOCAL_MACHINE,
        &keyName,
        0
        )))
    {
        NtClose(keyHandle);
        return TRUE;
    }

    return FALSE;
}

static PPH_STRING PhGetOpaqueXmlNodeText(
    __in mxml_node_t *xmlNode
    )
{
    if (xmlNode && xmlNode->child && xmlNode->child->type == MXML_OPAQUE && xmlNode->child->value.opaque)
    {
        return PhCreateStringFromAnsi(xmlNode->child->value.opaque);
    }
    
    return PhReferenceEmptyString();
}

static BOOL QueryXmlData(
    VOID
    )
{
    PCHAR data = NULL;
    BOOL isSuccess = FALSE;
    HINTERNET netInitialize = NULL, netConnection = NULL, netRequest = NULL;
    mxml_node_t *xmlDoc = NULL, *xmlNodeVer = NULL, *xmlNodeRelDate = NULL, *xmlNodeSize = NULL, *xmlNodeHash = NULL;

    // Create a user agent string.
    PPH_STRING phVersion = PhGetPhVersion();
    PPH_STRING userAgent = PhConcatStrings2(L"PH Updater v", phVersion->Buffer);

    __try
    {
        // Initialize the wininet library.
        if (!(netInitialize = InternetOpen(
            userAgent->Buffer,
            INTERNET_OPEN_TYPE_PRECONFIG,
            NULL,
            NULL,
            0
            )))
        {
            LogEvent(NULL, PhFormatString(L"Updater: (InitializeConnection) InternetOpen failed (%d)", GetLastError()));
            __leave;
        }

        // Connect to the server.
        if (!(netConnection = InternetConnect(
            netInitialize,
            UPDATE_URL,
            INTERNET_DEFAULT_HTTP_PORT,
            NULL,
            NULL,
            INTERNET_SERVICE_HTTP,
            0,
            0
            )))
        {
            LogEvent(NULL, PhFormatString(L"Updater: (InitializeConnection) InternetConnect failed (%d)", GetLastError()));
            __leave;
        }

        // Open the HTTP request.
        if (!(netRequest = HttpOpenRequest(
            netConnection,
            L"GET",
            UPDATE_FILE,
            NULL,
            NULL,
            NULL,
            // wj32: do NOT cache --------------------------- Old - "Always cache the update xml, it can be cleared by deleting IE history, we configured the file to cache locally for two days."
            INTERNET_FLAG_RELOAD,
            0
            )))
        {
            LogEvent(NULL, PhFormatString(L"Updater: (InitializeConnection) HttpOpenRequest failed (%d)", GetLastError()));
            __leave;
        }

        // Send the HTTP request.
        if (!HttpSendRequest(netRequest, NULL, 0, NULL, 0))
        {
            LogEvent(NULL, PhFormatString(L"HttpSendRequest failed (%d)", GetLastError()));
            __leave;
        }

        // Read the resulting xml into our buffer.
        if (!ReadRequestString(netRequest, &data, NULL))
        {
            // We don't need to log this.
            __leave;
        }

        // Load our XML.
        xmlDoc = mxmlLoadString(NULL, data, QueryXmlDataCallback);
        // Check our XML.
        if (xmlDoc == NULL || xmlDoc->type != MXML_ELEMENT)
        {
            LogEvent(NULL, PhCreateString(L"Updater: (WorkerThreadStart) mxmlLoadString failed."));
            __leave;
        }

        // Find the ver node.
        xmlNodeVer = mxmlFindElement(xmlDoc, xmlDoc, "ver", NULL, NULL, MXML_DESCEND);
        // Find the reldate node.
        xmlNodeRelDate = mxmlFindElement(xmlDoc, xmlDoc, "reldate", NULL, NULL, MXML_DESCEND);
        // Find the size node.
        xmlNodeSize = mxmlFindElement(xmlDoc, xmlDoc, "size", NULL, NULL, MXML_DESCEND);
        // Find the hash node.
        xmlNodeHash = mxmlFindElement(xmlDoc, xmlDoc, "sha1", NULL, NULL, MXML_DESCEND);

        // Format strings into unicode PPH_STRING's
        UpdateData.Version = PhGetOpaqueXmlNodeText(xmlNodeVer);
        UpdateData.RelDate = PhGetOpaqueXmlNodeText(xmlNodeRelDate);
        UpdateData.Size = PhGetOpaqueXmlNodeText(xmlNodeSize);
        UpdateData.Hash = PhGetOpaqueXmlNodeText(xmlNodeHash);

        // parse and check string
        //if (!ParseVersionString(XmlData->Version->Buffer, &XmlData->MajorVersion, &XmlData->MinorVersion))
        //    __leave;
        if (!PhIsNullOrEmptyString(UpdateData.Version))
        {
            PH_STRINGREF sr, majorPart, minorPart;
            ULONG64 majorInteger = 0, minorInteger = 0;

            PhInitializeStringRef(&sr, UpdateData.Version->Buffer);

            if (PhSplitStringRefAtChar(&sr, '.', &majorPart, &minorPart))
            {
                PhStringToInteger64(&majorPart, 10, &majorInteger);
                PhStringToInteger64(&minorPart, 10, &minorInteger);

                UpdateData.MajorVersion = (ULONG)majorInteger;
                UpdateData.MinorVersion = (ULONG)minorInteger;

                isSuccess = TRUE;
            }
        }
    }
    __finally
    {
        if (xmlDoc)
        {
            mxmlDelete(xmlDoc);
            xmlDoc = NULL;
        }

        if (netInitialize)
        {
            InternetCloseHandle(netInitialize);
            netInitialize = NULL;
        }

        if (netConnection)
        {
            InternetCloseHandle(netConnection);
            netConnection = NULL;
        }

        if (netRequest)
        {
            InternetCloseHandle(netRequest);
            netRequest = NULL;
        }

        if (userAgent)
        {
            PhDereferenceObject(userAgent);
            userAgent = NULL;
        }

        if (phVersion)
        {
            PhDereferenceObject(phVersion);
            phVersion = NULL;
        }
    }

    return isSuccess;
}

static BOOL FreeXmlData(
    VOID
    )
{
    if (UpdateData.Version)
    {
        PhDereferenceObject(UpdateData.Version);
        UpdateData.Version = NULL;
    }
    
    if (UpdateData.RelDate)
    {
        PhDereferenceObject(UpdateData.RelDate);
        UpdateData.RelDate = NULL;
    }
    
    if (UpdateData.Size)
    {
        PhDereferenceObject(UpdateData.Size);
        UpdateData.Size = NULL;
    }
    
    if (UpdateData.Hash)
    {
        PhDereferenceObject(UpdateData.Hash);
        UpdateData.Hash = NULL;
    }

    return TRUE;
}

static NTSTATUS SilentUpdateCheckThreadStart(
    __in PVOID Parameter
    )
{
    if (ConnectionAvailable())
    {
        if (QueryXmlData())
        {
            ULONG majorVersion = 0;
            ULONG minorVersion = 0;

            // Get the current Process Hacker version
            PhGetPhVersionNumbers(&majorVersion, &minorVersion, NULL, NULL);

            // Compare the current version against the latest available version
            if (CompareVersions(UpdateData.MajorVersion, UpdateData.MinorVersion, majorVersion, minorVersion) > 0)
            {
                // Don't spam the user the second they open PH, delay dialog creation for 3 seconds.
                Sleep(3 * 1000);

                // Show the dialog asynchronously on a new thread.
                ShowUpdateDialog();
            }
        }

        FreeXmlData();
    }

    return STATUS_SUCCESS;
}

static NTSTATUS CheckUpdateThreadStart(
    __in PVOID Parameter
    )
{
    HWND hwndDlg = (HWND)Parameter;

    if (ConnectionAvailable())
    {
        if (QueryXmlData())
        {
            INT result = 0;

            PhGetPhVersionNumbers(
                &UpdateData.PhMajorVersion, 
                &UpdateData.PhMinorVersion, 
                NULL, 
                &UpdateData.PhRevisionVersion
                );

            result = 1;/*CompareVersions(
                UpdateData.MajorVersion, 
                UpdateData.MinorVersion, 
                UpdateData.PhMajorVersion, 
                UpdateData.PhMinorVersion
                );*/

            if (result > 0)
            {
                PPH_STRING summaryText = PhFormatString(
                    L"Process Hacker %u.%u",
                    UpdateData.MajorVersion,
                    UpdateData.MinorVersion
                    );

                PPH_STRING releaseDateText = PhFormatString(
                    L"Released: %s",
                    UpdateData.RelDate->Buffer
                    );

                PPH_STRING releaseSizeText = PhFormatString(
                    L"Size: %s",
                    UpdateData.Size->Buffer
                    );

                SetDlgItemText(hwndDlg, IDC_MESSAGE, summaryText->Buffer);
                SetDlgItemText(hwndDlg, IDC_RELDATE, releaseDateText->Buffer);
                SetDlgItemText(hwndDlg, IDC_STATUS, releaseSizeText->Buffer);

                // Set the state for the button to know it can preform the download action.
                PhUpdaterState = Download;

                // Enable the download button.
                Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), TRUE);
                // Use the Scrollbar macro to enable the other controls.
                ScrollBar_Show(GetDlgItem(hwndDlg, IDC_PROGRESS), TRUE);
                ScrollBar_Show(GetDlgItem(hwndDlg, IDC_RELDATE), TRUE);
                ScrollBar_Show(GetDlgItem(hwndDlg, IDC_STATUS), TRUE);

                PhDereferenceObject(releaseSizeText);
                PhDereferenceObject(releaseDateText);
                PhDereferenceObject(summaryText);
            }
            else if (result == 0)
            {
                PPH_STRING summaryText = PhCreateString(
                    L"No updates available"
                    );

                PPH_STRING versionText = PhFormatString(
                    L"You're running the latest stable version: v%u.%u",
                    UpdateData.MajorVersion,
                    UpdateData.MinorVersion
                    );

                SetDlgItemText(hwndDlg, IDC_MESSAGE, summaryText->Buffer);
                SetDlgItemText(hwndDlg, IDC_RELDATE, versionText->Buffer);
  
                PhDereferenceObject(versionText);
                PhDereferenceObject(summaryText);
            }
            else if (result < 0)
            {
                PPH_STRING summaryText = PhCreateString(
                    L"No updates available"
                    );

                PPH_STRING versionText = PhFormatString(
                    L"You're running SVN build: v%u.%u (r%u)",
                    UpdateData.PhMajorVersion,
                    UpdateData.PhMinorVersion,
                    UpdateData.PhRevisionVersion
                    );

                //swprintf_s(
                // szReleaseText,
                // _countof(szReleaseText),
                // L"Released: %s",
                // xmlData.RelDate
                // );

                SetDlgItemText(hwndDlg, IDC_RELDATE, versionText->Buffer);
                SetDlgItemText(hwndDlg, IDC_MESSAGE, summaryText->Buffer);
       
                PhDereferenceObject(versionText);
                PhDereferenceObject(summaryText);
            }
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS DownloadUpdateThreadStart(
    __in PVOID Parameter
    )
{
    PPH_STRING downloadUrlPath = NULL;
    HANDLE tempFileHandle = NULL;
    HINTERNET hInitialize = NULL, hConnection = NULL, hRequest = NULL;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    HWND hwndDlg = (HWND)Parameter;

    Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), FALSE);
    SetDlgItemText(hwndDlg, IDC_STATUS, L"Initializing");

    // Reset the progress state on Vista and above.
    if (WindowsVersion > WINDOWS_XP)
        SendDlgItemMessage(hwndDlg, IDC_PROGRESS, PBM_SETSTATE, PBST_NORMAL, 0L);

    if (!ConnectionAvailable())
        return status;

    __try
    {
        // Get temp dir.
        WCHAR tempPathString[MAX_PATH];
        DWORD tempPathLength = GetTempPath(MAX_PATH, tempPathString);

        if (tempPathLength == 0 || tempPathLength > MAX_PATH)
        {
            LogEvent(hwndDlg, PhFormatString(L"CreateFile failed (%d)", GetLastError()));
            __leave;
        }

        // create the download path string.
        downloadUrlPath = PhFormatString(
            L"/projects/processhacker/files/processhacker2/processhacker-%u.%u-setup.exe/download?use_mirror=autoselect", /* ?use_mirror=waix" */
            UpdateData.MajorVersion,
            UpdateData.MinorVersion
            );

        // Append the tempath to our string: %TEMP%processhacker-%u.%u-setup.exe
        // Example: C:\\Users\\dmex\\AppData\\Temp\\processhacker-2.10-setup.exe
        SetupFilePath = PhFormatString(
            L"%sprocesshacker-%u.%u-setup.exe",
            tempPathString,
            UpdateData.MajorVersion,
            UpdateData.MinorVersion
            );

        // Create output file
        status = PhCreateFileWin32(
            &tempFileHandle,
            SetupFilePath->Buffer,
            FILE_GENERIC_READ | FILE_GENERIC_WRITE,
            FILE_ATTRIBUTE_NOT_CONTENT_INDEXED | FILE_ATTRIBUTE_TEMPORARY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            FILE_OVERWRITE_IF,
            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
            );

        if (!NT_SUCCESS(status))
        {
            LogEvent(hwndDlg, PhFormatString(L"PhCreateFileWin32 failed (%s)", ((PPH_STRING)PHA_DEREFERENCE(PhGetNtMessage(status)))->Buffer));
            __leave;
        }

        {
            // Create a user agent string.
            PPH_STRING phVersion = PhGetPhVersion();
            PPH_STRING userAgent = PhConcatStrings2(L"PH Updater v", phVersion->Buffer);

            // Initialize the wininet library.
            if (!(hInitialize = InternetOpen(
                userAgent->Buffer,
                INTERNET_OPEN_TYPE_PRECONFIG,
                NULL,
                NULL,
                0
                )))
            {
                LogEvent(hwndDlg, PhFormatString(L"Updater: (InitializeConnection) InternetOpen failed (%d)", GetLastError()));

                PhDereferenceObject(userAgent);
                PhDereferenceObject(phVersion);

                __leave;
            }

            PhDereferenceObject(userAgent);
            PhDereferenceObject(phVersion);
        }

        // Connect to the server.
        if (!(hConnection = InternetConnect(
            hInitialize,
            L"sourceforge.net",
            INTERNET_DEFAULT_HTTP_PORT,
            NULL,
            NULL,
            INTERNET_SERVICE_HTTP,
            0,
            0)))
        {
            LogEvent(hwndDlg, PhFormatString(L"InternetConnect failed (%d)", GetLastError()));
            __leave;
        }

        // Open the HTTP request.
        if (!(hRequest = HttpOpenRequest(
            hConnection,
            NULL,
            downloadUrlPath->Buffer,
            NULL,
            NULL,
            NULL,
            INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RESYNCHRONIZE,
            0
            )))
        {
            LogEvent(hwndDlg, PhFormatString(L"HttpOpenRequest failed (%d)", GetLastError()));
            __leave;
        }

        SetDlgItemText(hwndDlg, IDC_STATUS, L"Connecting");

        // Send the HTTP request.
        if (!HttpSendRequest(hRequest, NULL, 0, NULL, 0))
        {
            LogEvent(hwndDlg, PhFormatString(L"HttpSendRequest failed (%d)", GetLastError()));

            // Enable the 'Retry' button.
            Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), TRUE);
            SetDlgItemText(hwndDlg, IDC_DOWNLOAD, L"Retry");

            // Reset the state and let user retry the download.
            PhUpdaterState = Download;
        }
        else
        {
            BYTE hashBuffer[20]; 
            DWORD contentLengthSize = sizeof(DWORD);
            PH_HASH_CONTEXT hashContext;

            // Initialize hash algorithm.
            PhInitializeHash(&hashContext, Sha1HashAlgorithm);

            if (!HttpQueryInfoW(hRequest, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &contentLengthSize, 0))
            {
                // No content length...impossible to calculate % complete...
                // we can read the data, BUT in this instance Sourceforge always returns the content length
                // so instead we'll exit here instead of downloading the file.
                LogEvent(hwndDlg, PhFormatString(L"HttpQueryInfo failed (%d)", GetLastError()));
                __leave;
            }
            else
            {
                BYTE buffer[PAGE_SIZE];
                DWORD bytesRead = 0, startTick = 0;
                IO_STATUS_BLOCK isb;

                // Zero the buffer.
                ZeroMemory(buffer, PAGE_SIZE);

                // Reset the counters.
                bytesDownloaded = 0, timeTransferred = 0, LastUpdateTime = 0;
                IsUpdating = FALSE;

                // Start the clock.
                startTick = GetTickCount();
                timeTransferred = startTick;

                // Download the data.
                while (InternetReadFile(hRequest, buffer, PAGE_SIZE, &bytesRead))
                {
                    // If we get zero bytes, the file was uploaded or there was an error.
                    if (bytesRead == 0)
                        break;

                    // If window closed and thread handle was closed, just dispose and exit.
                    // (This also skips error checking/prompts and updating the disposed UI)
                    if (!DownloadThreadHandle)
                        __leave;

                    // Update the hash of bytes we downloaded.
                    PhUpdateHash(&hashContext, buffer, bytesRead);

                    // Write the downloaded bytes to disk.
                    status = NtWriteFile(
                        tempFileHandle,
                        NULL,
                        NULL,
                        NULL,
                        &isb,
                        buffer,
                        bytesRead,
                        NULL,
                        NULL
                        );

                    if (!NT_SUCCESS(status))
                    {
                        PPH_STRING message = PhGetNtMessage(status);

                        LogEvent(hwndDlg, PhFormatString(L"NtWriteFile failed (%s)", message->Buffer));

                        PhDereferenceObject(message);
                        break;
                    }

                    // Check dwBytesRead are the same dwBytesWritten length returned by WriteFile.
                    if (bytesRead != isb.Information)
                    {
                        PPH_STRING message = PhGetNtMessage(status);

                        LogEvent(hwndDlg, PhFormatString(L"NtWriteFile failed (%s)", message->Buffer));

                        PhDereferenceObject(message);
                        break;
                    }
                                        
                    // Update our total bytes downloaded
                    PhAcquireQueuedLockExclusive(&Lock);
                    bytesDownloaded += (DWORD)isb.Information;
                    PhReleaseQueuedLockExclusive(&Lock);

                    AsyncUpdate();
                }

                // Check if we downloaded the entire file.
                assert(bytesDownloaded == contentLength);

                // Compute our hash result.
                if (PhFinalHash(&hashContext, &hashBuffer, 20, NULL))
                {
                    // Allocate our hash string, hex the final hash result in our hashBuffer.
                    PPH_STRING hexString = PhBufferToHexString(hashBuffer, 20);

                    if (PhEqualString(hexString, UpdateData.Hash, TRUE))
                    {
                        // If PH is not elevated, set the UAC shield for the install button as the setup requires elevation.
                        if (!PhElevated)
                            SendMessage(GetDlgItem(hwndDlg, IDC_DOWNLOAD), BCM_SETSHIELD, 0, TRUE);

                        // Set the download result, don't include hash status since it succeeded.
                        //SetDlgItemText(hwndDlg, IDC_STATUS, L"Download Complete");
                        // Set button text for next action
                        Button_SetText(GetDlgItem(hwndDlg, IDC_DOWNLOAD), L"Install");
                        // Enable the Install button
                        Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), TRUE);
                        // Hash succeeded, set state as ready to install.
                        PhUpdaterState = Install;
                    }
                    else
                    {
                        if (WindowsVersion > WINDOWS_XP)
                            SendDlgItemMessage(hwndDlg, IDC_PROGRESS, PBM_SETSTATE, PBST_ERROR, 0L);

                        SetDlgItemText(hwndDlg, IDC_STATUS, L"Download complete, SHA1 Hash failed.");

                        // Set button text for next action
                        Button_SetText(GetDlgItem(hwndDlg, IDC_DOWNLOAD), L"Retry");
                        // Enable the Install button
                        Button_Enable(GetDlgItem(hwndDlg, IDC_DOWNLOAD), TRUE);
                        // Hash failed, reset state to downloading so user can redownload the file.
                        PhUpdaterState = Download;
                    }

                    PhDereferenceObject(hexString);
                }
                else
                {
                    //SetDlgItemText(hwndDlg, IDC_STATUS, L"PhFinalHash failed");

                    // Show fancy Red progressbar if hash failed on Vista and above.
                    if (WindowsVersion > WINDOWS_XP)
                        SendDlgItemMessage(hwndDlg, IDC_PROGRESS, PBM_SETSTATE, PBST_ERROR, 0L);
                }
            }
        }

        status = STATUS_SUCCESS;
    }
    __finally
    {
        if (hInitialize)
        {
            InternetCloseHandle(hInitialize);
            hInitialize = NULL;
        }

        if (hConnection)
        {
            InternetCloseHandle(hConnection);
            hConnection = NULL;
        }

        if (hRequest)
        {
            InternetCloseHandle(hRequest);
            hRequest = NULL;
        }

        if (tempFileHandle)
        {
            NtClose(tempFileHandle);
            tempFileHandle = NULL;
        }

        if (downloadUrlPath)
        {
            PhDereferenceObject(downloadUrlPath);
            downloadUrlPath = NULL;
        }
    }

    return status;
}

static NTSTATUS ShowUpdateDialogThreadStart(
    __in PVOID Parameter
    )
{
    BOOL result;
    MSG message;
    PH_AUTO_POOL autoPool;

    PhInitializeAutoPool(&autoPool);

    UpdateDialogHandle = CreateDialog(
        (HINSTANCE)PluginInstance->DllBase,
        MAKEINTRESOURCE(IDD_UPDATE),
        PhMainWndHandle,
        UpdaterWndProc
        );

    PhSetEvent(&InitializedEvent);

    while (result = GetMessage(&message, NULL, 0, 0))
    {
        if (result == -1)
            break;

        if (!IsDialogMessage(UpdateDialogHandle, &message))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        PhDrainAutoPool(&autoPool);
    }

    PhDeleteAutoPool(&autoPool);
    PhResetEvent(&InitializedEvent);

    // Ensure global objects are disposed and reset when window closes.

    if (SetupFilePath)
    {
        PhDereferenceObject(SetupFilePath);
        SetupFilePath = NULL;
    }

    if (UpdateCheckThreadHandle)
    {
        NtClose(UpdateCheckThreadHandle);
        UpdateCheckThreadHandle = NULL;
    }

    if (DownloadThreadHandle)
    {
        NtClose(DownloadThreadHandle);
        DownloadThreadHandle = NULL;
    }

    if (UpdaterDialogThreadHandle)
    {
        NtClose(UpdaterDialogThreadHandle);
        UpdaterDialogThreadHandle = NULL;
    }

    if (FontHandle)
    {
        DeleteObject(FontHandle);
        FontHandle = NULL;
    }

    FreeXmlData();

    if (UpdateDialogHandle)
    {
        DestroyWindow(UpdateDialogHandle);
        UpdateDialogHandle = NULL;
    }

    return STATUS_SUCCESS;
}

INT_PTR CALLBACK UpdaterWndProc(
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
            LOGFONT lHeaderFont = { 0 };

            // load the PH main icon using the 'magic' resource id.
            HANDLE hPhIcon = LoadImage(
                GetModuleHandle(NULL),
                MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER),
                IMAGE_ICON,
                GetSystemMetrics(SM_CXICON),
                GetSystemMetrics(SM_CYICON),
                LR_SHARED
                );

            // Set our initial state as download
            PhUpdaterState = Download;

            // Set the window icon.
            SendMessage(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)hPhIcon);

            lHeaderFont.lfHeight = -15;
            lHeaderFont.lfWeight = FW_MEDIUM;
            lHeaderFont.lfQuality = CLEARTYPE_QUALITY | ANTIALIASED_QUALITY;
            
            // We don't check if Segoe exists, CreateFontIndirect does this for us.
            wcscpy_s(
                lHeaderFont.lfFaceName, 
                _countof(lHeaderFont.lfFaceName), 
                L"Segoe UI"
                );

            // Create the font handle.
            FontHandle = CreateFontIndirectW(&lHeaderFont);

            // Set the header font.
            SendMessage(GetDlgItem(hwndDlg, IDC_MESSAGE), WM_SETFONT, (WPARAM)FontHandle, FALSE);

            // Center the update window on PH if visible and not mimimized else center on desktop.
            PhCenterWindow(hwndDlg, (IsWindowVisible(GetParent(hwndDlg)) && !IsIconic(GetParent(hwndDlg))) ? GetParent(hwndDlg) : NULL);

            // Create our update check thread.
            UpdateCheckThreadHandle = PhCreateThread(0, (PUSER_THREAD_START_ROUTINE)CheckUpdateThreadStart, hwndDlg);
        }
        break;
    case WM_SHOWDIALOG:
        {
            if (IsIconic(hwndDlg))
                ShowWindow(hwndDlg, SW_RESTORE);
            else
                ShowWindow(hwndDlg, SW_SHOW);

            SetForegroundWindow(hwndDlg);
        }
        break;
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
        {
            HDC hDC = (HDC)wParam;
            HWND hwndChild = (HWND)lParam;

            // Check for our static label and change the color.
            if (GetDlgCtrlID(hwndChild) == IDC_MESSAGE)
            {
                SetTextColor(hDC, RGB(19, 112, 171));
            }

            // Set a transparent background for the control backcolor.
            SetBkMode(hDC, TRANSPARENT);

            // set window background color.
            return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
        }
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDCANCEL:
            case IDOK:
                {
                    PostQuitMessage(0);
                }
                break;
            case IDC_DOWNLOAD:
                {
                    switch (PhUpdaterState)
                    {
                    case Download:
                        {
                            if (PhInstalledUsingSetup())
                            {
                                // Start our Downloader thread
                                DownloadThreadHandle = PhCreateThread(0, (PUSER_THREAD_START_ROUTINE)DownloadUpdateThreadStart, hwndDlg);
                            }
                            else
                            {
                                // Let the user handle non-setup installation, show the homepage and close this dialog.
                                PhShellExecute(hwndDlg, L"http://processhacker.sourceforge.net/downloads.php", NULL);

                                PostQuitMessage(0);
                            }
                        }
                        break;
                    case Install:
                        {
                            SHELLEXECUTEINFO info = { sizeof(SHELLEXECUTEINFO) };
                            info.lpFile = SetupFilePath->Buffer;
                            info.lpVerb = L"runas";
                            info.nShow = SW_SHOW;
                            info.hwnd = hwndDlg;

                            ProcessHacker_PrepareForEarlyShutdown(PhMainWndHandle);

                            if (!ShellExecuteEx(&info))
                            {
                                // Install failed, cancel the shutdown.
                                ProcessHacker_CancelEarlyShutdown(PhMainWndHandle);

                                // Set button text for next action
                                Button_SetText(GetDlgItem(hwndDlg, IDC_DOWNLOAD), L"Retry");
                            }
                            else
                            {
                                ProcessHacker_Destroy(PhMainWndHandle);
                            }
                        }
                        break;
                    }
                }
                break;
            }
            break;
        }
        break;
    case WM_UPDATE:
        {
            if (IsUpdating)
            {
                DWORD time_taken;
                DWORD download_speed;        
                //DWORD time_remain = (MulDiv(time_taken, contentLength, bytesDownloaded) - time_taken);
                int percent;
                PPH_STRING dlRemaningBytes;
                PPH_STRING dlLength;
                PPH_STRING dlSpeed;
                PPH_STRING statusText;

                PhAcquireQueuedLockExclusive(&Lock);

                time_taken = (GetTickCount() - timeTransferred);
                download_speed = (bytesDownloaded / max(time_taken, 1));  
                percent = MulDiv(100, bytesDownloaded, contentLength);

                dlRemaningBytes = PhFormatSize(bytesDownloaded, -1);
                dlLength = PhFormatSize(contentLength, -1);
                dlSpeed = PhFormatSize(download_speed * 1024, -1);

                LastUpdateTime = GetTickCount();

                PhReleaseQueuedLockExclusive(&Lock);

                statusText = PhFormatString(
                    L"%s (%d%%) of %s @ %s/s",
                    dlRemaningBytes->Buffer,
                    percent,
                    dlLength->Buffer,
                    dlSpeed->Buffer
                    );

                SetDlgItemText(hwndDlg, IDC_STATUS, statusText->Buffer);
                SendDlgItemMessage(hwndDlg, IDC_PROGRESS, PBM_SETPOS, percent, 0);

                PhDereferenceObject(statusText);
                PhDereferenceObject(dlSpeed);
                PhDereferenceObject(dlLength);
                PhDereferenceObject(dlRemaningBytes);

                
                IsUpdating = FALSE; 
            }  
        }
        break;
    }

    return FALSE;
}

VOID LogEvent(
    __in_opt HWND hwndDlg,
    __in __post_invalid PPH_STRING str
    )
{
    if (str)
    {
        if (hwndDlg)
        {
            SetDlgItemText(hwndDlg, IDC_STATUS, str->Buffer);
        }
        else
        {
            PhLogMessageEntry(PH_LOG_ENTRY_MESSAGE, str);
        }

        PhDereferenceObject(str);
    }
}

mxml_type_t QueryXmlDataCallback(
    __in mxml_node_t *node
    )
{
    return MXML_OPAQUE;
}

VOID ShowUpdateDialog(
    VOID
    )
{
    if (!UpdaterDialogThreadHandle)
    {
        if (!(UpdaterDialogThreadHandle = PhCreateThread(0, (PUSER_THREAD_START_ROUTINE)ShowUpdateDialogThreadStart, NULL)))
        {
            PhShowStatus(PhMainWndHandle, L"Unable to create the updater window.", 0, GetLastError());
            return;
        }

        PhWaitForEvent(&InitializedEvent, NULL);
    }

    SendMessage(UpdateDialogHandle, WM_SHOWDIALOG, 0, 0);
}

VOID StartInitialCheck(
    VOID
    )
{
    // Queue up our initial update check.
    PhCreateThread(0, SilentUpdateCheckThreadStart, NULL);
}