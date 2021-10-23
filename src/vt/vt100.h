#ifndef VT100_H
#define VT100_H

#include "vt.h"

class VT100 : public VT
{
public:
    VT100(QVTerminal *terminal);

    // VT interface
public:
    QByteArray dataFromKey(QString text, int key, Qt::KeyboardModifiers modifiers);
};

#endif // VT100_H
