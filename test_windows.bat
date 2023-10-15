@ECHO OFF

REM ------------------------------------------------------------------------------

SETLOCAL

SET PLATFORM=windows
SET ONION="%~dp0\bin\onion.exe"

IF NOT EXIST %ONION% (
    	ECHO Failed to find onion.exe in synced repository
	EXIT /B 1
)

ECHO Using onion at '%ONION%', platform '%PLATFORM%'

REM ------------------------------------------------------------------------------

CALL :TEST_MODULE "tests\app_self_contained"
if ERRORLEVEL 1 ( EXIT /B 1 )

CALL :TEST_MODULE "tests\module_with_static_lib"
if ERRORLEVEL 1 ( EXIT /B 1 )

CALL :TEST_MODULE "tests\app_with_external_static_lib"
if ERRORLEVEL 1 ( EXIT /B 1 )

CALL :TEST_MODULE "tests\app_with_lib"
if ERRORLEVEL 1 ( EXIT /B 1 )

CALL :TEST_MODULE "tests\app_with_lib_and_tests"
if ERRORLEVEL 1 ( EXIT /B 1 )

CALL :TEST_MODULE "tests\app_with_local_static_lib"
if ERRORLEVEL 1 ( EXIT /B 1 )

CALL :TEST_MODULE "tests\app_with_thirdparty_dep"
if ERRORLEVEL 1 ( EXIT /B 1 )

CALL :TEST_MODULE "tests\bison_file"
if ERRORLEVEL 1 ( EXIT /B 1 )

CALL :TEST_MODULE "tests\common_static_lib"
if ERRORLEVEL 1 ( EXIT /B 1 )

CALL :TEST_MODULE "tests\detached_lib"
if ERRORLEVEL 1 ( EXIT /B 1 )

CALL :TEST_MODULE "tests\embed_file"
if ERRORLEVEL 1 ( EXIT /B 1 )

ECHO All tests finished!
EXIT /B 0

REM ------------------------------------------------------------------------------

:TEST_MODULE
PUSHD %~1

%ONION% configure
if ERRORLEVEL 1 (
	ECHO Unable to configure test '%~1'
	POPD
	EXIT /B 1
)

%ONION% make
if ERRORLEVEL 1 (
	ECHO Unable to generate build files for test '%~1'
	POPD
	EXIT /B 1
)

%ONION% build -config=Release
if ERRORLEVEL 1 (
	ECHO Unable to compile test '%~1'
	POPD
	EXIT /B 1
)

%ONION% test -config=Release
if ERRORLEVEL 1 (
	ECHO Failed to run tests for '%~1'
	POPD
	EXIT /B 1
)

%ONION% build -config=Debug
if ERRORLEVEL 1 (
	ECHO Unable to compile test '%~1'
	POPD
	EXIT /B 1
)

%ONION% test -config=Debug
if ERRORLEVEL 1 (
	ECHO Failed to run tests for '%~1'
	POPD
	EXIT /B 1
)

POPD

ECHO Testing for '%~1' finished
EXIT /B 0



