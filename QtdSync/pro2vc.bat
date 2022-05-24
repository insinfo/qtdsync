@echo off
%~d0
cd %~dp0
if EXIST ".svn\entries" (
    call ..\Shared\getSvnVersion\getSvnVersion.bat . include\svnrevision.h
)
if EXIST "..\Shared\.svn\entries" (
    call ..\Shared\getSvnVersion\getSvnVersion.bat ..\Shared include\svnrevision_shared.h "QTDSHARED_"
)

call lrelease_qtdsync.bat
call uic_forms.bat

qmake -tp vc QtdSync.pro
qmake -tp vc QtdSyncServer.pro
qmake -tp vc QtdSyncMonitor.pro
