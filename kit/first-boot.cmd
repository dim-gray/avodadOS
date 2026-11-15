@echo off
rem avocadOS first-boot setup. Run from the install disc after ReactOS is installed:
rem     D:\avocados\first-boot.cmd        (use your CD drive letter)
rem Copies the avocadOS shell to C:\avocadOS, installs the bundled apps silently,
rem and makes the avocadOS shell the default desktop.
setlocal
set SRC=%~dp0
set DST=%SystemDrive%\avocadOS
if exist "%DST%\firstboot.lock" goto :eof
mkdir "%DST%" 2>nul
echo started> "%DST%\firstboot.lock"

echo [avocadOS] copying shell and wallpapers...
xcopy /e /i /y "%SRC%avocadOS" "%DST%" >nul

echo [avocadOS] installing Firefox...
if exist "%SRC%apps\firefox.exe" start /wait "" "%SRC%apps\firefox.exe" -ms
echo [avocadOS] installing VLC...
if exist "%SRC%apps\vlc.exe" start /wait "" "%SRC%apps\vlc.exe" /S
echo [avocadOS] installing LibreOffice...
if exist "%SRC%apps\libreoffice.msi" start /wait msiexec /i "%SRC%apps\libreoffice.msi" /qn /norestart
echo [avocadOS] installing Python...
if exist "%SRC%apps\python.msi" start /wait msiexec /i "%SRC%apps\python.msi" /qn /norestart

echo [avocadOS] setting the avocadOS shell as default...
reg add "HKLM\Software\Microsoft\Windows NT\CurrentVersion\Winlogon" /v Shell /t REG_SZ /d "%DST%\avocado-shell.exe" /f

echo done> "%DST%\firstboot.done"
echo [avocadOS] finished. Log off or restart to see the new desktop.
endlocal
