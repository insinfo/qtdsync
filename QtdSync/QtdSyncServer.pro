TEMPLATE	= app
TARGET		= QtdSyncServer
QT		    -= gui core

CONFIG		+= warn_on debug_and_release embed_manifest_exe WIN32
CONFIG      -= qt
DEFINES     += QTDSYNCSERVER
SOURCES		= src/mainServer.cpp

RC_FILE     =   QtdSync.rc

CONFIG(debug, debug|release) {
} else {
    QMAKE_POST_LINK  = copy "release\\QtdSyncServer.exe" "bin\\QtdSyncServer.exe"
}
