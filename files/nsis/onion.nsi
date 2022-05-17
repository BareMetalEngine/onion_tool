!include LogicLib.nsh
!include WinCore.nsh
!ifndef NSIS_CHAR_SIZE
!define NSIS_CHAR_SIZE 1
!endif

; onion.nsi
;--------------------------------

Name "Onion Build Tool"
OutFile "onion-setup.exe"
RequestExecutionLevel Admin
Unicode True
InstallDir "$ProgramFiles\Onion"

;--------------------------------

Page directory
Page instfiles

;--------------------------------

Function RegAppendString
System::Store S
Pop $R0 ; append
Pop $R1 ; separator
Pop $R2 ; reg value
Pop $R3 ; reg path
Pop $R4 ; reg hkey
System::Call 'ADVAPI32::RegCreateKey(i$R4,tR3,*i.r1)i.r0'
${If} $0 = 0
    System::Call 'ADVAPI32::RegQueryValueEx(ir1,tR2,i0,*i.r2,i0,*i0r3)i.r0'
    ${If} $0 <> 0
        StrCpy $2 ${REG_SZ}
        StrCpy $3 0
    ${EndIf}
    StrLen $4 $R0
    StrLen $5 $R1
    IntOp $4 $4 + $5
    IntOp $4 $4 + 1 ; For \0
    !if ${NSIS_CHAR_SIZE} > 1
        IntOp $4 $4 * ${NSIS_CHAR_SIZE}
    !endif
    IntOp $4 $4 + $3
    System::Alloc $4
    System::Call 'ADVAPI32::RegQueryValueEx(ir1,tR2,i0,i0,isr9,*ir4r4)i.r0'
    ${If} $0 = 0
    ${OrIf} $0 = ${ERROR_FILE_NOT_FOUND}
        System::Call 'KERNEL32::lstrlen(t)(ir9)i.r0'
        ${If} $0 <> 0
            System::Call 'KERNEL32::lstrcat(t)(ir9,tR1)'
        ${EndIf}
        System::Call 'KERNEL32::lstrcat(t)(ir9,tR0)'
        System::Call 'KERNEL32::lstrlen(t)(ir9)i.r0'
        IntOp $0 $0 + 1
        !if ${NSIS_CHAR_SIZE} > 1
            IntOp $0 $0 * ${NSIS_CHAR_SIZE}
        !endif
        System::Call 'ADVAPI32::RegSetValueEx(ir1,tR2,i0,ir2,ir9,ir0)i.r0'
    ${EndIf}
    System::Free $9
    System::Call 'ADVAPI32::RegCloseKey(ir1)'
${EndIf}
Push $0
System::Store L
FunctionEnd

Function AppendInstallDirToPath
Push ${HKEY_LOCAL_MACHINE}
Push "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
Push "Path"
Push ";"
Push "$INSTDIR"
Call RegAppendString
Pop $0
FunctionEnd

;--------------------------------

Section ""
  SetOutPath $INSTDIR
  File onion.exe
  Call AppendInstallDirToPath
SectionEnd
