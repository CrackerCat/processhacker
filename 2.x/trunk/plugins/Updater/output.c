/*
* Process Hacker Update Checker - 
*   main program
* 
* Copyright (C) 2011 dmex
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

#include "updater.h"

mxml_type_t PhpSettingsLoadCallback(
    __in mxml_node_t *node
    )
{
    return MXML_OPAQUE;
}

static NTSTATUS WorkerThreadStart(
	__in PVOID Parameter
	)
{
	int result;
	DWORD dwBytes;
	mxml_node_t *xmlNode, *xmlNode2, *xmlNode3, *xmlNode4;
	PPH_STRING local;
	DWORD dwContentLen = 0;				
	int xPercent = 0;	
	DWORD dwBufLen = sizeof(BUFFER_LEN);
	DWORD dwBytesRead = 0, dwBytesWritten = 0;
	HWND hwndDlg = (HWND)Parameter;
	
	DisposeHandles();

	local = PhGetPhVersion();

	InitializeConnection(FALSE, L"processhacker.sourceforge.net", L"/updater.php");

	// Send the HTTP request.
	if (HttpSendRequest(file, NULL, 0, NULL, 0))
	{
		if (HttpQueryInfo(file, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, (LPVOID)&dwContentLen, &dwBufLen, 0))
		{
			char *buffer = (char*)PhAllocate(BUFFER_LEN);
			PPH_STRING summaryText;
			// Read the resulting xml into our buffer.
			while (InternetReadFile(file, buffer, BUFFER_LEN, &dwBytes))
			{
				if (dwBytes == 0)
				{
					// We're done.
					break;
				}
			}

			// Load our XML.
			xmlNode = mxmlLoadString(NULL, buffer, MXML_OPAQUE_CALLBACK);

			// Check our XML.
			if (xmlNode->type != MXML_ELEMENT)
			{
				mxmlDelete(xmlNode);

				return STATUS_FILE_CORRUPT_ERROR;
			}

			// Find the ver node.
			xmlNode2 = mxmlFindElement(xmlNode, xmlNode, "ver", NULL, NULL, MXML_DESCEND);
			// Find the reldate node.
			xmlNode3 = mxmlFindElement(xmlNode, xmlNode, "reldate", NULL, NULL, MXML_DESCEND);
			// Find the size node.
			xmlNode4 = mxmlFindElement(xmlNode, xmlNode, "size", NULL, NULL, MXML_DESCEND);

			result = VersionParser("2.10", "2.20");

			switch (result)
			{
			case 1:
				{
					PPH_STRING summaryText;
					// create a PPH_STRING from our ANSI node.
					summaryText = PhCreateStringFromAnsi(xmlNode2->child->value.opaque);	
					summaryText = PhFormatString(L"Process Hacker %s is available.", summaryText->Buffer);
					SetDlgItemText(hwndDlg, IDC_MESSAGE, summaryText->Buffer);

					PhDereferenceObject(summaryText);

					// create a PPH_STRING from our ANSI node.
					summaryText = PhCreateStringFromAnsi(xmlNode3->child->value.opaque);	
					summaryText = PhFormatString(L"Released: %s", summaryText->Buffer);
					SetDlgItemText(hwndDlg, IDC_MESSAGE2, summaryText->Buffer);
					
					PhDereferenceObject(summaryText);

					// create a PPH_STRING from our ANSI node.
					summaryText = PhCreateStringFromAnsi(xmlNode4->child->value.opaque);	
					summaryText = PhFormatString(L"Size: %s", summaryText->Buffer);
					SetDlgItemText(hwndDlg, IDC_MESSAGE3, summaryText->Buffer);

					PhDereferenceObject(summaryText);

					ShowWindow(GetDlgItem(hwndDlg, IDYES), SW_SHOW);
				}
				break;
			case 0:
				{
					PPH_STRING summaryText = PhFormatString(L"You're running the latest version: %s", remoteVersion->Buffer);
					SetDlgItemText(hwndDlg, IDC_MESSAGE, summaryText->Buffer);

					PhDereferenceObject(summaryText);
				}
				break;
			case -1:
				{
					PPH_STRING summaryText = PhFormatString(L"You're running a newer version: %s", local->Buffer);
					SetDlgItemText(hwndDlg, IDC_MESSAGE, summaryText->Buffer);

					PhDereferenceObject(summaryText);
				}
				break;
			default:
				{	
					PPH_STRING summaryText = PhFormatString(L"You're running a newer version: %s", local->Buffer);
					SetDlgItemText(hwndDlg, IDC_MESSAGE, summaryText->Buffer);
				
					PhDereferenceObject(summaryText);

					LogEvent(L"Updater: Update check unknown result: %s", result);
				}
				break;
			}
		}
	}
	else
	{
		NTSTATUS status = PhGetLastWin32ErrorAsNtStatus();

		DisposeHandles();

		LogEvent(L"Updater: HttpSendRequest failed (%d)", status);

		return status;
	}

	PhDereferenceObject(local);

	return STATUS_SUCCESS;
}

static NTSTATUS DownloadWorkerThreadStart(
	__in PVOID Parameter
	)
{
	HANDLE dlFile;
	UINT dwRetVal = 0;
    TCHAR lpTempPathBuffer[MAX_PATH];
	HWND hwndDlg = (HWND)Parameter;
	HWND hwndProgress = GetDlgItem(hwndDlg,IDC_PROGRESS1);

	DisposeHandles();

	// Get the temp path env string (no guarantee it's a valid path).
	dwRetVal = GetTempPath(MAX_PATH, lpTempPathBuffer);

	if (dwRetVal > MAX_PATH || dwRetVal == 0)
	{
		NTSTATUS result = PhGetLastWin32ErrorAsNtStatus();

		LogEvent(PhFormatString(TEXT("Updater: GetTempPath failed (%d)"), result));

		return result;
	}	

	localFilePath = PhConcatStrings2(lpTempPathBuffer, L"processhacker-2.19-setup.exe");

	InitializeConnection(FALSE, L"sourceforge.net", L"/projects/processhacker/files/processhacker2/processhacker-2.19-setup.exe/download");

	// Open output file
	dlFile = CreateFile(
		localFilePath->Buffer,
		GENERIC_WRITE,
		FILE_SHARE_WRITE,
		0,                     // handle cannot be inherited
		CREATE_ALWAYS,         // if file exists, delete it
		FILE_ATTRIBUTE_NORMAL,
		0);

	if (dlFile == INVALID_HANDLE_VALUE)
	{
		return PhGetLastWin32ErrorAsNtStatus();
	}

	SetDlgItemText(hwndDlg, IDC_STATUS, L"Connecting");

	// Send the HTTP request.
	if (HttpSendRequest(file, NULL, 0, NULL, 0))
	{
		DWORD dwContentLen = 0;
		DWORD dwBufLen = sizeof(dwContentLen);
		DWORD dwBytesRead = 0, dwBytesWritten = 0;
		INT xPercent = 0;

		if (HttpQueryInfo(file, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, (LPVOID)&dwContentLen, &dwBufLen, 0))
		{
			char *buffer = (char*)PhAllocate(BUFFER_LEN);
			DWORD dwTotalReadSize = 0;

			// Reset Progressbar state.
			PhSetWindowStyle(hwndProgress, PBS_MARQUEE, 0);

			while (InternetReadFile(file, buffer, BUFFER_LEN, &dwBytesRead))
			{
				if (dwBytesRead == 0)
				{
					// We're done.
					break;
				}

				dwTotalReadSize += dwBytesRead;
				xPercent = (int)(((double)dwTotalReadSize / (double)dwContentLen) * 100);

				SendMessage(hwndProgress, PBM_SETPOS, xPercent, 0);
				{
					PPH_STRING str;
					PPH_STRING dlCurrent = PhFormatSize(dwTotalReadSize, -1);
					PPH_STRING dlLength = PhFormatSize(dwContentLen, -1);

					str = PhFormatString(L"Downloaded: %d%% (%s)", xPercent, dlCurrent->Buffer);

					SetDlgItemText(hwndDlg, IDC_STATUS, str->Buffer);

					PhDereferenceObject(str);
					PhDereferenceObject(dlCurrent);
					PhDereferenceObject(dlLength);
				}

				if (!WriteFile(dlFile, buffer, dwBytesRead, &dwBytesWritten, NULL)) 
				{
					LogEvent(PhFormatString(L"Updater: (DownloadWorkerThreadStart) WriteFile failed (%d)", PhGetLastWin32ErrorAsNtStatus()));
					break;
				}

				if (dwBytesRead != dwBytesWritten) 
				{	
					LogEvent(PhFormatString(L"Updater: (DownloadWorkerThreadStart) WriteFile dwBytesRead != dwBytesWritte (%d)", PhGetLastWin32ErrorAsNtStatus()));
					break;                
				}

			}

			PhFree(buffer);
		}
		else
		{
			// DWORD err = GetLastError();

			// No content length...impossible to calculate % complete so just read until we are done.
			DWORD dwBytesRead = 0;
			DWORD dwBytesWritten = 0;
			char *buffer = (char*)PhAllocate(BUFFER_LEN);
		
			while (InternetReadFile(file, buffer, BUFFER_LEN, &dwBytesRead))
			{	
				if (dwBytesRead == 0)
				{
					// We're done.
					break;
				}

				if (!WriteFile(dlFile, buffer, dwBytesRead, &dwBytesWritten, NULL)) 
				{
					LogEvent(PhFormatString(L"Updater: (DownloadWorkerThreadStart) WriteFile failed (%d)", PhGetLastWin32ErrorAsNtStatus()));

					break;
				}

				if (dwBytesRead != dwBytesWritten) 
				{
					// File write error
					break;                
				}
			}

			PhFree(buffer);
		}
	}
	else
	{
		NTSTATUS result = PhGetLastWin32ErrorAsNtStatus();

		LogEvent(PhFormatString(L"Updater: (DownloadWorkerThreadStart) HttpSendRequest failed (%d)", result));

		return result;
	}

	if (dlFile)
		NtClose(dlFile);  

	DisposeHandles();

	SetDlgItemText(hwndDlg, IDC_STATUS, L"Download Complete");

	SetWindowText(GetDlgItem(hwndDlg, IDYES), L"Install");
	EnableWindow(GetDlgItem(hwndDlg, IDYES), TRUE);

	Install = TRUE;

	return STATUS_SUCCESS;
}

INT_PTR CALLBACK NetworkOutputDlgProc(      
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
			PhCenterWindow(hwndDlg, GetParent(hwndDlg));
			PhCreateThread(0, (PUSER_THREAD_START_ROUTINE)WorkerThreadStart, hwndDlg);  
			Install = FALSE;
		}
		break;
	case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
			case IDCANCEL:
			case IDOK:
				{
					DisposeHandles();

					EndDialog(hwndDlg, IDOK);
				}
				break;
			case IDYES:
				{
					if (Install)
					{
						PhShellExecute(hwndDlg, localFilePath->Buffer, NULL);
						DisposeHandles();
						
						ProcessHacker_Destroy(PhMainWndHandle);
						return FALSE;
					}

					if (PhInstalledUsingSetup())
					{	
						HWND hwndProgress = GetDlgItem(hwndDlg,IDC_PROGRESS1);

						// Enable the progressbar
						ShowWindow(GetDlgItem(hwndDlg, IDC_PROGRESS1), SW_SHOW);
			            // Enable the status text
						ShowWindow(GetDlgItem(hwndDlg, IDC_STATUS), SW_SHOW);
						// Enable the Download/Install button
						EnableWindow(GetDlgItem(hwndDlg, IDYES), FALSE);
	
						SetDlgItemText(hwndDlg, IDC_STATUS, L"Initializing");

						PhSetWindowStyle(hwndProgress, PBS_MARQUEE, PBS_MARQUEE);
						PostMessage(hwndProgress, PBM_SETMARQUEE, TRUE, 75);

						// Star our Downloader thread
						PhCreateThread(0, (PUSER_THREAD_START_ROUTINE)DownloadWorkerThreadStart, hwndDlg);   
					}
					else
					{
						NTSTATUS result = PhGetLastWin32ErrorAsNtStatus();

						LogEvent(PhFormatString(L"Updater: PhInstalledUsingSetup failed: %d", result));
					}
				}
				break;
			}
		}
		break;
	}

	return FALSE;
}

VOID InitializeConnection(BOOL useCache, PCWSTR host, PCWSTR path)
{
		// Initialize the wininet library.
	initialize = InternetOpen(
		L"PH Updater", // user-agent
		INTERNET_OPEN_TYPE_PRECONFIG, // use system proxy configuration	 
		NULL, 
		NULL, 
		0
		);

	if (!initialize)
	{
		LogEvent(PhFormatString(L"Updater: (InitializeConnection) InternetOpen failed (%d)", PhGetLastWin32ErrorAsNtStatus()));
	}

	// Connect to the server.
	connection = InternetConnect(
		initialize,
		host, 
		INTERNET_DEFAULT_HTTP_PORT,
		NULL, 
		NULL, 
		INTERNET_SERVICE_HTTP, 
		0, 
		0
		);

	if (!connection)
	{
		LogEvent(PhFormatString(L"Updater: (InitializeConnection) InternetConnect failed (%d)", PhGetLastWin32ErrorAsNtStatus()));
	}
	
	// Open the HTTP request.
	file = HttpOpenRequest(
		connection, 
		NULL, 
		path, 
		NULL, 
		NULL, 
		NULL, 
		useCache ? INTERNET_FLAG_DONT_CACHE : 0, // Specify this flag here to disable Internet* API caching.
		0
		);

	if (!file)
	{
		LogEvent(PhFormatString(L"Updater: (InitializeConnection) HttpOpenRequest failed (%d)", PhGetLastWin32ErrorAsNtStatus()));
	}
}


INT VersionParser(char* version1, char* version2) 
{
	INT i = 0, a1 = 0, b1 = 0, ret = 0;
	size_t a = strlen(version1); 
	size_t b = strlen(version2);

	if (b > a) 
		a = b;

	for (i = 0; i < a; i++) 
	{
		a1 += version1[i];
		b1 += version2[i];
	}

	if (b1 > a1)
	{
		// second version is fresher
		ret = 1; 
	}
	else if (b1 == a1) 
	{
		// versions is equal
		ret = 0;
	}
	else 
	{
		// first version is fresher
		ret = -1; 
	}

	return ret;
}

BOOL PhInstalledUsingSetup() 
{
	LONG result;
	HKEY hKey;

	// Check uninstall entries for the 'Process_Hacker2_is1' registry key.
	result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Process_Hacker2_is1", 0, KEY_QUERY_VALUE, &hKey);

	// Cleanup
	NtClose(hKey);

	if (result != ERROR_SUCCESS)
		return FALSE;
	
	return TRUE;
}

VOID LogEvent(PPH_STRING str)
{
	PhLogMessageEntry(PH_LOG_ENTRY_MESSAGE, str);
	
	OutputDebugString(str->Buffer);
	
	PhDereferenceObject(str);
}

VOID DisposeHandles()
{
	if (file)
		InternetCloseHandle(file);

	if (connection)
		InternetCloseHandle(connection);

	if (initialize)
		InternetCloseHandle(initialize);

	//if (remoteVersion)
		//PhDereferenceObject(remoteVersion);

	//if (localFilePath)
		//PhDereferenceObject(localFilePath);
}