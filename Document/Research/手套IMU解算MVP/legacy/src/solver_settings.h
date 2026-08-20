#pragma once

#include <QString>

struct SolverSettings {
    double accelerometerRangeG = 16.0;
    double gyroscopeRangeDps = 2000.0;
    double magnetometerDivisor = 120.0;
    double madgwickBeta = 0.10;
    bool magnetometerEnabled = true;
    double magnetometerMinNorm = 0.01;
    double magnetometerMaxNorm = 1.0e9;

    static SolverSettings defaults();
    bool isValid(QString *errorMessage = nullptr) const;
    bool operator==(const SolverSettings &other) const;
};
