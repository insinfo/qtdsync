@echo off
setLocal EnableDelayedExpansion
rem First get current version in file
set REVISION=""
set PREFIX=%~3
if EXIST %2 (
    set /a n=0
    for /f "tokens=*" %%a in ('type %2') do (
        set /a n+=1
        if !n! == 1 (
            set REV=%%~a
            set REVISION=!REV:~20!
            goto forbreak
        )
    )
)
:forbreak

set /a n=0
for /f "tokens=*" %%a in ('type %1\.svn\entries') do (
    set /a n+=1
    if !n! == 3 (
        set REV=%%a
        if "!REV!" == "!REVISION!" (
            echo Revision unchanged ^(%%~a^).
            goto ende
        )
        echo Current SVN Revision %%a
        echo #define %PREFIX%SVNREVISION %%a> %2
    )
    if !n! == 4 (
        set REPO=%%a
        echo #define %PREFIX%SVNREPOSITORY "!REPO:~22!">> %2
    )
)
:ende