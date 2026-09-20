#pragma once

/**
 * @file printer_installer.hpp
 * @brief Windows virtual printer management for FolioNote ("Print to FolioNote").
 *
 * GENERAL WORKING PROCESS & ARCHITECTURE:
 * 1. Virtual Printing Mechanism:
 *    Windows 10 and 11 provide the built-in "Microsoft Print to PDF" driver (ntprint.inf).
 *    FolioNote registers a specialized virtual printer named "Print to FolioNote"
 *    using this underlying PDF engine driver. Any Windows application (browsers,
 *    office suites, CAD viewers) can print directly to FolioNote.
 *
 * 2. Administrator Privilege Escalation (UAC):
 *    Modifying the Windows print spooler subsystem (adding or removing printer objects,
 *    drivers, or ports) requires local Administrator privileges. Standard desktop
 *    applications execute under restricted user tokens (Medium Integrity Level).
 *    To install or uninstall the printer without requiring FolioNote itself to always
 *    run as Administrator, we utilize the Windows ShellExecuteEx API with the verb "runas".
 *    This requests the Windows User Account Control (UAC) consent elevation prompt
 *    (High Integrity Level) for a focused PowerShell configuration task.
 */

#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <winspool.h>
#endif

namespace Folio {

class PrinterInstaller {
public:
    /**
     * @brief Checks whether the "Print to FolioNote" virtual printer is registered.
     * 
     * Working Mechanism:
     * - Uses the Win32 Spooler API OpenPrinterW to query the local print spooler subsystem.
     * - If the printer exists, OpenPrinterW succeeds and returns TRUE.
     * 
     * @param printerName The friendly name of the printer to verify (default: "Print to FolioNote").
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
     * @brief Elevates privileges via UAC and installs the "Print to FolioNote" virtual printer.
     *
     * Working Process:
     * 1. Constructs a PowerShell command invocation that checks for existing printer registration.
     * 2. Executes `Add-Printer` targeting the built-in "Microsoft Print to PDF" driver and `PORTPROMPT:` port.
     * 3. Invokes `ShellExecuteExW` specifying `lpVerb = L"runas"` to prompt the user with the Windows UAC dialog.
     * 4. Waits synchronously for the elevated process to exit and returns the execution status.
     *
     * @param printerName Name of the printer to create (default: "Print to FolioNote").
     * @return true if installation process succeeded (exit code 0); false if cancelled or errored.
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

        // Escape backslashes for PowerShell embedding
        std::wstring escapedExe = L"";
        for (wchar_t c : exePathW) {
            if (c == L'\'') escapedExe += L"''";
            else escapedExe += c;
        }

        // PowerShell script to:
        // a) Enable Microsoft-Windows-PrintService/Operational log (captures Event 307 on print completion)
        // b) Add printer with 'Microsoft Print To PDF' driver
        // c) Create scheduled task 'FolioNotePrintDispatch' triggered by Event 307 to invoke FolioNote --import
        std::wstring psCommand = 
            L"-NoProfile -ExecutionPolicy Bypass -Command \""
            L"$pName = '" + printerName + L"'; "
            L"wevtutil sl 'Microsoft-Windows-PrintService/Operational' /e:true; "
            L"if (-not (Get-Printer -Name $pName -ErrorAction SilentlyContinue)) { "
            L"  Add-Printer -Name $pName -DriverName 'Microsoft Print To PDF' -PortName 'FILE:'; "
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
            L"      <Subscription>&lt;QueryList&gt;&lt;Query Id=`\"0`\" Path=`\"Microsoft-Windows-PrintService/Operational`\"&gt;&lt;Select Path=`\"Microsoft-Windows-PrintService/Operational`\"&gt;*[System[EventID=307]] and *[UserData[DocumentPrinted[Param5='Print to FolioNote']]]&lt;/Select&gt;&lt;/Query&gt;&lt;/QueryList&gt;</Subscription>`r`\n"
            L"      <ValueQueries><Value name=`\"PrintedFile`\">Event/UserData/DocumentPrinted/Param6</Value></ValueQueries>`r`\n"
            L"    </EventTrigger>`r`\n"
            L"  </Triggers>`r`\n"
            L"  <Principals><Principal id=`\"Author`\"><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>`r`\n"
            L"  <Settings>`r`\n"
            L"    <MultipleInstancesPolicy>Parallel</MultipleInstancesPolicy>`r`\n"
            L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>`r`\n"
            L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>`r`\n"
            L"    <AllowHardTerminate>true</AllowHardTerminate>`r`\n"
            L"    <StartWhenAvailable>true</StartWhenAvailable>`r`\n"
            L"    <Enabled>true</Enabled>`r`\n"
            L"    <Priority>7</Priority>`r`\n"
            L"  </Settings>`r`\n"
            L"  <Actions Context=`\"Author`\">`r`\n"
            L"    <Exec>`r`\n"
            L"      <Command>$appExe</Command>`r`\n"
            L"      <Arguments>--import `\"`$(PrintedFile)`\"</Arguments>`r`\n"
            L"    </Exec>`r`\n"
            L"  </Actions>`r`\n"
            L"</Task>`r`\n"
            L"'@; "
            L"$xmlFile = [System.IO.Path]::GetTempFileName(); "
            L"[System.IO.File]::WriteAllText($xmlFile, $xml, [System.Text.Encoding]::Unicode); "
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
                // Wait for PowerShell configuration completion (up to 30 seconds)
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
     * @brief Elevates privileges via UAC and uninstalls the "Print to FolioNote" virtual printer.
     *
     * Working Process:
     * 1. Constructs a PowerShell command to locate and unregister the named printer and scheduled task.
     * 2. Invokes `ShellExecuteExW` with `lpVerb = L"runas"` for UAC elevation.
     * 3. Executes `Remove-Printer` and `schtasks /delete` cleanly from the subsystem.
     *
     * @param printerName Name of the printer to remove (default: "Print to FolioNote").
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
