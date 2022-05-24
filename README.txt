Build instructions for 
QtdSync v0.6.20 beta
================================

0. Note on my behalf
--------------------------------
The source code is a mess and will be improved with v0.7 ;-)

1. Compile instructions
--------------------------------
    To compile QtdSync, do the following steps:
    
    1.1. Windows
    --------------------------------
        1. Make sure Visual Studio (tested with 2005/2010) is installed
        2. Install Qt4.8.x (http://qt.nokia.com)
        3. Adjust QTD_PASS_HASH defined in "Shared/QtdClasses/QtdPassHash.h" to your private md5 hash.
           This hash is used to encrypt sensitive information as passwords a.s.o.
           NOTE: The predefined hash is NOT the one used in the official release. Therefor qtd-files created with ANY
                 custom build will NOT be compatible with the offical release ones.
        4. run QtdSync/pro2vc.bat
        5. use the QtdSync/*.vcproj file to build the project.

    1.2. Linux
    --------------------------------
        1. Make sure you have all necessary compile and linker tools installed (build-essentials)
        2. Install Qt4.8.x (http://qt.nokia.com)
        3. Adjust QTD_PASS_HASH defined in "Shared/QtdClasses/QtdPassHash.h" to your private md5 hash.
           This hash is used to encrypt sensitive information as passwords a.s.o.
           NOTE: The predefined hash is NOT the one used in the official release. Therefor qtd-files created with ANY
                 custom build will NOT be compatible with the offical release ones.
        4. run qmake -makefile QtdSync.pro
        5. use the generated makefile to compile the project


2. Additional notes
--------------------------------

    2.1. Special linkage (Windows only)
    --------------------------------
        To reduce space (and update load) the official QtdSync release is linked to a single Qt-DLL (QtdBase.dll).
        This DLL is a special linkage of the main Qt libraries QtCore, QtGui, QtNetwork and QtXml.
        
        If you also want to do that, relink your Qt into one DLL (QtdBase.dll) exporting all the necessary symbols
        and add the compiled lib to the LIBS statement, e.g.
         
        LIBS      += <your-qtdbase-path>/QtdBase.lib
        
        in the QtdSync/QtdSync.pro file. After you have done that rerun pro2vc.bat
        
    2.2. Binary size
    --------------------------------
        Also to reduce space (and update load) the official QtdSync release (both QtdSync and QtdBase.dll) are
        packed using upx (http://upx.sourceforge.net).
       
3. Contact info
--------------------------------
If you have any further question, please don't hesitate to contact me: mail (at) tdoering.de
NOTE: Since I am a husband, father of four and an employee with a full time job, responses might take a while ;-)