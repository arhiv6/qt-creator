// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "gdbserverprovider.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QPlainTextEdit;
QT_END_NAMESPACE

namespace Utils { class PathChooser; }

namespace BareMetal::Internal {

// GenericGdbServerProvider

class GenericGdbServerProvider final : public GdbServerProvider
{
public:
    QVariantMap toMap() const final;
    bool fromMap(const QVariantMap &data) final;

    bool operator==(const IDebugServerProvider &other) const final;

    Utils::CommandLine command() const final;

private:
    GenericGdbServerProvider();
    QSet<StartupMode> supportedStartupModes() const final;

    Utils::FilePath m_executableFile;
    QString m_additionalArguments;
    
    friend class GenericGdbServerProviderConfigWidget;
    friend class GenericGdbServerProviderFactory;
    friend class BareMetalDevice;
};

// GenericGdbServerProviderFactory

class GenericGdbServerProviderFactory final : public IDebugServerProviderFactory
{
public:
    GenericGdbServerProviderFactory();
};

// GenericGdbServerProviderConfigWidget

class GenericGdbServerProviderConfigWidget final
        : public GdbServerProviderConfigWidget
{
public:
    explicit GenericGdbServerProviderConfigWidget(
            GenericGdbServerProvider *provider);

private:
    void apply() final;
    void discard() final;

    void setFromProvider();

    HostWidget *m_hostWidget = nullptr;
    Utils::PathChooser *m_executableFileChooser = nullptr;
    QLineEdit *m_additionalArgumentsLineEdit = nullptr;
    QCheckBox *m_useExtendedRemoteCheckBox = nullptr;
    QPlainTextEdit *m_initCommandsTextEdit = nullptr;
    QPlainTextEdit *m_resetCommandsTextEdit = nullptr;
};

} // BareMetal::Internal
