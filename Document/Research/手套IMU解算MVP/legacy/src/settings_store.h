#pragma once

#include "solver_settings.h"

#include <QSettings>

class SettingsStore final {
public:
    explicit SettingsStore(QSettings &settings);

    SolverSettings load() const;
    bool save(const SolverSettings &value, QString *errorMessage = nullptr);
    void clear();

private:
    QSettings &settings_;
};
