#pragma once

#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace handstudio {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
    Fatal
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    QString code;
    QString message;
    QString detail;
    qint64 timestampNs = 0;
};

}

Q_DECLARE_METATYPE(handstudio::DiagnosticSeverity)
Q_DECLARE_METATYPE(handstudio::Diagnostic)
