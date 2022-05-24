TEMPLATE	= app
TARGET		= QtdSyncMonitor
QT		    -= gui core

CONFIG		+= warn_on debug_and_release embed_manifest_exe WIN32
CONFIG      -= qt
DEFINES     += QTDSYNCMONITOR
SOURCES		= src/mainMonitor.cpp

RC_FILE     =   QtdSync.rc

CONFIG(debug, debug|release) {
} else {
    QMAKE_POST_LINK  = copy "release\\QtdSyncMonitor.exe" "bin\\QtdSyncMonitor.exe"
}
