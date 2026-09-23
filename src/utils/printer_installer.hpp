#pragma once

/**
 * =========================================================================================
 * @file printer_installer.hpp
 * @brief Windows virtual printer management for FolioNote ("Print to FolioNote").
 * =========================================================================================
 *
 * ARCHITECTURAL PURPOSE & DESIGN RATIONALE:
 * 1. Virtual Printing Pipeline:
 *    Windows 10 and 11 provide the built-in "Microsoft Print to PDF" driver (ntprint.inf).
 *    FolioNote registers a specialized virtual printer named "Print to FolioNote" using this
 *    underlying driver. Any Windows application (browsers, office suites, CAD viewers) can
 *    print directly to FolioNote.
 *
 * 2. Automatic Import Routing & Event Dispatch:
 *    - To automatically route printed documents into FolioNote without manual file picking,
 *      we enable the Windows Print Service Operational event log (`wevtutil sl ...`).
 *    - When a document finishes spooling, the Windows Spooler raises Event 307.
 *    - In Event 307, `Param2` captures the source document title, while `Param5` identifies
 *      the printer name.
 *    - A Windows Scheduled Task (`FolioNotePrintDispatch`) subscribes to Event 307 for
 *      "Print to FolioNote", automatically launching FolioNote with `--import` and passing
 *      the document title.
 *
 * 3. UAC Administrator Privilege Escalation:
 *    - Configuring the Windows print spooler (adding printers, ports, or modifying event logs)
 *      requires administrative privileges (High Integrity Level).
 *    - FolioNote normally runs under standard user permissions (Medium Integrity Level).
 *    - `InstallPrinterElevated` and `UninstallPrinterElevated` use `ShellExecuteExW` with the
 *      verb `L"runas"` to prompt the user once via the standard Windows UAC dialog to execute
 *      the required PowerShell configuration task.
 */

#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <winspool.h>
#endif

namespace Folio {

class PrinterInstaller {
public:
    /**
     * @brief Checks whether the "Print to FolioNote" virtual printer is registered in the spooler.
     * 
     * Working Mechanism:
     * - Queries the local print spooler subsystem via the Win32 Spooler API `OpenPrinterW`.
     * - Returns true if the printer handle is acquired successfully.
     * 
     * @param printerName The friendly name of the printer to verify (default: L"Print to FolioNote").
     * @return true if the printer is installed and accessible in the Windows Spooler; false otherwise.
     */
    static bool IsPrinterInstalled(const std::wstring& printerName = L"Print to FolioNote") {
#if defined(_WIN32)
        PRINTER_DEFAULTSW defaults = { NULL, NULL, PRINTER_READ };
        HANDLE hPrinter = NULL;
        BOOL success = OpenPrinterW(const_cast<LPWSTR>(printerName.c_str()), &hPrinter, &defaults);
        if (success && hPrinter != NULL) {
            ClosePrinter(hPrinter);
            return true;
        }
        return false;
#else
        (void)printerName;
        return false;
#endif
    }

    /**
     * @brief Elevates privileges via UAC and installs the "Print to FolioNote" virtual printer
     *        alongside its automated event-dispatch scheduled task.
     *
     * Working Process:
     * 1. Resolves the absolute path to the current running FolioNote binary.
     * 2. Enables the `Microsoft-Windows-PrintService/Operational` channel.
     * 3. Registers the printer targeting the 'Microsoft Print To PDF' driver.
     * 4. Registers the `FolioNotePrintDispatch` scheduled task triggered by Event 307,
     *    routing `Param2` (the document title) into FolioNote's `--import` CLI pipeline.
     *
     * @param printerName Name of the virtual printer to register (default: L"Print to FolioNote").
     * @return true if installation process exited successfully (exit code 0); false otherwise.
     */
    static bool InstallPrinterElevated(const std::wstring& printerName = L"Print to FolioNote") {
#if defined(_WIN32)
        // 1. Resolve absolute path of current executable to launch on print event
        std::wstring exePathW(MAX_PATH, L'\0');
        DWORD len = GetModuleFileNameW(NULL, &exePathW[0], static_cast<DWORD>(exePathW.size()));
        while (len >= exePathW.size()) {
            exePathW.resize(exePathW.size() * 2);
            len = GetModuleFileNameW(NULL, &exePathW[0], static_cast<DWORD>(exePathW.size()));
        }
        exePathW.resize(len);

        // Escape single quotes for safe PowerShell script block inclusion
        std::wstring escapedExe = L"";
        for (wchar_t c : exePathW) {
            if (c == L'\'') escapedExe += L"''";
            else escapedExe += c;
        }

        // PowerShell setup script:
        // a) Enable Microsoft-Windows-PrintService/Operational log
        // b) Register virtual printer with 'Microsoft Print To PDF' driver
        // c) Configure scheduled task triggered by Event 307 (spool completion)
        std::wstring psCommand = 
            L"-NoProfile -ExecutionPolicy Bypass -Command \""
            L"$pName = '" + printerName + L"'; "
            L"wevtutil sl 'Microsoft-Windows-PrintService/Operational' /e:true; "
            L"if (-not (Get-Printer -Name $pName -ErrorAction SilentlyContinue)) { "
            L"  Add-Printer -Name $pName -DriverName 'Microsoft Print To PDF' -PortName 'PORTPROMPT:'; "
            L"  Set-Printer -Name $pName -Comment 'Virtual PDF printer for importing documents into FolioNote'; "
            L"  Restart-Service -Name Spooler -Force -ErrorAction SilentlyContinue "
            L"}; "
            L"$appExe = '" + escapedExe + L"'; "
            L"$xml = @'`\r\n"
            L"<?xml version=`\"1.0`\" encoding=`\"UTF-16`\"?>`r`\n"
            L"<Task version=`\"1.2`\" xmlns=`\"http://schemas.microsoft.com/windows/2004/02/mit/task`\">`r`\n"
            L"  <RegistrationInfo><Description>Auto-dispatches printed PDFs into FolioNote</Description></RegistrationInfo>`r`\n"
            L"  <Triggers>`r`\n"
            L"    <EventTrigger>`r`\n"
            L"      <Enabled>true</Enabled>`r`\n"
            L"      <Subscription>&lt;QueryList&gt;&lt;Query Id=`\"0`\" Path=`\"Microsoft-Windows-PrintService/Operational`\"&gt;&lt;Select Path=`\"Microsoft-Windows-PrintService/Operational`\"&gt;*[System[EventID=307]] and *[UserData[DocumentPrinted[Param5='\" + printerName + \"']]]&lt;/Select&gt;&lt;/Query&gt;&lt;/QueryList&gt;</Subscription>`r`\n"
            L"      <ValueQueries>`r`\n"
            L"        <Value name=`\"DocTitle`\">Event/UserData/DocumentPrinted/Param2</Value>`r`\n"
            L"      </ValueQueries>`r`\n"
            L"    </EventTrigger>`r`\n"
            L"  </Triggers>`r`\n"
            L"  <Principals><Principal id=`\"Author`\"><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>`r`\n"
            L"  <Settings>`r`\n"
            L"    <MultipleInstancesPolicy>Parallel</MultipleInstancesPolicy>`r`\n"
            L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>`r`\n"
            L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>`r`\n"
            L"    <ExecutionTimeLimit>PT1M</ExecutionTimeLimit>`r`\n"
            L"    <Priority>7</Priority>`r`\n"
            L"  </Settings>`r`\n"
            L"  <Actions Context=`\"Author`\">`r`\n"
            L"    <Exec>`r`\n"
            L"      <Command>$appExe</Command>`r`\n"
            L"      <Arguments>--import-print `\"`$(DocTitle)`\"</Arguments>`r`\n"
            L"    </Exec>`r`\n"
            L"  </Actions>`r`\n"
            L"</Task>`r`\n"
            L"'@; "
            L"$xmlFile = [System.IO.Path]::GetTempFileName(); "
            L"[System.IO.File]::WriteAllText($xmlFile,$xml, [System.Text.Encoding]::Unicode); "
            L"schtasks.exe /create /tn 'FolioNotePrintDispatch' /xml $xmlFile /f; "
            L"Remove-Item -Path $xmlFile -Force -ErrorAction SilentlyContinue"
            L"\"";

        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.hwnd = NULL;
        sei.lpVerb = L"runas"; // Requests UAC administrator elevation
        sei.lpFile = L"powershell.exe";
        sei.lpParameters = psCommand.c_str();
        sei.nShow = SW_HIDE;

        if (ShellExecuteExW(&sei)) {
            if (sei.hProcess != NULL) {
                WaitForSingleObject(sei.hProcess, 30000);
                DWORD exitCode = 1;
                GetExitCodeProcess(sei.hProcess, &exitCode);
                CloseHandle(sei.hProcess);
                return (exitCode == 0);
            }
            return true;
        }
        return false;
#else
        (void)printerName;
        return false;
#endif
    }

    /**
     * @brief Elevates privileges via UAC and uninstalls the "Print to FolioNote" virtual printer
     *        and associated scheduled dispatch tasks.
     *
     * @param printerName Name of the printer to remove (default: L"Print to FolioNote").
     * @return true if uninstallation process exited successfully; false otherwise.
     */
    static bool UninstallPrinterElevated(const std::wstring& printerName = L"Print to FolioNote") {
#if defined(_WIN32)
        std::wstring psCommand = 
            L"-NoProfile -ExecutionPolicy Bypass -Command \""
            L"$pName = '" + printerName + L"'; "
            L"schtasks.exe /delete /tn 'FolioNotePrintDispatch' /f -ErrorAction SilentlyContinue; "
            L"if (Get-Printer -Name $pName -ErrorAction SilentlyContinue) { "
            L"  Remove-Printer -Name $pName -ErrorAction SilentlyContinue; "
            L"  Restart-Service -Name Spooler -Force -ErrorAction SilentlyContinue "
            L"}\"";

        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.hwnd = NULL;
        sei.lpVerb = L"runas"; // Requests UAC administrator elevation
        sei.lpFile = L"powershell.exe";
        sei.lpParameters = psCommand.c_str();
        sei.nShow = SW_HIDE;

        if (ShellExecuteExW(&sei)) {
            if (sei.hProcess != NULL) {
                WaitForSingleObject(sei.hProcess, 30000);
                DWORD exitCode = 1;
                GetExitCodeProcess(sei.hProcess, &exitCode);
                CloseHandle(sei.hProcess);
                return (exitCode == 0);
            }
            return true;
        }
        return false;
#else
        (void)printerName;
        return false;
#endif
    }
};

} // namespace Folio