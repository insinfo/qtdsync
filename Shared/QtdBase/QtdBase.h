#ifndef QT_BUILD_CORE_LIB
# include <QtGui/QtGui>
# include <QtXml/QtXml>
# include <QtNetwork/QtNetwork>
#else
# include "qglobal.h"
#endif

#ifdef QTD_STATIC
Q_CORE_EXPORT void* qtdbase_new(size_t size);
Q_CORE_EXPORT void qtdbase_delete(void* p);
#endif


