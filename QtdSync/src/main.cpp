#include "QtdSync.h"
#include "QtdBase.h"

//----------------------------------------------------------------------------
void help(int argc, char* argv[])
{
    QtdApplication a(argc,argv);
    QtdTools::ProductInfo info;
    QtdTools::productInfo(a.applicationFilePath(), info);

    QString helpStr;
    helpStr += "<center>";
    helpStr += "<font style=\"font-weight: bold; font-family: Tahoma; font-size: 10pt;\">" + info.name + " v" + info.versionName + "</font><br>";
    helpStr += "Copyright (c) " + QDateTime::currentDateTime().toString("yyyy") + " by Thomas Doering";
    helpStr += "</center><hr size=\"1\" color=\"#cccccc\">";
    helpStr += "This is a front end for rsync.<br>It explicitly provides support for local backup only (for now).<br>";
    helpStr += "<br>";
    helpStr += "Usage: qtdsync [options] [backset set files]<br>";
    helpStr += "<br>";
    helpStr += "Options:<br>";
    helpStr += "<table>";
    helpStr += "<tr><td>-h,</td><td>--help,</td><td>/?</td><td>show this information (no other options supported)</td></tr>";
    //helpStr += "<tr><td>-m,</td><td>--monitor</td><td></td><td>run QtdSyncMonitor (no other options supported)</td></tr>";
    helpStr += "<tr><td>-b,</td><td>--backup</td><td></td><td>do backup for the provided backup set files</td></tr>";
    helpStr += "<tr><td>-r,</td><td>--restore</td><td></td><td>restore backup for the provided backup set files</td></tr>";
    helpStr += "<tr><td>-l,</td><td>--location</td><td></td><td>set the destination path for backup sets without it set</td></tr>";
    helpStr += "<tr><td>-o,</td><td>--override</td><td></td><td>override every backup destination given in the backup sets</td></tr>";
    helpStr += "<tr><td>-q,</td><td>--quit</td><td></td><td>auto-quits after backup</td></tr>";
    helpStr += "<tr><td>-s,</td><td>--silent</td><td></td><td>silent mode</td></tr>";
    helpStr += "<tr><td>-f,</td><td>--force</td><td></td><td>force backup,<br>even if its currently locked by another backup process</td></tr>";
    helpStr += "<tr><td>-v,</td><td>--valid</td><td></td><td>always mark the backup as valid</td></tr>";
    helpStr += "<tr></tr>";
    helpStr += "<tr><td colspan=\"3\">[backup set files]</td><td>a whitespace separated list of ";
    helpStr += "backup set files (*.qtd)<br>";
    helpStr += "if the backup option (-b) is not set ";
    helpStr += "qtdsync provides further options</td></tr>";
    helpStr += "</table>";
    QMessageBox::information(0L, info.name + " v" + info.versionName, helpStr);
}
//----------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    int ret;
    QStringList fileList;
    QtdApplication::QtdConfig config;

    if (argc > 1) {
        int i = 1;

        while (i < argc) {
            QString cArgv = QString(argv[i]);

            if (cArgv == "--help" || cArgv == "-h" || cArgv == "/?") {
                help(argc,argv);
                exit(0);
            } else if (cArgv == "--monitor" || cArgv == "-m") {
                QtdSync rsync(argc, argv, QtdSync::eTray);
                rsync.setQuitOnLastWindowClosed(false);
                int ret = rsync.exec();
                return ret;
#ifdef WIN32
            } else if (cArgv == "--server-config") {
                QtdSync rsync(argc, argv, QtdSync::eServer);
                int ret = rsync.exec();
                return ret;
            } else if (cArgv == "--server-start") {
                QtdSync rsync(argc, argv, QtdSync::eServerStart);
                return 0;
            } else if (cArgv == "--server-stop") {
                QtdSync rsync(argc, argv, QtdSync::eServerStop);
                return 0;
#else
            } else if (cArgv == "--enableStartupMonitor") {
                QFile rc("/etc/init.d/qtdsyncmonitor");
                if (rc.open(QIODevice::WriteOnly)) {
                    QTextStream ts(&rc);
                    ts << "#!/bin/bash\n\n";
                    ts << QString("%1 -m\n").arg(qApp->applicationFilePath());
                    rc.close();

                    system("ln -s /etc/init.d/qtdsyncmonitor /etc/rc5.d/S99qtdsyncmonitor");
                    system("chmod a+x /etc/rc5.d/S99qtdsyncmonitor");
                    system("chmod a+x /etc/init.d/qtdsyncmonitor");
                }
                return 0;
            } else if (cArgv == "--disableStartupMonitor") {
                QFile::remove("/etc/init.d/qtdsyncmonitor");
                QFile::remove("/etc/rc5.d/S99qtdsyncmonitor");
                return 0;
#endif
            } else if (cArgv == "--backup" || cArgv == "-b") {
                config["backup"] = "true";
            } else if (cArgv == "--restore" || cArgv == "-r") {
                config["restore"] = "true";
            } else if (cArgv.startsWith("--location=") || cArgv.startsWith("-l=")) {
                int midPos = cArgv.startsWith("--") ? strlen("--location=") : strlen("-l=");
                config["backuplocation"] = cArgv.mid(midPos);
            } else if (cArgv == "--override" || cArgv == "-o") {
                config["override"] = "true";
            } else if (cArgv == "--silent" || cArgv == "-s") {
                config["silent"] = "true";
            } else if (cArgv == "--quit" || cArgv == "-q") {
                config["quit"] = "true";
            } else if (cArgv == "--force" || cArgv == "-f") {
                config["force"] = "true";
            } else if (cArgv == "--valid" || cArgv == "-v") {
                config["markFailedAsValid"] = "true";
            } else if (cArgv == "--noupdate" || cArgv == "-n") {
                config["noupdate"] = "true";
            } else {
                QStringList cArgL = cArgv.split("=", QString::SkipEmptyParts);
                QString newArg = cArgL.takeFirst();
                QString newArgValue = cArgL.count() > 0 ? cArgL.join("=") : "true";
                config[newArg] = newArgValue;
                if (QFileInfo(cArgv).exists()) {
                    fileList << cArgv;
                }
            }
            i++;
        }

        if ((config["backup"] == "true" || config["restore"] == "true") && fileList.count() > 0) {
            QtdSync rsync(argc, argv, QtdSync::eNone);
            QObject::connect( &rsync, SIGNAL( lastWindowClosed() ), &rsync, SLOT( quit() ) );
            int ret = rsync.doBackup(fileList, config);
            return ret;
        }
    }

    QtdSync rsync(argc, argv, QtdSync::eMain, fileList, config);
    QObject::connect( &rsync, SIGNAL( lastWindowClosed() ), &rsync, SLOT( quit() ) );
    ret = rsync.exec();

    return ret;
}