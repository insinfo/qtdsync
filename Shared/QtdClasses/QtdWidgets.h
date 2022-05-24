#ifndef QTDWIDGETS_H
#define QTDWIDGETS_H

#include "QtdBase.h"
#include "QtdTools.h"

#ifdef Q_WS_WIN
//==============================================================================
/**
  * This is a helper class for using the Desktop Window Manager
  * functionality on Windows 7 and Windows Vista. On other platforms
  * these functions will simply not do anything.
  */

// taken from Qt Labs
/*
 * Internal helper class that notifies windows if the
 * DWM compositing state changes and updates the widget
 * flags correspondingly.
 */
//==============================================================================
class WindowNotifier : public QWidget
{
    Q_OBJECT
public:
    WindowNotifier() { winId(); }
    void addWidget(QWidget *widget);
    void removeWidget(QWidget *widget) { widgets.removeAll(widget); }
    bool winEvent(MSG *message, long *result);

public slots:
    void widgetDestroyed(QObject* obj = 0);

private:
    QWidgetList widgets;
};
#endif // Q_WS_WIN

//==============================================================================
class QtWin : public QObject
{
public:
    static bool enableBlurBehindWindow(QWidget *widget, bool enable = true);
    static bool extendFrameIntoClientArea(QWidget *widget,
                                          int left = -1, int top = -1,
                                          int right = -1, int bottom = -1);
    static bool isCompositionEnabled();
    static QColor colorizatinColor();

#ifdef Q_WS_WIN
private:
    static WindowNotifier *windowNotifier();
#endif
};

//==============================================================================
class QtdToolTipLabel : public QLabel
{
public:
    QtdToolTipLabel(QWidget* pParent = 0L) : QLabel(pParent) {};
    void setDefaultText(QString text)
    {
        m_defText = text;
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event)
    {
        QWidget* pWidget = qobject_cast<QWidget*>(obj);
        QString toolTip;

        // special handling to tabbars
        QTabBar* pTabBar = qobject_cast<QTabBar*>(pWidget);
        if (pTabBar) {
            if (event->type() == QEvent::HoverMove) {
                int nTabIdx = pTabBar->tabAt(static_cast<QHoverEvent*>(event)->pos());
                if (nTabIdx != -1) {
                    // can't use QTabBar::tabToolTip since uic seems to be buggy for that one
                    QTabWidget* pTabWidget = qobject_cast<QTabWidget*>(pTabBar->parentWidget());
                    if (pTabWidget) toolTip = pTabWidget->widget(nTabIdx)->toolTip();
                }
            }
        } else if (event->type() == QEvent::Enter) {
            toolTip = pWidget->toolTip();
        }

        if (!toolTip.isEmpty() && toolTip != this->text()) {
            this->setText(toolTip);
        } else if (event->type() == QEvent::HoverLeave) {
            this->setText(m_defText);
        } else if (event->type() == QEvent::ToolTip) {
            return true;
        }

        // standard event processing
        return QObject::eventFilter(obj, event);
     }

    QString m_defText;
};

//==============================================================================
class QtdTabWidget : public QTabWidget
{
    Q_OBJECT
public:
    QtdTabWidget(QWidget* parent) : QTabWidget(parent) {}

signals:
    void    resized();

protected:
    virtual void resizeEvent(QResizeEvent* event) {
        emit resized();
        QTabWidget::resizeEvent(event);
    }
};


//==============================================================================
class QtdTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit QtdTreeWidget(QWidget* parent);

signals:
    void dataDropped(QMimeData*);
    void dataImageDataDropped(QPixmap);
    void dataTextDropped(QString);
    void dataHtmlDropped(QString);
    void dataUrlsDropped(QStringList);

protected:
    void dragEnterEvent(QDragEnterEvent *event);
    void dragMoveEvent(QDragMoveEvent *event);
    void dragLeaveEvent(QDragLeaveEvent *event);
    void dropEvent(QDropEvent *event);
};

//==============================================================================
class QtdLineEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit QtdLineEdit(QWidget* parent);

    void setCompleter(QCompleter *completer);
    void setCompleterWordBoundary(QString wordBoundary);

signals:
    void dataDropped(QMimeData*);
    void dataImageDataDropped(QPixmap);
    void dataTextDropped(QString);
    void dataHtmlDropped(QString);
    void dataUrlsDropped(QStringList);
    void focusLost();

protected slots:
    void insertCompletion(QString);

protected:
    void dragEnterEvent(QDragEnterEvent *event);
    void dragMoveEvent(QDragMoveEvent *event);
    void dragLeaveEvent(QDragLeaveEvent *event);
    void dropEvent(QDropEvent *event);
    void keyPressEvent(QKeyEvent *event);
    void focusInEvent(QFocusEvent *event);

    QCompleter* c;
    QString     wordBoundary;
};

//==============================================================================
class QtdDirTreeItem;
class QtdDirTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    //------------------------------------------------------------------------
    QtdDirTreeModel(const QtdFileEntryInfo& rootEntry, QObject* pParent = 0L);
    ~QtdDirTreeModel();

    QVariant        data(const QModelIndex &index, int role) const;
    Qt::ItemFlags   flags(const QModelIndex &index) const;
    QVariant        headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
    QModelIndex     index(int row, int column, const QModelIndex &parent = QModelIndex()) const;
    QModelIndex     parent(const QModelIndex &index) const;
    int             rowCount(const QModelIndex &parent = QModelIndex()) const;
    int             columnCount(const QModelIndex &parent = QModelIndex()) const;
    QString         selectedDir(QTreeView* pView);

    QtdDirTreeItem* rootItem();

private:
    void            setupModelData(const QtdFileEntryInfoList& entryList, QtdDirTreeItem *parent);

    QtdDirTreeItem* m_pRootItem;
};

//==============================================================================
class QtdCheckBoxList;
class QtdCheckBoxListDelegate : public QItemDelegate
{
    Q_OBJECT
public:
    QtdCheckBoxListDelegate(QObject *parent);

    //--------------------------------------------------------------------------
    void        paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;
    QWidget*    createEditor(QWidget *parent, const QStyleOptionViewItem & /*option*/, const QModelIndex & /*index*/) const;
    void        setEditorData(QWidget *editor, const QModelIndex &index) const;
    void        setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const;
    void        updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex & /*index*/) const;

private slots:
    void        commitAndCloseEditor(int);

private:
    QtdCheckBoxList*   m_pParent;
 }; 



//==============================================================================
class QtdCheckBoxList: public QComboBox
{
    Q_OBJECT
public:
    QtdCheckBoxList(QWidget *widget = 0, QString emtyDisplayText = "", int nVersion = 0);
    ~QtdCheckBoxList();

    void            addItem(const QString& text, bool checked, const QVariant& userData = QVariant());
    void            addItem(const QIcon& icon, const QString& text, bool checked, const QVariant& userData = QVariant());
    void            addItem(const QString& text, const QVariant& userData = QVariant());
    void            addItem(const QIcon& icon, const QString& text, const QVariant& userData = QVariant());
    void            addItems(const QStringList& texts);

    void            setAutoBuildDisplayText(bool bAutoBuild, QString separator = ",");
    void            setDisplayText(QString text);
    QString         displayText() const;

    void            setEmptyDisplayText(QString text);
    QString         emptyDisplayText() const;

    bool            isChecked(int nIdx);
    void            setChecked(int nIdx, bool bChecked = true);

    virtual void    hidePopup();
    virtual void    showPopup();
    virtual bool    eventFilter(QObject *object, QEvent *event);
    virtual void    paintEvent(QPaintEvent *);


public slots:
    void            slot_checkStateChanged(int, int);

signals:
    void            checkStateChanged(int nIdx, int nState);
    void            popupHidden();

protected:
    void            rebuildDisplayText();

private:
    QString         m_currentDisplayText;
    QString         m_emtyDisplayText;
    int             m_nVersion;
    bool            m_bAutoBuildDisplayText;
    QString         m_displayTextSeparator;
};

//==============================================================================
// http://developer.qt.nokia.com/faq/answer/how_can_i_do_multiselection_in_a_qcalendarwidget
class QtCalendarWidget : public QCalendarWidget {
  Q_OBJECT
public:
  QtCalendarWidget(QWidget *parent = 0) : QCalendarWidget(parent)
  {
    view = qFindChild<QTableView *>(this);
    view->viewport()->installEventFilter(this);
  }
 
  bool eventFilter(QObject *obj, QEvent *event)
  {
    if (obj->parent() && obj->parent() == view) {
      if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonRelease) {
          QMouseEvent *me = static_cast<QMouseEvent*>(event);
          QPoint pos = me->pos();
          if (event->type() == QEvent::MouseButtonPress &&
            !(me->modifiers() & Qt::ShiftModifier)) {
              QModelIndex idx = view->indexAt(pos);
              if (idx.row() != 0 && idx.column() != 0)
                startIndex = idx;
          } else if (event->type() == QEvent::MouseButtonRelease &&
            me->modifiers() & Qt::ShiftModifier) {
              QModelIndex idx = view->indexAt(pos);
              if (idx.row() != 0 && idx.column() != 0)
                endIndex = idx;
              else
                return false;
              if (!startIndex.isValid())
                startIndex =
                view->selectionModel()->selectedIndexes().first();
              endIndex = view->indexAt(pos);
              int rowStart = startIndex.row();
              int rowEnd = endIndex.row();
              int colStart = startIndex.column();
              int colEnd = endIndex.column();
              QItemSelection sel;
              for (int i=rowStart;i<=rowEnd;i++) {
                if (i == rowStart && i != rowEnd) {
                  for (int j=colStart;
                    j<view->model()->columnCount();j++)
                    view->selectionModel()->select(
                    view->model()->index(i, j),
                    QItemSelectionModel::Select);
                } else if (i == rowEnd) {
                  int start = (i == rowStart) ? colStart : 1;
                  for (int j = start;j<colEnd;j++)
                    view->selectionModel()->select(
                    view->model()->index(i, j),
                    QItemSelectionModel::Select);
                } else {
                  for (int j=1;j<view->model()->columnCount();j++)
                    view->selectionModel()->select(
                    view->model()->index(i, j),
                    QItemSelectionModel::Select);
                }
              }
              view->selectionModel()->select(endIndex,
                QItemSelectionModel::Select);
              return true;
          }
      }
      return false;
    } else {
      return QCalendarWidget::eventFilter(obj, event);
    }
  }
 
private:
  QTableView *view;
  QPersistentModelIndex startIndex;
  QPersistentModelIndex endIndex;
};

//==============================================================================
class QtdWidget : public QWidget
{
    Q_OBJECT
public:
    QtdWidget(QWidget* parent = 0L, Qt::WindowFlags f = 0);
    ~QtdWidget();

    void setData(int role, const QVariant& data);
    QVariant& data(int role);

public slots:
    void adjustSize();

signals:
    void entered();
    void left();
    void doubleClicked();
    void clicked();

protected:
    virtual void enterEvent(QEvent*);
    virtual void leaveEvent(QEvent*);
    virtual void mouseDoubleClickEvent(QMouseEvent*);
    virtual void mouseReleaseEvent(QMouseEvent*);

    QMap<int, QVariant>    m_data;
    QVariant               m_emptyData;
};

//============================================================================
class QtdWin7Dialog : public QDialog
{
    Q_OBJECT
public:
    QtdWin7Dialog(QWidget *parent = 0, Qt::WindowFlags f = 0)
        : QDialog(parent, f)
    {
    }

    void setAero() {
        if (QtWin::isCompositionEnabled()) {
            QtWin::extendFrameIntoClientArea(this);
            if (!this->isMaximized()) {
                this->layout()->setContentsMargins(0,0,0,0);
            } else {
                this->layout()->setContentsMargins(6,6,6,6);
            }
        }
    }

protected:
    virtual void resizeEvent(QResizeEvent* event)
    {
        setAero();
        QDialog::resizeEvent(event);
    }

    virtual void showEvent(QShowEvent* event)
    {
        setAero();
        QDialog::showEvent(event);
    }
};

//============================================================================
class QtdTreeListBox : public QComboBox
{
    Q_OBJECT
public:
    QtdTreeListBox(QWidget* pParent = 0L);

    QTreeWidget* treeWidget();
    virtual void showPopup();
    virtual void paintEvent(QPaintEvent *);
    QString itemPath(QTreeWidgetItem* item = 0L);

signals:
    void selectedPath(QString);

protected slots:
    void slot_itemClicked(QTreeWidgetItem* item, int column);
    void slot_currentIndexChanged(int);

protected:
    QTreeWidget* m_pTreeWidget;
};

//============================================================================
class QtdCheckBox : public QCheckBox
{
    Q_OBJECT
public:
    QtdCheckBox(QWidget* pParent = 0L) : QCheckBox(pParent) 
    {
        this->connect(this, SIGNAL(stateChanged(int)), SLOT(slot_stateChanged(int)));
    };

public slots:
    void slot_stateChanged(int) {
        emit stateChanged(this->isChecked());
    }

signals:
    void stateChanged(bool);
};

//============================================================================
class QtdPlainTextEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    QtdPlainTextEdit(QWidget* pParent = 0L) : QPlainTextEdit(pParent) 
    {
        this->connect(this, SIGNAL(textChanged()), SLOT(slot_textChanged()));
    };

public slots:
    void slot_textChanged() {
        emit textChanged(this->toPlainText());
    }

signals:
    void textChanged(const QString& text);
};

#endif // QTDTOOLS_H
