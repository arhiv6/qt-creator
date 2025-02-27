// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/dialogs/ioptionspage.h>

namespace Copilot {

class CopilotOptionsPage : public Core::IOptionsPage
{
public:
    CopilotOptionsPage();
    ~CopilotOptionsPage() override;

    static CopilotOptionsPage &instance();

    void init();

    QWidget *widget() override;
    void apply() override;
    void finish() override;
};

} // namespace Copilot
