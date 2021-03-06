/*
* Process Hacker Extra Plugins -
*   Plugin Manager
*
* Copyright (C) 2016-2017 dmex
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

#include "main.h"
#include "miniz\miniz.h"

ULONGLONG ParseVersionString(
    _In_ PPH_STRING Version
    )
{
    PH_STRINGREF remainingPart, majorPart, minorPart, revisionPart, reservedPart;
    ULONG64 majorInteger = 0, minorInteger = 0, revisionInteger = 0, reservedInteger = 0;

    PhInitializeStringRef(&remainingPart, PhGetString(Version));
    PhSplitStringRefAtChar(&remainingPart, '.', &majorPart, &remainingPart);
    PhSplitStringRefAtChar(&remainingPart, '.', &minorPart, &remainingPart);
    PhSplitStringRefAtChar(&remainingPart, '.', &revisionPart, &remainingPart);
    PhSplitStringRefAtChar(&remainingPart, '.', &reservedPart, &remainingPart);

    PhStringToInteger64(&majorPart, 10, &majorInteger);
    PhStringToInteger64(&minorPart, 10, &minorInteger);
    PhStringToInteger64(&revisionPart, 10, &revisionInteger);
    PhStringToInteger64(&reservedPart, 10, &reservedInteger);

    return MAKE_VERSION_ULONGLONG(majorInteger, minorInteger, reservedInteger, revisionInteger);
}

NTSTATUS QueryPluginsCallbackThread(
    _In_ PVOID Parameter
    )
{
    HINTERNET httpSessionHandle = NULL;
    HINTERNET httpConnectionHandle = NULL;
    HINTERNET httpRequestHandle = NULL;
    ULONG xmlStringBufferLength = 0;
    PSTR xmlStringBuffer = NULL;
    PVOID rootJsonObject = NULL;
    PWCT_CONTEXT context = Parameter;

    if (!(httpSessionHandle = WinHttpOpen(
        L"ExtraPlugins_1.0",
        WindowsVersion >= WINDOWS_8_1 ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
        )))
    {
        goto CleanupExit;
    }

    if (!(httpConnectionHandle = WinHttpConnect(
        httpSessionHandle,
        L"wj32.org",
        INTERNET_DEFAULT_HTTP_PORT,
        0
        )))
    {
        goto CleanupExit;
    }

    if (!(httpRequestHandle = WinHttpOpenRequest(
        httpConnectionHandle,
        NULL,
        L"/processhacker/plugins/list.php",
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_REFRESH
        )))
    {
        goto CleanupExit;
    }

    if (!WinHttpSendRequest(
        httpRequestHandle,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH,
        0
        ))
    {
        goto CleanupExit;
    }

    if (!WinHttpReceiveResponse(httpRequestHandle, NULL))
        goto CleanupExit;

    if (!ReadRequestString(httpRequestHandle, &xmlStringBuffer, &xmlStringBufferLength))
        goto CleanupExit;

    if (!(rootJsonObject = PhCreateJsonParser(xmlStringBuffer)))
        goto CleanupExit;

    for (INT i = 0; i < PhGetJsonArrayLength(rootJsonObject); i++)
    {
        PVOID jvalue;
        PPLUGIN_NODE entry;
        SYSTEMTIME time = { 0 };
        SYSTEMTIME localTime = { 0 };
        PH_STRINGREF pluginBaseName;
        PPH_STRING pluginDllPath;

        entry = PhCreateAlloc(sizeof(PLUGIN_NODE));
        memset(entry, 0, sizeof(PLUGIN_NODE));

        jvalue = PhGetJsonArrayIndexObject(rootJsonObject, i);
        entry->Id = PhGetJsonValueAsString(jvalue, "plugin_id");
        entry->InternalName = PhGetJsonValueAsString(jvalue, "plugin_internal_name");
        entry->Name = PhGetJsonValueAsString(jvalue, "plugin_name");
        entry->Version = PhGetJsonValueAsString(jvalue, "plugin_version");
        entry->Author = PhGetJsonValueAsString(jvalue, "plugin_author");
        entry->Description = PhGetJsonValueAsString(jvalue, "plugin_description");
        entry->IconUrl = PhGetJsonValueAsString(jvalue, "plugin_icon");
        entry->Requirements = PhGetJsonValueAsString(jvalue, "plugin_requirements");
        entry->FeedbackUrl = PhGetJsonValueAsString(jvalue, "plugin_feedback");
        entry->Screenshots = PhGetJsonValueAsString(jvalue, "plugin_screenshots");
        entry->AddedTime = PhGetJsonValueAsString(jvalue, "plugin_datetime_added");
        entry->UpdatedTime = PhGetJsonValueAsString(jvalue, "plugin_datetime_updated");
        entry->Download_count = PhGetJsonValueAsString(jvalue, "plugin_download_count");
        entry->Download_link_32 = PhGetJsonValueAsString(jvalue, "plugin_download_link_32");
        entry->Download_link_64 = PhGetJsonValueAsString(jvalue, "plugin_download_link_64");
        entry->SHA2_32 = PhGetJsonValueAsString(jvalue, "plugin_hash_32");
        entry->SHA2_64 = PhGetJsonValueAsString(jvalue, "plugin_hash_64");
        entry->HASH_32 = PhGetJsonValueAsString(jvalue, "plugin_signed_32");
        entry->HASH_64 = PhGetJsonValueAsString(jvalue, "plugin_signed_64");
        entry->FileName = PhGetJsonValueAsString(jvalue, "plugin_filename");

        swscanf(
            PhGetString(entry->UpdatedTime),
            L"%hu-%hu-%hu %hu:%hu:%hu",
            &time.wYear,
            &time.wMonth,
            &time.wDay,
            &time.wHour,
            &time.wMinute,
            &time.wSecond
            );

        if (SystemTimeToTzSpecificLocalTime(NULL, &time, &localTime))
        {
            entry->UpdatedTime = PhFormatDateTime(&localTime);
        }

        PPH_STRING directory = PhGetApplicationDirectory();
        pluginDllPath = PhConcatStrings(3, PhGetString(directory), L"Plugins\\", PhGetString(entry->FileName));
        PhDereferenceObject(directory);

        PhInitializeStringRefLongHint(&pluginBaseName, PhGetString(entry->FileName));
   
        if (PhIsPluginDisabled(&pluginBaseName))
            goto CleanupExit;

        if (RtlDoesFileExists_U(PhGetString(pluginDllPath)))
        {
            ULONG versionSize;
            PVOID versionInfo;
            PUSHORT languageInfo;
            UINT language;
            UINT bufferSize = 0;
            PWSTR buffer = NULL;
            PPH_STRING internalName = NULL;
            PPH_STRING version = NULL;

            entry->FilePath = PhCreateString2(&pluginDllPath->sr);

            versionSize = GetFileVersionInfoSize(PhGetString(entry->FilePath), NULL);
            versionInfo = PhAllocate(versionSize);
            memset(versionInfo, 0, versionSize);

            if (GetFileVersionInfo(PhGetString(entry->FilePath), 0, versionSize, versionInfo))
            {
                if (VerQueryValue(versionInfo, L"\\", &buffer, &bufferSize))
                {
                    VS_FIXEDFILEINFO* info = (VS_FIXEDFILEINFO*)buffer;

                    if (info->dwSignature == 0xfeef04bd)
                    {
                        version = PhFormatString(
                            L"%lu.%lu.%lu.%lu",
                            (info->dwFileVersionMS >> 16) & 0xffff,
                            (info->dwFileVersionMS >> 0) & 0xffff,
                            (info->dwFileVersionLS >> 16) & 0xffff,
                            (info->dwFileVersionLS >> 0) & 0xffff
                            );
                    }
                }

                if (VerQueryValue(versionInfo, L"\\VarFileInfo\\Translation", &languageInfo, &language))
                {
                    PPH_STRING internalNameString = PhFormatString(
                        L"\\StringFileInfo\\%04x%04x\\InternalName",
                        languageInfo[0],
                        languageInfo[1]
                        );

                    if (VerQueryValue(versionInfo, PhGetStringOrEmpty(internalNameString), &buffer, &bufferSize))
                    {
                        internalName = PhCreateStringEx(buffer, bufferSize * sizeof(WCHAR));
                    }

                    PhDereferenceObject(internalNameString);
                }
            }

            PhFree(versionInfo);

            if (entry->PluginInstance = (PPHAPP_PLUGIN)PhFindPlugin(PhGetString(entry->InternalName)))
            {
                ULONGLONG currentVersion = ParseVersionString(version);
                ULONGLONG latestVersion = ParseVersionString(entry->Version);

                entry->PluginOptions = entry->PluginInstance->Information.HasOptions;

                if (currentVersion < latestVersion)
                {
                    entry->State = PLUGIN_STATE_UPDATE;
                    PostMessage(context->DialogHandle, ID_UPDATE_ADD, 0, (LPARAM)entry);
                }
            }
            else
            {
                entry->State = PLUGIN_STATE_RESTART;
                PostMessage(context->DialogHandle, ID_UPDATE_ADD, 0, (LPARAM)entry);
            }
        }
        else
        {   
            entry->State = PLUGIN_STATE_REMOTE; 
            PostMessage(context->DialogHandle, ID_UPDATE_ADD, 0, (LPARAM)entry);
        }
    }

CleanupExit:

    if (rootJsonObject)
        PhFreeJsonParser(rootJsonObject);

    if (httpRequestHandle)
        WinHttpCloseHandle(httpRequestHandle);

    if (httpConnectionHandle)
        WinHttpCloseHandle(httpConnectionHandle);

    if (httpSessionHandle)
        WinHttpCloseHandle(httpSessionHandle);

    if (xmlStringBuffer)
        PhFree(xmlStringBuffer);

    PostMessage(context->DialogHandle, ID_UPDATE_COUNT, 0, 0);

    return STATUS_SUCCESS;
}

NTSTATUS SetupExtractBuild(
    _In_ PVOID Parameter
    )
{
    static PH_STRINGREF pluginsDirectory = PH_STRINGREF_INIT(L"plugins\\");
    mz_bool status = MZ_FALSE;
    mz_zip_archive zip_archive;
    PPH_UPDATER_CONTEXT context = (PPH_UPDATER_CONTEXT)Parameter;

    memset(&zip_archive, 0, sizeof(zip_archive));

    // TODO: Move existing folder items.

    if (!(status = mz_zip_reader_init_file(&zip_archive, PhGetStringOrEmpty(context->SetupFilePath), 0)))
    {
        goto error;
    }

    for (ULONG i = 0; i < mz_zip_reader_get_num_files(&zip_archive); i++)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&zip_archive, i, &stat))
        {
            continue;
        }

        if (mz_zip_reader_is_file_a_directory(&zip_archive, i))
        {
            PPH_STRING directory;
            PPH_STRING fileName;
            PPH_STRING fullSetupPath;
            PPH_STRING extractPath;
            ULONG indexOfFileName = -1;

            fileName = PhConvertUtf8ToUtf16(stat.m_filename);
            directory = PhGetApplicationDirectory();
            extractPath = PhConcatStringRef3(&directory->sr, &pluginsDirectory, &fileName->sr);
            fullSetupPath = PhGetFullPath(PhGetStringOrEmpty(extractPath), &indexOfFileName);

            PhCreateDirectory(fullSetupPath);

            PhDereferenceObject(fullSetupPath);
            PhDereferenceObject(extractPath);
            PhDereferenceObject(directory);
            PhDereferenceObject(fileName);
        }
        else
        {
            PPH_STRING directory;
            PPH_STRING fileName;
            PPH_STRING fullSetupPath;
            PPH_STRING extractPath;
            PPH_STRING directoryPath;
            PPH_STRING fileNameString;
            ULONG indexOfFileName = -1;

            fileName = PhConvertUtf8ToUtf16(stat.m_filename);
            directory = PhGetApplicationDirectory();
            extractPath = PhConcatStringRef3(&directory->sr, &pluginsDirectory, &fileName->sr);
            fullSetupPath = PhGetFullPath(PhGetStringOrEmpty(extractPath), &indexOfFileName);
            fileNameString = PhConcatStrings(2, fullSetupPath->Buffer, L".bak");

            if (indexOfFileName != -1)
            {
                if (directoryPath = PhSubstring(fullSetupPath, 0, indexOfFileName))
                {
                    PhCreateDirectory(directoryPath);

                    PhDereferenceObject(directoryPath);
                }
            }

            if (RtlDoesFileExists_U(PhGetStringOrEmpty(fullSetupPath)))
            {
                MoveFileEx(PhGetString(fullSetupPath), PhGetString(fileNameString), MOVEFILE_REPLACE_EXISTING);
            }

            if (!mz_zip_reader_extract_to_file(&zip_archive, i, PhGetString(fullSetupPath), 0))
            {
                goto error;
            }

            PhDereferenceObject(fileNameString);
            PhDereferenceObject(fullSetupPath);
            PhDereferenceObject(extractPath);
            PhDereferenceObject(directory);
            PhDereferenceObject(fileName);
        }
    }

    mz_zip_reader_end(&zip_archive);
    return STATUS_SUCCESS;

error:
    mz_zip_reader_end(&zip_archive);
    return STATUS_FAIL_CHECK;
}