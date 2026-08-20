#pragma once

#include "slimevr_settings.h"

#include <QSettings>

// Persists SlimeVrSettings under the "slimevr/" key group. The 6-byte device
// id is generated once and stored; load() may therefore write (creation of a
// missing id) and is not const.
class SlimeVrSettingsStore final {
public:
    explicit SlimeVrSettingsStore(QSettings &settings);

    SlimeVrSettings load();
    bool save(const SlimeVrSettings &value, QString *errorMessage = nullptr);
    void clear();

private:
    QSettings &settings_;
};
