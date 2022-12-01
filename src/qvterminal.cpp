#include "qvterminal.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QStyleHints>
#include <QTextCodec>

#include <vt/vt100.h>

QVTerminal::QVTerminal(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    _device = Q_NULLPTR;

    _cursorPos.setX(0);
    _cursorPos.setY(0);
    _cursorTimer.start(QGuiApplication::styleHints()->cursorFlashTime() / 2);
    _cvisible = true;
    connect(&_cursorTimer, &QTimer::timeout, this, &QVTerminal::toggleCursor);

    _echo = false;
    _crlf = false;
    _state = QVTerminal::Text;

    QVTCharFormat format;
    QFont font;
    font.setFamily("monospace");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(13);
    format.setFont(font);
    format.setForeground(QColor(187, 187, 187));
    format.setBackground(QColor(0, 0, 0));
    setFormat(format);

    _layout = new QVTLayout();

    _pasteAction = new QAction("Paste");
    _pasteAction->setShortcut(QKeySequence("Ctrl+V"));
    connect(_pasteAction, &QAction::triggered, this, &QVTerminal::paste);
    addAction(_pasteAction);

    _vt = new VT100(this);
}

QVTerminal::~QVTerminal()
{
}

void QVTerminal::setIODevice(QIODevice *device)
{
    _device = device;
    if (_device)
    {
        connect(_device, &QIODevice::readyRead, this, &QVTerminal::read);
        read();
    }
}

void QVTerminal::appendData(const QByteArray &data)
{
    QString text;


    setUpdatesEnabled(false);

    QTextCodec *textCodec = QTextCodec::codecForName("UTF-8");
    QString dataString = textCodec->toUnicode(data);

    QString::const_iterator it = dataString.cbegin();
    while (it != dataString.cend())
    {
        QChar c = *it;
        switch (_state)
        {
            case QVTerminal::Text:
                if (c == 0x1B)
                {
                    appendString(text);
                    text.clear();
                    _state = QVTerminal::Escape;
                }
                else if (c == '\n')
                {
                    appendString(text);
                    text.clear();
                    _layout->appendLine();

                    _cursorPos.setX(0);
                    _cursorPos.setY(_cursorPos.y() + 1);
                }
                else if (c.isPrint())
                {
                    text.append(c);
                }
                break;

            case QVTerminal::Escape:
                _formatValue = 0;
                if (c == '[')
                {
                    _state = QVTerminal::Format;
                }
                else if (c == '(')
                {
                    _state = QVTerminal::ResetFont;
                }
                break;

            case QVTerminal::Format:
                if (c >= '0' && c <= '9')
                {
                    _formatValue = _formatValue * 10 + (c.cell() - '0');
                }
                else
                {
                    if (c == ';' || c == 'm')
                    {
                        if (_formatValue == 0)  // reset format
                        {
                            _curentFormat = _format;
                        }
                        else if (_formatValue == 4)  // underline
                        {
                            _curentFormat.font().setUnderline(true);
                        }
                        else if (_formatValue == 7)  // reverse
                        {
                            QColor foreground = _curentFormat.foreground();
                            _curentFormat.setForeground(_curentFormat.background());
                            _curentFormat.setBackground(foreground);
                        }
                        else if (_formatValue / 10 == 3)  // foreground
                        {
                            _curentFormat.setForeground(vt100color(static_cast<char>(_formatValue % 10) + '0'));
                        }
                        else if (_formatValue / 10 == 4)  // background
                        {
                            _curentFormat.setBackground(vt100color(static_cast<char>(_formatValue % 10) + '0'));
                        }

                        if (c == ';')
                        {
                            _formatValue = 0;
                            _state = QVTerminal::Format;
                        }
                        else
                        {
                            _state = QVTerminal::Text;
                        }
                    }
                    else
                    {
                        _state = QVTerminal::Text;
                    }
                }
                break;

            case QVTerminal::ResetFont:
                _curentFormat = _format;
                _state = QVTerminal::Text;
                break;
        }
        it++;
    }
    appendString(text);

    verticalScrollBar()->setRange(0, _ch * (_layout->lineCount() + 1) - viewport()->size().height() + 6);
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());

    setUpdatesEnabled(true);
    update();
}

void QVTerminal::paste()
{
    QByteArray data;
    data.append(QApplication::clipboard()->text().toUtf8());
    writeData(data);
}

QColor QVTerminal::vt100color(char c)
{
    switch (c)
    {
        case '1':
            return QColor(Qt::red);

        case '2':
            return QColor(Qt::green);

        case '3':
            return QColor(Qt::yellow);

        case '4':
            return QColor(Qt::blue);

        case '5':
            return QColor(Qt::magenta);

        case '6':
            return QColor(Qt::cyan);

        case '7':
            return QColor(Qt::white);

        default:
            return QColor(Qt::black);
    }
}

void QVTerminal::read()
{
    if (!_device)
    {
        return;
    }
    if (_device->isReadable())
    {
        appendData(_device->readAll());
    }
}

void QVTerminal::appendString(const QString &str)
{
    foreach (QChar c, str)
    {
        QVTChar termChar(c, _curentFormat);
        _layout->lineAt(_cursorPos.y()).append(termChar);
        _cursorPos.setX(_cursorPos.x() + 1);
    }
}

void QVTerminal::toggleCursor()
{
    _cvisible = !_cvisible;
    viewport()->update();
}

bool QVTerminal::crlf() const
{
    return _crlf;
}

void QVTerminal::setCrlf(bool crlf)
{
    _crlf = crlf;
}

void QVTerminal::writeData(const QByteArray &data)
{
    if (_device && _device->isWritable())
    {
        _device->write(data);
    }
    if (_echo)
    {
        appendData(data);
    }
}

bool QVTerminal::echo() const
{
    return _echo;
}

void QVTerminal::setEcho(bool echo)
{
    _echo = echo;
}

const QVTCharFormat &QVTerminal::format() const
{
    return _format;
}

void QVTerminal::setFormat(const QVTCharFormat &format)
{
    _format = format;
    _curentFormat = format;

    QFontMetrics fm(_format.font());
    _cw = fm.boundingRect('M').width();
    _ch = fm.height();
}

void QVTerminal::keyPressEvent(QKeyEvent *event)
{
    QByteArray data;
    QString text = event->text();

    if (text == "\r")
    {
        if (_crlf)
        {
            data.append("\r");
        }
        data.append("\n");
    }
    else
    {
        data.append(_vt->dataFromKey(event->text(), event->key(), event->modifiers()));
    }

    writeData(data);

    QAbstractScrollArea::keyPressEvent(event);
}

void QVTerminal::paintEvent(QPaintEvent *paintEvent)
{
    Q_UNUSED(paintEvent);

    QPainter p(viewport());
    p.setPen(QColor(187, 187, 187));
    p.setBrush(QColor(0x23, 0x26, 0x29));
    p.setFont(_format.font());

    p.fillRect(viewport()->rect(), QColor(0x23, 0x26, 0x29));

    QPoint pos;
    pos.setY(3);

    int firstLine = verticalScrollBar()->value() / _ch;
    int lastLine = viewport()->size().height() / _ch + firstLine;
    if (lastLine > _layout->lineCount())
    {
        lastLine = _layout->lineCount();
    }

    for (int l = firstLine; l < lastLine; l++)
    {
        pos.setY(pos.y() + _ch);
        pos.setX(3);
        for (int c = 0; c < _layout->lineAt(l).size(); c++)
        {
            const QVTChar &vtChar = _layout->lineAt(l).chars()[c];
            pos.setX(pos.x() + _cw);
            p.setPen(vtChar.format().foreground());
            p.drawText(QRect(pos, QSize(_cw, -_ch)).normalized(), Qt::AlignCenter, QString(vtChar.c()));

            // p.setBrush(QBrush());
            // p.drawRect(QRect(pos, QSize(_cw, -_ch)));
        }
    }

    if (_cvisible)
    {
        p.fillRect(QRect((_cursorPos.x() + 1) * _cw + 3, _cursorPos.y() * _ch + 3 + firstLine, _cw, _ch), QColor(187, 187, 187));
    }
}

void QVTerminal::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event)
    verticalScrollBar()->setPageStep(_ch * 10);
    verticalScrollBar()->setSingleStep(_ch);
    verticalScrollBar()->setRange(0, _ch * (_layout->lineCount() + 1) - viewport()->size().height() + 6);
}

void QVTerminal::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton)
    {
        if (QApplication::clipboard()->supportsSelection())
        {
            QByteArray data;
            data.append(QApplication::clipboard()->text(QClipboard::Selection).toUtf8());
            writeData(data);
        }
    }
    QAbstractScrollArea::mousePressEvent(event);
}

#ifndef QT_NO_CONTEXTMENU
void QVTerminal::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.addAction(_pasteAction);
    _pasteAction->setEnabled(!QApplication::clipboard()->text().isEmpty());
    menu.exec(event->globalPos());
}
#endif  // QT_NO_CONTEXTMENU

bool QVTerminal::viewportEvent(QEvent *event)
{
    return QAbstractScrollArea::viewportEvent(event);
}
