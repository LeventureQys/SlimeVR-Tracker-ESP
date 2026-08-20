#pragma once

#include <QDir>
#include <QUuid>

namespace handstudio::testutil {

// RAII temporary directory under the current working directory (the workspace),
// because sandboxed CI blocks file writes inside the system temp tree. The
// directory is removed recursively on destruction.
class TestTempDir {
public:
    TestTempDir()
    {
        const QString base = QDir::current().absolutePath();
        path_ = base + QStringLiteral("/s2test_")
                + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(path_);
    }

    ~TestTempDir()
    {
        if (!path_.isEmpty()) {
            QDir(path_).removeRecursively();
        }
    }

    TestTempDir(const TestTempDir &) = delete;
    TestTempDir &operator=(const TestTempDir &) = delete;

    QString path() const
    {
        return path_;
    }

private:
    QString path_;
};

}
