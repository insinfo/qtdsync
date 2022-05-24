TEMPLATE     = app
TARGET       = QtdSync
LANGUAGE     = C++
INCLUDEPATH += include ./../Shared/QtdBase
CONFIG      += qt warn_on debug_and_release
CONFIG      -= console
QT 			    += xml network

win32 {
LIBS        += version.lib
CONFIG      += WIN32
}

DEFINES     += HAVE_SVN_REVISION

CONFIG(debug, debug|release) {
    DESTDIR = debug
    win32-msvc2005 {
        QMAKE_CXXFLAGS += /Fddebug\\QtdSync.pdb
    }
    INCLUDEPATH += debug
} else {
    DESTDIR = release
    win32 {
        win32-msvc2005 {
            QMAKE_CXXFLAGS  += /Fdrelease\\QtdSync.pdb
        }
        QMAKE_POST_LINK  = copy "release\\QtdSync.exe" "bin\\QtdSync.exe"
    }
    INCLUDEPATH     += release
}

SOURCES     =   src/main.cpp \
                src/QtdSync.cpp

HEADERS     =   include/QtdSync.h \
                version.h \
                include/svnrevision.h

RESOURCES   =   QtdSync.qrc
win32 {
    RC_FILE   =  QtdSync.rc
    RESOURCES += QtdSync_Windows.qrc
}

FORMS       =   forms/QtdSyncDlg.ui \
                forms/QtdSyncStartupDlg.ui \
                forms/QtdSyncConfigDlg.ui \
                forms/QtdSyncAboutDlg.ui \
                forms/QtdSyncAuthDlg.ui \
                forms/QtdSyncRemoteUserDlg.ui \
                forms/QtdSyncUpdateDlg.ui \
                forms/QtdSyncScheduledBackupDlg.ui \
                forms/QtdSyncScheduleDlg.ui \
                forms/QtdSyncSettingsDlg.ui \
                forms/QtdSyncServerDlg.ui \
                forms/QtdSyncVDirConfigDlg.ui \
                forms/QtdSyncMailDlg.ui \
                forms/QtdSyncSSHFolderDlg.ui \
                forms/QtdSyncFolderBindingDlg.ui

TRANSLATIONS +=  translations/qtdsync_de_DE.ts \
                 translations/qtdsync_en_US.ts
                 
include(./../Shared/QtdClasses/QtdClasses.pri)
