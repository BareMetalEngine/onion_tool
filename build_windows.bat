@ECHO OFF

IF "%ONION_GIT_PUBLIC_TOKEN%" == "" (
	ECHO "No ONION_GIT_PUBLIC_TOKEN set"
	EXIT /B 1
) else (
	ECHO Using git token: "%ONION_GIT_PUBLIC_TOKEN%"	
)

set MAIN_DIR=%cd%
set OUTPUT_BINARY=%MAIN_DIR%\.build\bin\Release\onion.exe
set RELEASE_DIR=%MAIN_DIR%\.release
set RELEASE_BINARY=%MAIN_DIR%\.release\onion.exe
set FILES_DIR=%MAIN_DIR%\files
set BUILD_DIR=%MAIN_DIR%\.build

REM -------------------------------------------

if EXIST %RELEASE_DIR% (
	ECHO Cleanup release dir...

	rmdir /s/q %RELEASE_DIR%
	if ERRORLEVEL 1 goto Error
)

if EXIST %BUILD_DIR% (
	ECHO Cleanup build dir...

	rmdir /s/q %BUILD_DIR%
	if ERRORLEVEL 1 goto Error
)

REM -------------------------------------------

ECHO Build...

if NOT EXIST %BUILD_DIR% (
	mkdir %BUILD_DIR%
	if ERRORLEVEL 1 goto Error
)

IF NOT EXIST "%BUILD_DIR%\windows" (
	mkdir %BUILD_DIR%\windows
	if ERRORLEVEL 1 goto Error
)

pushd %BUILD_DIR%\windows
if ERRORLEVEL 1 goto Error

cmake ..\..
if ERRORLEVEL 1 goto Error

cmake --build . --config Release --parallel
if ERRORLEVEL 1 goto Error

popd
if ERRORLEVEL 1 goto Error

REM -------------------------------------------

ECHO Package...

if NOT EXIST %RELEASE_DIR% (
	mkdir %RELEASE_DIR%
	if ERRORLEVEL 1 goto Error
)

if NOT EXIST %RELEASE_BINARY% (
	copy %OUTPUT_BINARY% %RELEASE_BINARY%
	if ERRORLEVEL 1 goto Error
	if NOT EXIST %RELEASE_BINARY% goto Error
)

%OUTPUT_BINARY% glue -action=pack -file="%RELEASE_BINARY%" -source="%FILES_DIR%"
if ERRORLEVEL 1 goto Error

REM -------------------------------------------

ECHO Publish...
%OUTPUT_BINARY% release -action=create
if ERRORLEVEL 1 goto Error

%OUTPUT_BINARY% release -action=add -file="%RELEASE_BINARY%"
if ERRORLEVEL 1 goto CancelRelease

%OUTPUT_BINARY% release -action=publish
if ERRORLEVEL 1 goto CancelRelease

REM -------------------------------------------

EXIT /B 0

:CancelRelease
ECHO ----------------------------------------
ECHO Release failed
ECHO ----------------------------------------
%OUTPUT_BINARY% release -action=discard

:Error
ECHO ----------------------------------------
ECHO There were fatal errors, building failed
ECHO ----------------------------------------

CD %MAIN_DIR%

IF EXIST %RELEASE_DIR% (
rmdir /s/q %RELEASE_DIR%
)

IF EXIST %BUILD_DIR% (
	rmdir /s/q %BUILD_DIR%
)

EXIT /B 1