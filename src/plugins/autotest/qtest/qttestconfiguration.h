// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../testconfiguration.h"

namespace Autotest {
namespace Internal {

class QtTestConfiguration : public DebuggableTestConfiguration
{
public:
    explicit QtTestConfiguration(ITestFramework *framework)
        : DebuggableTestConfiguration(framework) {}
    TestOutputReader *createOutputReader(Utils::QtcProcess *app) const override;
    QStringList argumentsForTestRunner(QStringList *omitted = nullptr) const override;
    Utils::Environment filteredEnvironment(const Utils::Environment &original) const override;
};

} // namespace Internal
} // namespace Autotest
