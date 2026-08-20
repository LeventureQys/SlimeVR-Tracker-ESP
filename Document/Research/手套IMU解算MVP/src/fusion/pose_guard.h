#pragma once

#include "quaternion_util.h"

#include <QQuaternion>
#include <QString>

namespace handstudio {

// Pre-publish orientation protection: finite check, norm-error check, sign
// continuity (q and -q), and holding the last valid orientation with a "held"
// marker when the candidate is invalid.
struct PoseGuardResult {
    QQuaternion orientation;
    bool held = false;   // previous valid orientation was held
    bool valid = false;  // a usable orientation is available at all
};

class PoseGuard {
public:
    struct Config {
        double maxNormError = 1.0e-4;

        bool isValid(QString *reason = nullptr) const;
    };

    explicit PoseGuard(const Config &config = {});
    void reset();
    PoseGuardResult protect(const QQuaternion &candidate);
    PoseGuardResult holdLast() const;
    bool hasValidPose() const;
    QQuaternion lastPose() const;
    Config config() const;

private:
    Config config_;
    bool hasValid_ = false;
    QQuaternion lastPose_;
};

}
