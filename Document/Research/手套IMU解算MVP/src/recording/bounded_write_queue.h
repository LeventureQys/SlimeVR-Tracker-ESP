#pragma once

#include <QByteArray>
#include <QList>

#include <algorithm>
#include <optional>

namespace handstudio {

// Bounded FIFO for pending disk writes. When full, enqueue() fails and the
// overflow is counted (bytes + items) instead of growing without bound. This is
// the primitive behind the design doc's "disk too slow -> drop lower-priority
// layers, keep raw.bin and diagnostics" policy.
class BoundedWriteQueue {
public:
    explicit BoundedWriteQueue(int maxItems = 1024)
        : maxItems_(maxItems < 0 ? 0 : maxItems)
    {
    }

    bool enqueue(const QByteArray &item)
    {
        if (maxItems_ <= 0 || items_.size() >= maxItems_) {
            ++overflowCount_;
            droppedBytes_ += static_cast<quint64>(item.size());
            return false;
        }
        items_.append(item);
        peakItems_ = std::max(peakItems_, static_cast<quint64>(items_.size()));
        return true;
    }

    std::optional<QByteArray> dequeue()
    {
        if (items_.isEmpty()) {
            return std::nullopt;
        }
        QByteArray item = items_.takeFirst();
        return item;
    }

    int size() const
    {
        return static_cast<int>(items_.size());
    }

    int maxItems() const
    {
        return maxItems_;
    }

    void setMaxItems(int maxItems)
    {
        maxItems_ = maxItems < 0 ? 0 : maxItems;
    }

    quint64 overflowCount() const
    {
        return overflowCount_;
    }

    quint64 droppedBytes() const
    {
        return droppedBytes_;
    }

    quint64 peakItems() const
    {
        return peakItems_;
    }

    void clear()
    {
        items_.clear();
    }

private:
    QList<QByteArray> items_;
    int maxItems_;
    quint64 overflowCount_ = 0;
    quint64 droppedBytes_ = 0;
    quint64 peakItems_ = 0;
};

}
