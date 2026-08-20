#pragma once

#include "input/idata_source.h"

#include <QElapsedTimer>
#include <QTimer>

namespace handstudio {

class DemoDataSource final : public IDataSource {
    Q_OBJECT

public:
    explicit DemoDataSource(QObject *parent = nullptr);
    SourceState state() const noexcept;

public slots:
    void start() override;
    void stop() override;

private slots:
    void emitGroup();

private:
    QTimer timer_;
    QElapsedTimer elapsed_;
    SourceState state_ = SourceState::Idle;
    quint8 sequence_ = 0;
};

}
