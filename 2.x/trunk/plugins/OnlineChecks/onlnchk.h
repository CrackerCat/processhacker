#ifndef ONLNCHK_H
#define ONLNCHK_H

// main

#ifndef MAIN_PRIVATE
extern PPH_PLUGIN PluginInstance;
#endif

// upload

#define UPLOAD_SERVICE_VIRUSTOTAL 1

VOID UploadToOnlineService(
    __in HWND hWnd,
    __in PPH_STRING FileName,
    __in ULONG Service
    );

#endif
