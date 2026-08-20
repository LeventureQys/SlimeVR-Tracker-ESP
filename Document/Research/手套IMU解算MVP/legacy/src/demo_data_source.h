#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <QtTypes>

class DemoDataSource final : public QObject {
    Q_OBJECT

public:
    explicit DemoDataSource(QObject *parent = nullptr);
    ~DemoDataSource() override;

    void start();
    void stop();
    bool isActive() const;

signals:
    void bytesReady(const QByteArray &bytes, qint64 monotonicNs);

private:
    void generateTick();
    QByteArray makeFrame(quint8 address, quint8 sequence, int sensorIndex) const;

    QTimer timer_;
    QElapsedTimer monotonicTimer_;
    quint8 sequence_ = 0;
    quint64 tickIndex_ = 0;
    double phase_ = 0.0;
};
