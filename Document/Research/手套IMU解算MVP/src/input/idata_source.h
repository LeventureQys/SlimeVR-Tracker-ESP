#pragma once

#include "core/diagnostic.h"

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QtGlobal>

namespace handstudio {

enum class SourceState {
    Idle,
    Starting,
    Running,
    Paused,
    Stopping,
    Error
};

// Common data-source contract (task book section 4). Serial and replay both
// feed the parser through the same bytesReady entry point.
class IDataSource : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~IDataSource() override = default;

signals:
    void bytesReady(const QByteArray &bytes, qint64 monotonicNs);
    void stateChanged(SourceState state, const Diagnostic &diagnostic);

public slots:
    virtual void start() = 0;
    virtual void stop() = 0;
};

}

Q_DECLARE_METATYPE(handstudio::SourceState)
