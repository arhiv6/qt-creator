// Copyright (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "genericgdbserverprovider.h"

#include <baremetal/baremetalconstants.h>
#include <baremetal/baremetaltr.h>
#include <baremetal/debugserverprovidermanager.h>

#include <utils/fileutils.h>
#include <utils/qtcassert.h>
#include <utils/variablechooser.h>

#include <QCheckBox>
#include <QFormLayout>
#include <QPlainTextEdit>

using namespace Utils;

namespace BareMetal::Internal {

const char executableFileKeyC[] = "ExecutableFile";
const char additionalArgumentsKeyC[] = "AdditionalArguments";

// GenericGdbServerProvider

GenericGdbServerProvider::GenericGdbServerProvider()
    : GdbServerProvider(Constants::GDBSERVER_GENERIC_PROVIDER_ID)
{
    setChannel("localhost", 3333);
    setTypeDisplayName(Tr::tr("Generic"));
    setConfigurationWidgetCreator([this] { return new GenericGdbServerProviderConfigWidget(this); });
}

QSet<GdbServerProvider::StartupMode> GenericGdbServerProvider::supportedStartupModes() const
{
    return {StartupOnNetwork};
}

CommandLine GenericGdbServerProvider::command() const
{
    CommandLine cmd{m_executableFile};

    if (!m_additionalArguments.isEmpty())
        cmd.addArgs(m_additionalArguments, CommandLine::Raw);

    return cmd;
}

QVariantMap GenericGdbServerProvider::toMap() const
{
    QVariantMap data = GdbServerProvider::toMap();
    data.insert(executableFileKeyC, m_executableFile.toSettings());
    data.insert(additionalArgumentsKeyC, m_additionalArguments);
    return data;
}

bool GenericGdbServerProvider::fromMap(const QVariantMap &data)
{
    if (!GdbServerProvider::fromMap(data))
        return false;

    m_executableFile = FilePath::fromSettings(data.value(executableFileKeyC));
    m_additionalArguments = data.value(additionalArgumentsKeyC).toString();
    return true;
}

bool GenericGdbServerProvider::operator==(const IDebugServerProvider &other) const
{
    if (!GdbServerProvider::operator==(other))
        return false;

    const auto p = static_cast<const GenericGdbServerProvider *>(&other);
    return m_executableFile == p->m_executableFile
            && m_additionalArguments == p->m_additionalArguments;
}

// GenericGdbServerProviderFactory

GenericGdbServerProviderFactory::GenericGdbServerProviderFactory()
{
    setId(Constants::GDBSERVER_GENERIC_PROVIDER_ID);
    setDisplayName(Tr::tr("Generic"));
    setCreator([] { return new GenericGdbServerProvider; });
}

// GdbServerProviderConfigWidget

GenericGdbServerProviderConfigWidget::GenericGdbServerProviderConfigWidget(
        GenericGdbServerProvider *provider)
    : GdbServerProviderConfigWidget(provider)
{
    Q_ASSERT(provider);

    m_hostWidget = new HostWidget(this);
    m_mainLayout->addRow(Tr::tr("Host:"), m_hostWidget);
    
    m_executableFileChooser = new Utils::PathChooser;
    m_executableFileChooser->setExpectedKind(Utils::PathChooser::ExistingCommand);
    m_mainLayout->addRow(Tr::tr("Executable file:"), m_executableFileChooser);
    
    m_additionalArgumentsLineEdit = new QLineEdit(this);
    m_mainLayout->addRow(Tr::tr("Additional arguments:"), m_additionalArgumentsLineEdit);

    m_useExtendedRemoteCheckBox = new QCheckBox(this);
    m_useExtendedRemoteCheckBox->setToolTip(Tr::tr("Use GDB target extended-remote"));
    m_mainLayout->addRow(Tr::tr("Extended mode:"), m_useExtendedRemoteCheckBox);
    m_initCommandsTextEdit = new QPlainTextEdit(this);
    m_initCommandsTextEdit->setToolTip(defaultInitCommandsTooltip());
    m_mainLayout->addRow(Tr::tr("Init commands:"), m_initCommandsTextEdit);
    m_resetCommandsTextEdit = new QPlainTextEdit(this);
    m_resetCommandsTextEdit->setToolTip(defaultResetCommandsTooltip());
    m_mainLayout->addRow(Tr::tr("Reset commands:"), m_resetCommandsTextEdit);

    addErrorLabel();
    setFromProvider();

    const auto chooser = new Utils::VariableChooser(this);
    chooser->addSupportedWidget(m_initCommandsTextEdit);
    chooser->addSupportedWidget(m_resetCommandsTextEdit);

    connect(m_hostWidget, &HostWidget::dataChanged,
            this, &GdbServerProviderConfigWidget::dirty);
    connect(m_executableFileChooser, &Utils::PathChooser::rawPathChanged,
            this, &GdbServerProviderConfigWidget::dirty);
    connect(m_additionalArgumentsLineEdit, &QLineEdit::textChanged,
            this, &GdbServerProviderConfigWidget::dirty);
    connect(m_useExtendedRemoteCheckBox, &QCheckBox::stateChanged,
            this, &GdbServerProviderConfigWidget::dirty);
    connect(m_initCommandsTextEdit, &QPlainTextEdit::textChanged,
            this, &GdbServerProviderConfigWidget::dirty);
    connect(m_resetCommandsTextEdit, &QPlainTextEdit::textChanged,
            this, &GdbServerProviderConfigWidget::dirty);
}

void GenericGdbServerProviderConfigWidget::apply()
{
    const auto p = static_cast<GenericGdbServerProvider *>(m_provider);
    Q_ASSERT(p);

    p->setChannel(m_hostWidget->channel());
    p->m_executableFile = m_executableFileChooser->filePath();
    p->m_additionalArguments = m_additionalArgumentsLineEdit->text();
    p->setUseExtendedRemote(m_useExtendedRemoteCheckBox->isChecked());
    p->setInitCommands(m_initCommandsTextEdit->toPlainText());
    p->setResetCommands(m_resetCommandsTextEdit->toPlainText());
    IDebugServerProviderConfigWidget::apply();
}

void GenericGdbServerProviderConfigWidget::discard()
{
    setFromProvider();
    IDebugServerProviderConfigWidget::discard();
}

void GenericGdbServerProviderConfigWidget::setFromProvider()
{
    const auto p = static_cast<GenericGdbServerProvider *>(m_provider);
    Q_ASSERT(p);

    const QSignalBlocker blocker(this);
    m_hostWidget->setChannel(p->channel());
    m_executableFileChooser->setFilePath(p->m_executableFile);
    m_additionalArgumentsLineEdit->setText(p->m_additionalArguments);
    m_useExtendedRemoteCheckBox->setChecked(p->useExtendedRemote());
    m_initCommandsTextEdit->setPlainText(p->initCommands());
    m_resetCommandsTextEdit->setPlainText(p->resetCommands());
}

} // ProjectExplorer::Internal
