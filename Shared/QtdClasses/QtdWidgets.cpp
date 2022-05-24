#include "QtdWidgets.h"
#include "QtdBase.h"

//============================================================================
#ifdef Q_WS_WIN

#include <QtCore/qt_windows.h>

// Blur behind data structures
#define DWM_BB_ENABLE                 0x00000001  // fEnable has been specified
#define DWM_BB_BLURREGION             0x00000002  // hRgnBlur has been specified
#define DWM_BB_TRANSITIONONMAXIMIZED  0x00000004  // fTransitionOnMaximized has been specified
#define WM_DWMCOMPOSITIONCHANGED        0x031E    // Composition changed window message

//----------------------------------------------------------------------------
typedef struct _DWM_BLURBEHIND
{
    DWORD dwFlags;
    BOOL fEnable;
    HRGN hRgnBlur;
    BOOL fTransitionOnMaximized;
} DWM_BLURBEHIND, *PDWM_BLURBEHIND;

typedef struct _MARGINS
{
    int cxLeftWidth;
    int cxRightWidth;
    int cyTopHeight;
    int cyBottomHeight;
} MARGINS, *PMARGINS;

//----------------------------------------------------------------------------
typedef HRESULT (WINAPI *PtrDwmIsCompositionEnabled)(BOOL* pfEnabled);
typedef HRESULT (WINAPI *PtrDwmExtendFrameIntoClientArea)(HWND hWnd, const MARGINS* pMarInset);
typedef HRESULT (WINAPI *PtrDwmEnableBlurBehindWindow)(HWND hWnd, const DWM_BLURBEHIND* pBlurBehind);
typedef HRESULT (WINAPI *PtrDwmGetColorizationColor)(DWORD *pcrColorization, BOOL *pfOpaqueBlend);

static PtrDwmIsCompositionEnabled pDwmIsCompositionEnabled= 0;
static PtrDwmEnableBlurBehindWindow pDwmEnableBlurBehindWindow = 0;
static PtrDwmExtendFrameIntoClientArea pDwmExtendFrameIntoClientArea = 0;
static PtrDwmGetColorizationColor pDwmGetColorizationColor = 0;
//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
static bool resolveLibs()
{
    if (!pDwmIsCompositionEnabled) {
        QLibrary dwmLib(QString::fromAscii("dwmapi"));
        pDwmIsCompositionEnabled =(PtrDwmIsCompositionEnabled)dwmLib.resolve("DwmIsCompositionEnabled");
        pDwmExtendFrameIntoClientArea = (PtrDwmExtendFrameIntoClientArea)dwmLib.resolve("DwmExtendFrameIntoClientArea");
        pDwmEnableBlurBehindWindow = (PtrDwmEnableBlurBehindWindow)dwmLib.resolve("DwmEnableBlurBehindWindow");
        pDwmGetColorizationColor = (PtrDwmGetColorizationColor)dwmLib.resolve("DwmGetColorizationColor");
    }
    return pDwmIsCompositionEnabled != 0;
}

#endif

//----------------------------------------------------------------------------
/*!
  * Chekcs and returns true if Windows DWM composition
  * is currently enabled on the system.
  *
  * To get live notification on the availability of
  * this feature, you will currently have to
  * reimplement winEvent() on your widget and listen
  * for the WM_DWMCOMPOSITIONCHANGED event to occur.
  *
  */
//----------------------------------------------------------------------------
bool QtWin::isCompositionEnabled()
{
#ifdef Q_WS_WIN
    if (resolveLibs()) {
        HRESULT hr = S_OK;
        BOOL isEnabled = false;
        hr = pDwmIsCompositionEnabled(&isEnabled);
        if (SUCCEEDED(hr))
            return isEnabled;
    }
#endif
    return false;
}

//----------------------------------------------------------------------------
/*!
  * Enables Blur behind on a Widget.
  *
  * \a enable tells if the blur should be enabled or not
  */
//----------------------------------------------------------------------------
bool QtWin::enableBlurBehindWindow(QWidget *widget, bool enable)
{
    Q_ASSERT(widget);
    bool result = false;
#ifdef Q_WS_WIN
# if QT_VERSION >= 0x040500
    if (resolveLibs()) {
        DWM_BLURBEHIND bb = {0};
        HRESULT hr = S_OK;
        bb.fEnable = enable;
        bb.dwFlags = DWM_BB_ENABLE;
        bb.hRgnBlur = NULL;
        widget->setAttribute(Qt::WA_TranslucentBackground, enable);
        widget->setAttribute(Qt::WA_NoSystemBackground, enable);
        hr = pDwmEnableBlurBehindWindow(widget->winId(), &bb);
        if (SUCCEEDED(hr)) {
            result = true;
            windowNotifier()->addWidget(widget);
        }
    }
# endif
#endif
    return result;
}

//----------------------------------------------------------------------------
/*!
  * ExtendFrameIntoClientArea.
  *
  * This controls the rendering of the frame inside the window.
  * Note that passing margins of -1 (the default value) will completely
  * remove the frame from the window.
  *
  * \note you should not call enableBlurBehindWindow before calling
  *       this functions
  *
  * \a enable tells if the blur should be enabled or not
  */
//----------------------------------------------------------------------------
bool QtWin::extendFrameIntoClientArea(QWidget *widget, int left, int top, int right, int bottom)
{

    Q_ASSERT(widget);
    Q_UNUSED(left);
    Q_UNUSED(top);
    Q_UNUSED(right);
    Q_UNUSED(bottom);

    bool result = false;
#ifdef Q_WS_WIN
# if QT_VERSION >= 0x040500
    if (resolveLibs()) {
        QLibrary dwmLib(QString::fromAscii("dwmapi"));
        HRESULT hr = S_OK;
        MARGINS m = {left, top, right, bottom};
        hr = pDwmExtendFrameIntoClientArea(widget->winId(), &m);
        if (SUCCEEDED(hr)) {
            result = true;
            windowNotifier()->addWidget(widget);
        }
        widget->setAttribute(Qt::WA_TranslucentBackground, result);
    }
# endif
#endif
    return result;
}

//----------------------------------------------------------------------------
/*!
  * Returns the current colorizationColor for the window.
  *
  * \a enable tells if the blur should be enabled or not
  */
//----------------------------------------------------------------------------
QColor QtWin::colorizatinColor()
{
    QColor resultColor = QApplication::palette().window().color();

#ifdef Q_WS_WIN
    if (resolveLibs()) {
        DWORD color = 0;
        BOOL opaque = FALSE;
        QLibrary dwmLib(QString::fromAscii("dwmapi"));
        HRESULT hr = S_OK;
        hr = pDwmGetColorizationColor(&color, &opaque);
        if (SUCCEEDED(hr))
            resultColor = QColor(color);
    }
#endif
    return resultColor;
}

#ifdef Q_WS_WIN
//----------------------------------------------------------------------------
WindowNotifier *QtWin::windowNotifier()
{
    static WindowNotifier *windowNotifierInstance = 0;
    if (!windowNotifierInstance)
        windowNotifierInstance = new WindowNotifier;
    return windowNotifierInstance;
}

//----------------------------------------------------------------------------
void WindowNotifier::addWidget(QWidget *widget)
{
    if (!widgets.contains(widget)) {
        connect(widget, SIGNAL(destroyed(QObject*)), this, SLOT(widgetDestroyed(QObject*)));
        widgets.append(widget); 
    }
}

/* Notify all enabled windows that the DWM state changed */
//----------------------------------------------------------------------------
bool WindowNotifier::winEvent(MSG *message, long *result)
{
    if (message && message->message == WM_DWMCOMPOSITIONCHANGED) {
        bool compositionEnabled = QtWin::isCompositionEnabled();
        foreach(QWidget * widget, widgets) {
            if (widget) {
                widget->setAttribute(Qt::WA_NoSystemBackground, compositionEnabled);
            }
            widget->update();
        }
    }
    return QWidget::winEvent(message, result);
}

//----------------------------------------------------------------------------
void WindowNotifier::widgetDestroyed(QObject* obj)
{
    removeWidget(qobject_cast<QWidget*>(obj));
}
#endif

//============================================================================
QtdTreeWidget::QtdTreeWidget(QWidget* parent)
: QTreeWidget(parent)
{
    setAcceptDrops(true);
}

//----------------------------------------------------------------------------
void QtdTreeWidget::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

//----------------------------------------------------------------------------
void QtdTreeWidget::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

//----------------------------------------------------------------------------
void QtdTreeWidget::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();

    if (mimeData->hasImage()) {
        emit dataImageDataDropped(qvariant_cast<QPixmap>(mimeData->imageData()));
    } else if (mimeData->hasHtml()) {
        emit dataHtmlDropped(mimeData->html());
    } else if (mimeData->hasText()) {
        emit dataTextDropped(mimeData->text());
    } else if (mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        QStringList urls;

        for (int i = 0; i < urlList.size() && i < 32; ++i) {
            QString url = urlList.at(i).path();
            urls << url;
        }
        emit dataUrlsDropped(urls);
    }

    event->acceptProposedAction();
}

//----------------------------------------------------------------------------
void QtdTreeWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    event->accept();
}

//============================================================================
QtdLineEdit::QtdLineEdit(QWidget* parent)
: QLineEdit(parent)
 ,c(0L)
 ,wordBoundary(" ")
{
    setAcceptDrops(true);
}

//----------------------------------------------------------------------------
void QtdLineEdit::setCompleterWordBoundary(QString wbd)
{
    wordBoundary = wbd;
}

//----------------------------------------------------------------------------
void QtdLineEdit::setCompleter(QCompleter *completer)
{
    if (c) {
        QObject::disconnect(c, 0, this, 0);
    }

    c = completer;
    if (!c) {
        return;
    }

    c->setWidget(this);
    c->setCompletionMode(QCompleter::PopupCompletion);
    //c->setCaseSensitivity(Qt::CaseInsensitive);
    QObject::connect(c, SIGNAL(activated(QString)),
                     this, SLOT(insertCompletion(QString)));
    //QObject::connect(c, SIGNAL(highlighted(QString)),
    //                 this, SLOT(insertCompletion(QString)));
}

//----------------------------------------------------------------------------
void QtdLineEdit::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

//----------------------------------------------------------------------------
void QtdLineEdit::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

//----------------------------------------------------------------------------
void QtdLineEdit::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();

    if (mimeData->hasImage()) {
        emit dataImageDataDropped(qvariant_cast<QPixmap>(mimeData->imageData()));
    } else if (mimeData->hasHtml()) {
        emit dataHtmlDropped(mimeData->html());
    } else if (mimeData->hasText()) {
        emit dataTextDropped(mimeData->text());
    } else if (mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        QStringList urls;

        for (int i = 0; i < urlList.size() && i < 32; ++i) {
            QString url = urlList.at(i).path();
            urls << url;
        }
        emit dataUrlsDropped(urls);
    }

    event->acceptProposedAction();
}

//----------------------------------------------------------------------------
void QtdLineEdit::dragLeaveEvent(QDragLeaveEvent *event)
{
    event->accept();
}

//----------------------------------------------------------------------------
void QtdLineEdit::insertCompletion(QString completion)
{
    if (!c || c->widget() != this) {
        return;
    }

    QStringList texts = text().split(" ");
    QString last = texts.takeLast();
    texts << completion;
    QString text = texts.join(" ");

    int nCompletionLength = completion.length() - last.length();

    setText(text);
}

//----------------------------------------------------------------------------
void QtdLineEdit::focusInEvent(QFocusEvent *e)
{
    if (c) {
        c->setWidget(this);
    }
    QLineEdit::focusInEvent(e);
}

//----------------------------------------------------------------------------
void QtdLineEdit::keyPressEvent(QKeyEvent *e)
{
    // the following is copied from the Qt-Examples
    if (c && c->popup() && c->popup()->isVisible()) {
        // The following keys are forwarded by the completer to the widget
       switch (e->key()) {
       case Qt::Key_Enter:
       case Qt::Key_Return:
       case Qt::Key_Escape:
       case Qt::Key_Tab:
       case Qt::Key_Backtab:
            e->ignore(); 
            return; // let the completer do default behavior
       default:
           break;
       }
    }

    bool isShortcut = ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_E); // CTRL+E
    if (!c || !isShortcut) // do not process the shortcut when we have a completer
        QLineEdit::keyPressEvent(e);
//! [7]

//! [8]
    const bool ctrlOrShift = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    if (!c || (ctrlOrShift && e->text().isEmpty()))
        return;

    QString eow = wordBoundary; // end of word
    bool hasModifier = (e->modifiers() != Qt::NoModifier) && !ctrlOrShift;
    QString completionPrefix = text().split(" ").last();

    if (!isShortcut && (hasModifier || e->text().isEmpty()|| completionPrefix.length() < 1 
                      || eow.contains(e->text().right(1)))) {
        if (c->popup()) c->popup()->hide();
        return;
    }

    if (completionPrefix != c->completionPrefix()) {
        c->setCompletionPrefix(completionPrefix);
        if (c->popup()) {
            c->popup()->setCurrentIndex(c->completionModel()->index(0, 0));
        }
    }
    QRect cr = cursorRect();
    if (c->popup()) {
        cr.setWidth(c->popup()->sizeHintForColumn(0)
                    + c->popup()->verticalScrollBar()->sizeHint().width());
    }
    c->complete(cr); // popup it up!
}

//============================================================================
class QtdDirTreeItem
{
public:
    //------------------------------------------------------------------------
    QtdDirTreeItem(QtdFileEntryInfo file, QtdDirTreeItem *parent = 0)
    {
        m_pParentItem = parent;

        if (m_pParentItem) {
            m_pParentItem->appendChild(this);
        }
        m_file = file;
    }

    //------------------------------------------------------------------------
    ~QtdDirTreeItem()
    {
        qDeleteAll(m_childItems);
    }

    //------------------------------------------------------------------------
    void appendChild(QtdDirTreeItem* pChild)
    {
        m_childItems.append(pChild);
    }

    //------------------------------------------------------------------------
    QtdDirTreeItem* child(int row)
    {
        return m_childItems.value(row);
    }

    //------------------------------------------------------------------------
    int childCount() const
    {
        return m_childItems.count();
    }
    //------------------------------------------------------------------------
    int columnCount() const
    {
        return 1;
    }

    //------------------------------------------------------------------------
    QVariant data(int column) const
    {
        if (column == 0) {
            return m_file.name;
        } else {
            return QVariant();
        }
    }

    //------------------------------------------------------------------------
    QString dirName() const
    {
        return m_file.path;
    }

    //------------------------------------------------------------------------
    int row() const
    {
        if (m_pParentItem) {
            return m_pParentItem->m_childItems.indexOf(const_cast<QtdDirTreeItem*>(this));
        }
        return 0;
    }

    //------------------------------------------------------------------------
    QtdDirTreeItem* parent()
    {
        return m_pParentItem;
    }

private:
    QList<QtdDirTreeItem*>      m_childItems;
    QtdFileEntryInfo            m_file;
    QtdDirTreeItem*             m_pParentItem;

    friend class QtdDirTreeModel;
};

//----------------------------------------------------------------------------
QtdDirTreeModel::QtdDirTreeModel(const QtdFileEntryInfo& rootEntry, QObject* pParent)
: QAbstractItemModel(pParent)
{
    m_pRootItem = new QtdDirTreeItem(rootEntry);
    setupModelData(rootEntry.files, m_pRootItem);
}

//------------------------------------------------------------------------
QtdDirTreeModel::~QtdDirTreeModel()
{
    delete m_pRootItem;
}

//------------------------------------------------------------------------
QtdDirTreeItem* QtdDirTreeModel::rootItem()
{
    return m_pRootItem;
}

//------------------------------------------------------------------------
QVariant QtdDirTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    QtdDirTreeItem* pItem = static_cast<QtdDirTreeItem*>(index.internalPointer());
    if (role == Qt::DecorationRole) {
        QFileIconProvider icons;
        QIcon icon = icons.icon(QFileInfo(pItem->m_file.path));
        return !icon.isNull() ? icon : icons.icon(pItem->m_file.bIsDir ? QFileIconProvider::Folder : QFileIconProvider::File);
    } else if (role != Qt::DisplayRole) {
        return QVariant();
    }


    return pItem->data(index.column());
}

//------------------------------------------------------------------------
Qt::ItemFlags QtdDirTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return 0;
    }

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

//------------------------------------------------------------------------
QVariant QtdDirTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        return  m_pRootItem->data(section);
    }

    return QVariant();
}

//------------------------------------------------------------------------
QModelIndex QtdDirTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }

    QtdDirTreeItem* pParentItem;

    if (!parent.isValid()) {
        pParentItem = m_pRootItem;
    } else {
        pParentItem = static_cast<QtdDirTreeItem*>(parent.internalPointer());
    }

    QtdDirTreeItem* pChildItem = pParentItem->child(row);
    if (pChildItem) {
        return createIndex(row, column, pChildItem);
    } else {
        return QModelIndex();
    }
}

//------------------------------------------------------------------------
QModelIndex QtdDirTreeModel::parent(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return QModelIndex();
    }

    QtdDirTreeItem* pChildItem = static_cast<QtdDirTreeItem*>(index.internalPointer());
    QtdDirTreeItem* pParentItem = pChildItem->parent();

    if (pParentItem == m_pRootItem) {
        return QModelIndex();
    }

    return createIndex(pParentItem->row(), 0, pParentItem);
}

//------------------------------------------------------------------------
int QtdDirTreeModel::rowCount(const QModelIndex &parent) const
{
    QtdDirTreeItem *pParentItem;
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid()) {
        pParentItem = m_pRootItem;
    } else {
        pParentItem = static_cast<QtdDirTreeItem*>(parent.internalPointer());
    }

    return pParentItem->childCount();
}

//------------------------------------------------------------------------
int QtdDirTreeModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return static_cast<QtdDirTreeItem*>(parent.internalPointer())->columnCount();
    } else {
        return m_pRootItem->columnCount();
    }
}

//------------------------------------------------------------------------
void QtdDirTreeModel::setupModelData(const QtdFileEntryInfoList& entryList, QtdDirTreeItem *parent)
{
    for (int i = 0; i < entryList.count(); i++) {
        QtdFileEntryInfo info = entryList.at(i);
        if (info.bIsDir) {
            QtdDirTreeItem* pCurrent = new QtdDirTreeItem(info, parent);
            setupModelData(info.files, pCurrent);
        }
    }

    for (int i = 0; i < entryList.count(); i++) {
        QtdFileEntryInfo info = entryList.at(i);
        if (!info.bIsDir) {
            QtdDirTreeItem* pCurrent = new QtdDirTreeItem(info, parent);
        }
    }
}

//------------------------------------------------------------------------
QString QtdDirTreeModel::selectedDir(QTreeView* pView)
{
    QModelIndex index = pView->selectionModel()->currentIndex();
    QString dir("");

    if (index.isValid()) {
        dir = static_cast<QtdDirTreeItem*>(index.internalPointer())->dirName();
    }

    return dir;
}

//==============================================================================
QtdCheckBoxListDelegate::QtdCheckBoxListDelegate(QObject *parent)
: QItemDelegate(parent)
, m_pParent(dynamic_cast<QtdCheckBoxList*>(parent))
{}

//--------------------------------------------------------------------------
void QtdCheckBoxListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    //Get item data
    bool value = index.data(Qt::CheckStateRole).toBool();
    QString text = index.data(Qt::DisplayRole).toString();

    // fill style options with item data
    const QStyle *style = QApplication::style();
    QStyleOptionButton opt;
    opt.state |= value ? QStyle::State_On : QStyle::State_Off;
    opt.state |= QStyle::State_Enabled;
    opt.text = text;
    opt.rect = option.rect;

    // draw item data as CheckBox
    style->drawControl(QStyle::CE_CheckBox,&opt,painter);
}

//--------------------------------------------------------------------------
QWidget* QtdCheckBoxListDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem & /*option*/, const QModelIndex & /*index*/) const
{
    // create check box as our editor
    QCheckBox *editor = new QCheckBox(parent);
    connect(editor, SIGNAL(stateChanged(int)), this, SLOT(commitAndCloseEditor(int)));
    return editor;
}

//--------------------------------------------------------------------------
 void QtdCheckBoxListDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
 {
     //set editor data
     QCheckBox *myEditor = static_cast<QCheckBox*>(editor);
     myEditor->setText(index.data(Qt::DisplayRole).toString());
     myEditor->setChecked(index.data(Qt::CheckStateRole).toBool());
 }

//--------------------------------------------------------------------------
void QtdCheckBoxListDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    //get the value from the editor (CheckBox)
    QCheckBox *myEditor = static_cast<QCheckBox*>(editor);
    bool value = myEditor->isChecked();
    bool oldValue = model->data(index, Qt::CheckStateRole).toBool();

    if (value != oldValue && m_pParent) {
        //set model data
        QMap<int,QVariant> data = model->itemData(index);
        data[Qt::DisplayRole]       = myEditor->text();
        data[Qt::CheckStateRole]    = value;
        model->setItemData(index,data);

        m_pParent->slot_checkStateChanged(index.row(), (int)value);
    }

}

//--------------------------------------------------------------------------
void QtdCheckBoxListDelegate::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex & /*index*/) const
{
    editor->setGeometry(option.rect);
    editor->setFocus();
}

//--------------------------------------------------------------------------
void QtdCheckBoxListDelegate::commitAndCloseEditor(int)
{
     QCheckBox *editor = qobject_cast<QCheckBox *>(sender());
     if (editor) {
         emit commitData(editor);
         //emit closeEditor(editor);
     }
}


//==============================================================================
QtdCheckBoxList::QtdCheckBoxList(QWidget *widget, QString emtyText, int nVersion)
:QComboBox(widget)
,m_currentDisplayText(emtyText)
,m_emtyDisplayText(emtyText)
,m_nVersion(nVersion)
,m_bAutoBuildDisplayText(true)
,m_displayTextSeparator(",")
{
    // set delegate items view 
    view()->setItemDelegate(new QtdCheckBoxListDelegate(this));

    // Enable editing on items view
    view()->setEditTriggers(QAbstractItemView::CurrentChanged);

    // set "CheckBoxList::eventFilter" as event filter for items view 
    view()->viewport()->installEventFilter(this);

    // it just cool to have it as defualt ;)
    view()->setAlternatingRowColors(true);
}


//------------------------------------------------------------------------------
QtdCheckBoxList::~QtdCheckBoxList()
{
}

//------------------------------------------------------------------------------
void QtdCheckBoxList::addItem(const QString& text, bool checked, const QVariant& userData)
{
    QComboBox::addItem(text, userData);
    this->setChecked(this->count()-1, checked);
}


//------------------------------------------------------------------------------
void QtdCheckBoxList::addItem(const QIcon& icon, const QString& text, bool checked, const QVariant& userData)
{
    QComboBox::addItem(icon, text, userData);
    this->setChecked(this->count()-1, checked);
}

//------------------------------------------------------------------------------
void QtdCheckBoxList::addItem(const QString& text, const QVariant& userData)
{
    QComboBox::addItem(text, userData);
    update();
}


//------------------------------------------------------------------------------
void QtdCheckBoxList::addItem(const QIcon& icon, const QString& text, const QVariant& userData)
{
    QComboBox::addItem(icon, text, userData);
    update();
}


//------------------------------------------------------------------------------
void QtdCheckBoxList::addItems(const QStringList& texts)
{
    QComboBox::addItems(texts);
    update();
}

//------------------------------------------------------------------------------
bool QtdCheckBoxList::eventFilter(QObject *object, QEvent *event)
{
      // don't close items view after we release the mouse button
      // by simple eating MouseButtonRelease in viewport of items view
      if(event->type() == QEvent::MouseButtonRelease && object==view()->viewport())
      {
            return true;
      }
      return QComboBox::eventFilter(object,event);
}


//------------------------------------------------------------------------------
void QtdCheckBoxList::paintEvent(QPaintEvent *)
{
    rebuildDisplayText();

    QStylePainter painter(this);
    painter.setPen(palette().color(QPalette::Text));

    // draw the combobox frame, focusrect and selected etc.
    QStyleOptionComboBox opt;
    initStyleOption(&opt);

    // if no display text been set , use m_emtyDisplayText as default
    if(m_currentDisplayText.isNull()) {
        opt.currentText = m_emtyDisplayText;
    } else {
        opt.currentText = m_currentDisplayText;
    }

    painter.drawComplexControl(QStyle::CC_ComboBox, opt);

    // draw the icon and text
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
}


//------------------------------------------------------------------------------
void QtdCheckBoxList::setDisplayText(QString text)
{
    m_currentDisplayText = text;
    update();
}

//------------------------------------------------------------------------------
QString QtdCheckBoxList::displayText() const
{
    return m_currentDisplayText;
}

//------------------------------------------------------------------------------
void QtdCheckBoxList::setEmptyDisplayText(QString text)
{
    m_emtyDisplayText = text;
    update();
}

//------------------------------------------------------------------------------
QString QtdCheckBoxList::emptyDisplayText() const
{
    return m_emtyDisplayText;
}

//------------------------------------------------------------------------------
void QtdCheckBoxList::setAutoBuildDisplayText(bool bAutoBuild, QString separator)
{
    m_bAutoBuildDisplayText = bAutoBuild;
    m_displayTextSeparator = separator;
    update();
}

//------------------------------------------------------------------------------
void QtdCheckBoxList::rebuildDisplayText()
{
    if (m_bAutoBuildDisplayText) {
        QStringList texts;
        for (int i = 0; i < count(); i++) {
            if (isChecked(i)) texts << itemText(i);
        }
        m_currentDisplayText = texts.join(m_displayTextSeparator);
    }
}


//------------------------------------------------------------------------------
bool QtdCheckBoxList::isChecked(int nIdx)
{
    if (nIdx < this->count()) {
        return this->itemData(nIdx, Qt::CheckStateRole).toBool();
    } else {
        return false;
    }
}

//------------------------------------------------------------------------------
void QtdCheckBoxList::setChecked(int nIdx, bool bChecked)
{
    if (this->count() > nIdx) {
        this->setItemData(nIdx, bChecked, Qt::CheckStateRole);
        update();
    }
}

//------------------------------------------------------------------------------
void QtdCheckBoxList::hidePopup()
{
    QComboBox::hidePopup();
    emit popupHidden();
}

//------------------------------------------------------------------------------
void QtdCheckBoxList::showPopup()
{
    QComboBox::showPopup();

    // this fixes the "first-item-is-not-checkable" issue
    view()->setCurrentIndex(model()->index(1, 0));
}

//------------------------------------------------------------------------------
void QtdCheckBoxList::slot_checkStateChanged(int row, int state)
{
    update();
    emit checkStateChanged(row, state);
}

//==============================================================================
QtdWidget::QtdWidget(QWidget* parent, Qt::WindowFlags f)
: QWidget(parent, f)
{}

//------------------------------------------------------------------------------
QtdWidget::~QtdWidget()
{
    m_data.clear();
}

//------------------------------------------------------------------------------
void QtdWidget::setData(int role, const QVariant& data)
{
    m_data[role] = data;
}

//------------------------------------------------------------------------------
QVariant& QtdWidget::data(int role)
{
    if (m_data.contains(role)) {
        return m_data[role];
    } else {
        return m_emptyData;
    }
}

//------------------------------------------------------------------------------
void QtdWidget::enterEvent(QEvent*)
{
    emit entered();
}

//------------------------------------------------------------------------------
void QtdWidget::leaveEvent(QEvent*)
{
    emit left();
}

//------------------------------------------------------------------------------
void QtdWidget::mouseDoubleClickEvent(QMouseEvent*)
{
    emit doubleClicked();
}

//------------------------------------------------------------------------------
void QtdWidget::mouseReleaseEvent(QMouseEvent*)
{
    emit clicked();
}

//------------------------------------------------------------------------------
void QtdWidget::adjustSize()
{
    QWidget::adjustSize();
}

//==============================================================================
QtdTreeListBox::QtdTreeListBox(QWidget* pParent)
: QComboBox(pParent)
 ,m_pTreeWidget(0L)
{
    m_pTreeWidget = new QTreeWidget(this);

    this->connect(this, SIGNAL(currentIndexChanged(int)), SLOT(slot_currentIndexChanged(int)));

    m_pTreeWidget->setHeaderHidden(true);
    m_pTreeWidget->setItemsExpandable(false);
    m_pTreeWidget->setAlternatingRowColors(true);

    this->setModel(m_pTreeWidget->model());
    this->setView(m_pTreeWidget);
}

//------------------------------------------------------------------------------
QTreeWidget* QtdTreeListBox::treeWidget()
{
    return m_pTreeWidget;
}

//------------------------------------------------------------------------------
void QtdTreeListBox::showPopup()
{
    m_pTreeWidget->expandAll();
    QComboBox::showPopup();
}

//------------------------------------------------------------------------------
void QtdTreeListBox::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    painter.setPen(palette().color(QPalette::Text));

    // draw the combobox frame, focusrect and selected etc.
    QStyleOptionComboBox opt;
    initStyleOption(&opt);

    // init text
    opt.currentText = itemPath(m_pTreeWidget->currentItem());
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);

    // draw the icon and text
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
}

//------------------------------------------------------------------------------
QString QtdTreeListBox::itemPath(QTreeWidgetItem* item)
{
    QTreeWidgetItem* pCurIt = item;
    if (!pCurIt) pCurIt = m_pTreeWidget->currentItem();
    if (!pCurIt) pCurIt = m_pTreeWidget->topLevelItem(0);
    if (!pCurIt) return "";

    QString text = pCurIt->text(0);
    while (pCurIt->parent()) {
        if (!pCurIt->parent()->text(0).isEmpty()) {
            text = pCurIt->parent()->text(0) + "/" + text;
        }
        pCurIt = pCurIt->parent();
    }
    return text;
}

//------------------------------------------------------------------------------
void QtdTreeListBox::slot_itemClicked(QTreeWidgetItem* item, int)
{
    emit selectedPath(itemPath(item));
}

//------------------------------------------------------------------------------
void QtdTreeListBox::slot_currentIndexChanged(int)
{
    emit selectedPath(itemPath());
}