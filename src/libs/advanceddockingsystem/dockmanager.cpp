// Copyright (C) 2020 Uwe Kindler
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-2.1-or-later OR GPL-3.0-or-later

#include "dockmanager.h"

#include "ads_globals.h"
#include "ads_globals_p.h"
#include "dockareawidget.h"
#include "dockfocuscontroller.h"
#include "dockingstatereader.h"
#include "dockoverlay.h"
#include "dockwidget.h"
#include "floatingdockcontainer.h"
#include "iconprovider.h"

#include "workspacedialog.h"

#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/qtcassert.h>
#include <utils/utilstr.h>

#include <algorithm>
#include <iostream>

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QLoggingCategory>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QVariant>
#include <QXmlStreamWriter>

Q_LOGGING_CATEGORY(adsLog, "qtc.qmldesigner.advanceddockingsystem", QtWarningMsg);

using namespace Utils;

namespace ADS
{
    /**
     * Internal file version in case the structure changes internally
     */
    enum eStateFileVersion {
        InitialVersion = 0,       //!< InitialVersion
        Version1 = 1,             //!< Version1
        CurrentVersion = Version1 //!< CurrentVersion
    };

    static DockManager::ConfigFlags g_staticConfigFlags = DockManager::DefaultNonOpaqueConfig;

    /**
     * Private data class of DockManager class (pimpl)
     */
    class DockManagerPrivate
    {
    public:
        DockManager *q;
        QList<QPointer<FloatingDockContainer>> m_floatingWidgets;
        QList<DockContainerWidget *> m_containers;
        DockOverlay *m_containerOverlay = nullptr;
        DockOverlay *m_dockAreaOverlay = nullptr;
        QMap<QString, DockWidget *> m_dockWidgetsMap;
        bool m_restoringState = false;
        QVector<FloatingDockContainer *> m_uninitializedFloatingWidgets;
        DockFocusController *m_focusController = nullptr;

        QString m_workspaceName;
        bool m_workspaceListDirty = true;
        QStringList m_workspaces;
        QSet<QString> m_workspacePresets;
        QHash<QString, QDateTime> m_workspaceDateTimes;
        QString m_workspaceToRestoreAtStartup;
        bool m_autorestoreLastWorkspace; // This option is set in the Workspace Manager!
        QSettings *m_settings = nullptr;
        QString m_workspacePresetsPath;
        bool m_modeChangeState = false;

        /**
         * Private data constructor
         */
        DockManagerPrivate(DockManager *parent);

        /**
         * Restores the state. If testing is set to true it will check if
         * the given data stream is a valid docking system state file.
         */
        bool restoreStateFromXml(const QByteArray &state,
                                 int version,
                                 bool testing = false);

        /**
         * Restore state
         */
        bool restoreState(const QByteArray &state, int version);

        void restoreDockWidgetsOpenState();
        void restoreDockAreasIndices();
        void emitTopLevelEvents();

        void hideFloatingWidgets()
        {
            // Hide updates of floating widgets from user
            for (const auto &floatingWidget : std::as_const(m_floatingWidgets)) {
                if (floatingWidget)
                    floatingWidget->hide();
            }
        }

        void markDockWidgetsDirty()
        {
            for (const auto &dockWidget : std::as_const(m_dockWidgetsMap))
                dockWidget->setProperty("dirty", true);
        }

        /**
         * Restores the container with the given index
         */
        bool restoreContainer(int index, DockingStateReader &stream, bool testing);

        void workspaceLoadingProgress();
    }; // class DockManagerPrivate

    DockManagerPrivate::DockManagerPrivate(DockManager *parent)
        : q(parent)
    {}

    bool DockManagerPrivate::restoreContainer(int index, DockingStateReader &stream, bool testing)
    {
        if (testing)
            index = 0;

        bool result = false;
        if (index >= m_containers.count()) {
            FloatingDockContainer *floatingWidget = new FloatingDockContainer(q);
            result = floatingWidget->restoreState(stream, testing);
        } else {
            qCInfo(adsLog) << "d->m_containers[i]->restoreState ";
            auto container = m_containers[index];
            if (container->isFloating())
                result = container->floatingWidget()->restoreState(stream, testing);
            else
                result = container->restoreState(stream, testing);
        }

        return result;
    }

    bool DockManagerPrivate::restoreStateFromXml(const QByteArray &state, int version, bool testing)
    {
        Q_UNUSED(version) // TODO version is not needed, why is it in here in the first place?

        if (state.isEmpty())
            return false;

        DockingStateReader stateReader(state);
        if (!stateReader.readNextStartElement())
            return false;

        if (stateReader.name() != QLatin1String("QtAdvancedDockingSystem"))
            return false;

        qCInfo(adsLog) << stateReader.attributes().value("version");
        bool ok;
        int v = stateReader.attributes().value("version").toInt(&ok);
        if (!ok || v > CurrentVersion)
            return false;

        stateReader.setFileVersion(v);

        qCInfo(adsLog) << stateReader.attributes().value("userVersion");
        // Older files do not support UserVersion but we still want to load them so
        // we first test if the attribute exists
        if (!stateReader.attributes().value("userVersion").isEmpty())
        {
            v = stateReader.attributes().value("userVersion").toInt(&ok);
            if (!ok || v != version)
                return false;
        }

        bool result = true;
#ifdef ADS_DEBUG_PRINT
        int dockContainers = stateReader.attributes().value("containers").toInt();
        qCInfo(adsLog) << dockContainers;
#endif
        int dockContainerCount = 0;
        while (stateReader.readNextStartElement()) {
            if (stateReader.name() == QLatin1String("container")) {
                result = restoreContainer(dockContainerCount, stateReader, testing);
                if (!result)
                    break;

                dockContainerCount++;
            }
        }

        if (!testing) {
            // Delete remaining empty floating widgets
            int floatingWidgetIndex = dockContainerCount - 1;
            int deleteCount = m_floatingWidgets.count() - floatingWidgetIndex;
            for (int i = 0; i < deleteCount; ++i) {
                m_floatingWidgets[floatingWidgetIndex + i]->deleteLater();
                q->removeDockContainer(m_floatingWidgets[floatingWidgetIndex + i]->dockContainer());
            }
        }

        return result;
    }

    void DockManagerPrivate::restoreDockWidgetsOpenState()
    {
        // All dock widgets, that have not been processed in the restore state
        // function are invisible to the user now and have no assigned dock area
        // They do not belong to any dock container, until the user toggles the
        // toggle view action the next time
        for (auto dockWidget : std::as_const(m_dockWidgetsMap)) {
            if (dockWidget->property(internal::dirtyProperty).toBool()) {
                dockWidget->flagAsUnassigned();
                emit dockWidget->viewToggled(false);
            } else {
                dockWidget->toggleViewInternal(
                    !dockWidget->property(internal::closedProperty).toBool());
            }
        }
    }

    void DockManagerPrivate::restoreDockAreasIndices()
    {
        // Now all dock areas are properly restored and we setup the index of
        // The dock areas because the previous toggleView() action has changed
        // the dock area index
        int count = 0;
        for (auto dockContainer : std::as_const(m_containers)) {
            count++;
            for (int i = 0; i < dockContainer->dockAreaCount(); ++i) {
                DockAreaWidget *dockArea = dockContainer->dockArea(i);
                QString dockWidgetName = dockArea->property("currentDockWidget").toString();
                DockWidget *dockWidget = nullptr;
                if (!dockWidgetName.isEmpty())
                    dockWidget = q->findDockWidget(dockWidgetName);

                if (!dockWidget || dockWidget->isClosed()) {
                    int index = dockArea->indexOfFirstOpenDockWidget();
                    if (index < 0)
                        continue;

                    dockArea->setCurrentIndex(index);
                } else {
                    dockArea->internalSetCurrentDockWidget(dockWidget);
                }
            }
        }
    }

    void DockManagerPrivate::emitTopLevelEvents()
    {
        // Finally we need to send the topLevelChanged() signals for all dock
        // widgets if top level changed
        for (auto dockContainer : std::as_const(m_containers)) {
            DockWidget *topLevelDockWidget = dockContainer->topLevelDockWidget();
            if (topLevelDockWidget) {
                topLevelDockWidget->emitTopLevelChanged(true);
            } else {
                for (int i = 0; i < dockContainer->dockAreaCount(); ++i) {
                    auto dockArea = dockContainer->dockArea(i);
                    for (auto dockWidget : dockArea->dockWidgets())
                        dockWidget->emitTopLevelChanged(false);
                }
            }
        }
    }

    bool DockManagerPrivate::restoreState(const QByteArray &state, int version)
    {
        QByteArray currentState = state.startsWith("<?xml") ? state : qUncompress(state);
        // Check the format of the given data stream
        if (!restoreStateFromXml(currentState, version, true)) {
            qCInfo(adsLog) << "checkFormat: Error checking format!!!";
            return false;
        }

        // Hide updates of floating widgets from use
        hideFloatingWidgets();
        markDockWidgetsDirty();

        if (!restoreStateFromXml(currentState, version)) {
            qCInfo(adsLog) << "restoreState: Error restoring state!!!";
            return false;
        }

        restoreDockWidgetsOpenState();
        restoreDockAreasIndices();
        emitTopLevelEvents();

        return true;
    }

    DockManager::DockManager(QWidget *parent)
        : DockContainerWidget(this, parent)
        , d(new DockManagerPrivate(this))
    {
        connect(this, &DockManager::workspaceListChanged, this, [=] {
            d->m_workspaceListDirty = true;
        });

        createRootSplitter();
        QMainWindow *mainWindow = qobject_cast<QMainWindow *>(parent);
        if (mainWindow) {
            mainWindow->setCentralWidget(this);
        }

        d->m_dockAreaOverlay = new DockOverlay(this, DockOverlay::ModeDockAreaOverlay);
        d->m_containerOverlay = new DockOverlay(this, DockOverlay::ModeContainerOverlay);
        d->m_containers.append(this);

        if (DockManager::configFlags().testFlag(DockManager::FocusHighlighting))
            d->m_focusController = new DockFocusController(this);
    }

    DockManager::~DockManager()
    {
        emit aboutToUnloadWorkspace(d->m_workspaceName);
        save();
        saveStartupWorkspace();

        // Using a temporal vector since the destructor of
        // FloatingDockWidgetContainer alters d->m_floatingWidgets.
        const auto copy = d->m_floatingWidgets;
        for (const auto &floatingWidget : copy) {
            if (floatingWidget)
                delete floatingWidget.get();
        }
        delete d;
    }

    DockManager::ConfigFlags DockManager::configFlags() { return g_staticConfigFlags; }

    void DockManager::setConfigFlags(const ConfigFlags flags) { g_staticConfigFlags = flags; }

    void DockManager::setConfigFlag(eConfigFlag flag, bool on)
    {
        internal::setFlag(g_staticConfigFlags, flag, on);
    }

    bool DockManager::testConfigFlag(eConfigFlag flag)
    {
        return configFlags().testFlag(flag);
    }

    IconProvider &DockManager::iconProvider()
    {
        static IconProvider instance;
        return instance;
    }

    int DockManager::startDragDistance()
    {
        return static_cast<int>(QApplication::startDragDistance() * 1.5);
    }

    void DockManager::setSettings(QSettings *settings) { d->m_settings = settings; }

    void DockManager::setWorkspacePresetsPath(const QString &path) { d->m_workspacePresetsPath = path; }

    DockAreaWidget *DockManager::addDockWidget(DockWidgetArea area,
                                               DockWidget *dockWidget,
                                               DockAreaWidget *dockAreaWidget)
    {
        d->m_dockWidgetsMap.insert(dockWidget->objectName(), dockWidget);
        return DockContainerWidget::addDockWidget(area, dockWidget, dockAreaWidget);
    }

    void DockManager::initialize()
    {
        syncWorkspacePresets();

        QString workspace = ADS::Constants::DEFAULT_WORKSPACE;

        // Determine workspace to restore at startup
        if (autoRestorLastWorkspace()) {
            QString lastWS = lastWorkspace();
            if (!lastWS.isEmpty() && workspaces().contains(lastWS))
                workspace = lastWS;
            else
                qDebug() << "Couldn't restore last workspace!";
        }

        openWorkspace(workspace);
    }

    DockAreaWidget *DockManager::addDockWidgetTab(DockWidgetArea area, DockWidget *dockWidget)
    {
        DockAreaWidget *areaWidget = lastAddedDockAreaWidget(area);
        if (areaWidget)
            return addDockWidget(ADS::CenterDockWidgetArea, dockWidget, areaWidget);
        else if (!openedDockAreas().isEmpty())
            return addDockWidget(area, dockWidget, openedDockAreas().constLast());
        else
            return addDockWidget(area, dockWidget, nullptr);
    }

    DockAreaWidget *DockManager::addDockWidgetTabToArea(DockWidget *dockWidget,
                                                        DockAreaWidget *dockAreaWidget)
    {
        return addDockWidget(ADS::CenterDockWidgetArea, dockWidget, dockAreaWidget);
    }

    FloatingDockContainer *DockManager::addDockWidgetFloating(DockWidget *dockWidget)
    {
        d->m_dockWidgetsMap.insert(dockWidget->objectName(), dockWidget);
        DockAreaWidget *oldDockArea = dockWidget->dockAreaWidget();
        if (oldDockArea)
            oldDockArea->removeDockWidget(dockWidget);

        dockWidget->setDockManager(this);
        FloatingDockContainer *floatingWidget = new FloatingDockContainer(dockWidget);
        floatingWidget->resize(dockWidget->size());
        if (isVisible())
            floatingWidget->show();
        else
            d->m_uninitializedFloatingWidgets.append(floatingWidget);

        return floatingWidget;
    }

    void DockManager::registerFloatingWidget(FloatingDockContainer *floatingWidget)
    {
        d->m_floatingWidgets.append(floatingWidget);
        emit floatingWidgetCreated(floatingWidget);
        qCInfo(adsLog) << "d->FloatingWidgets.count() " << d->m_floatingWidgets.count();
    }

    void DockManager::removeFloatingWidget(FloatingDockContainer *floatingWidget)
    {
        d->m_floatingWidgets.removeAll(floatingWidget);
    }

    void DockManager::registerDockContainer(DockContainerWidget *dockContainer)
    {
        d->m_containers.append(dockContainer);
    }

    void DockManager::removeDockContainer(DockContainerWidget *dockContainer)
    {
        if (this != dockContainer)
            d->m_containers.removeAll(dockContainer);
    }

    DockOverlay *DockManager::containerOverlay() const { return d->m_containerOverlay; }

    DockOverlay *DockManager::dockAreaOverlay() const { return d->m_dockAreaOverlay; }

    const QList<DockContainerWidget *> DockManager::dockContainers() const
    {
        return d->m_containers;
    }

    const QList<QPointer<FloatingDockContainer>> DockManager::floatingWidgets() const
    {
        return d->m_floatingWidgets;
    }

    unsigned int DockManager::zOrderIndex() const { return 0; }

    QByteArray DockManager::saveState(int version) const
    {
        QByteArray xmlData;
        QXmlStreamWriter stream(&xmlData);
        auto configFlags = DockManager::configFlags();
        stream.setAutoFormatting(configFlags.testFlag(XmlAutoFormattingEnabled));
        stream.writeStartDocument();
        stream.writeStartElement("QtAdvancedDockingSystem");
        stream.writeAttribute("version", QString::number(CurrentVersion));
        stream.writeAttribute("userVersion", QString::number(version));
        stream.writeAttribute("containers", QString::number(d->m_containers.count()));
        for (auto container : std::as_const(d->m_containers))
            container->saveState(stream);

        stream.writeEndElement();
        stream.writeEndDocument();
        return xmlData;
    }

    bool DockManager::restoreState(const QByteArray &state, int version)
    {
        // Prevent multiple calls as long as state is not restore. This may
        // happen, if QApplication::processEvents() is called somewhere
        if (d->m_restoringState)
            return false;

        // We hide the complete dock manager here. Restoring the state means
        // that DockWidgets are removed from the DockArea internal stack layout
        // which in turn  means, that each time a widget is removed the stack
        // will show and raise the next available widget which in turn
        // triggers show events for the dock widgets. To avoid this we hide the
        // dock manager. Because there will be no processing of application
        // events until this function is finished, the user will not see this
        // hiding
        bool isHidden = this->isHidden();
        if (!isHidden)
            hide();

        d->m_restoringState = true;
        emit restoringState();
        bool result = d->restoreState(state, version);
        d->m_restoringState = false;
        if (!isHidden)
            show();

        emit stateRestored();
        return result;
    }

    void DockManager::showEvent(QShowEvent *event)
    {
        Super::showEvent(event);
        if (d->m_uninitializedFloatingWidgets.empty())
            return;

        for (auto floatingWidget : std::as_const(d->m_uninitializedFloatingWidgets))
            floatingWidget->show();

        d->m_uninitializedFloatingWidgets.clear();
    }

    DockWidget *DockManager::findDockWidget(const QString &objectName) const
    {
        return d->m_dockWidgetsMap.value(objectName, nullptr);
    }

    void DockManager::removeDockWidget(DockWidget *dockWidget)
    {
        emit dockWidgetAboutToBeRemoved(dockWidget);
        d->m_dockWidgetsMap.remove(dockWidget->objectName());
        DockContainerWidget::removeDockWidget(dockWidget);
        emit dockWidgetRemoved(dockWidget);
    }

    QMap<QString, DockWidget *> DockManager::dockWidgetsMap() const { return d->m_dockWidgetsMap; }

    bool DockManager::isRestoringState() const { return d->m_restoringState; }

    void DockManager::showWorkspaceMananger()
    {
        save(); // Save current workspace

        WorkspaceDialog workspaceDialog(this, parentWidget());
        workspaceDialog.setAutoLoadWorkspace(autoRestorLastWorkspace());
        workspaceDialog.exec();

        QTC_ASSERT(d->m_settings, return );
        d->m_settings->setValue(Constants::AUTO_RESTORE_WORKSPACE_SETTINGS_KEY,
                                workspaceDialog.autoLoadWorkspace());
    }

    bool DockManager::isWorkspacePreset(const QString &workspace) const
    {
        return d->m_workspacePresets.contains(workspace);
    }

    bool DockManager::save()
    {
        if (isModeChangeState())
            return false;

        emit aboutToSaveWorkspace();

        bool result = write(activeWorkspace(), saveState(), parentWidget());
        if (result)
            d->m_workspaceDateTimes.insert(activeWorkspace(), QDateTime::currentDateTime());
        else
            QMessageBox::warning(parentWidget(),
                                 Tr::tr("Cannot Save Workspace"),
                                 Tr::tr("Could not save workspace to file %1")
                                     .arg(workspaceNameToFilePath(d->m_workspaceName)
                                              .toUserOutput()));

        return result;
    }

    QString DockManager::activeWorkspace() const { return d->m_workspaceName; }

    QString DockManager::lastWorkspace() const
    {
        QTC_ASSERT(d->m_settings, return {});
        return d->m_settings->value(Constants::STARTUP_WORKSPACE_SETTINGS_KEY).toString();
    }

    bool DockManager::autoRestorLastWorkspace() const
    {
        QTC_ASSERT(d->m_settings, return false);
        return d->m_settings->value(Constants::AUTO_RESTORE_WORKSPACE_SETTINGS_KEY).toBool();
    }

    const QString m_dirName = QLatin1String("workspaces");
    const QString m_fileExt = QLatin1String(".wrk"); // TODO

    QString DockManager::workspaceFileExtension() const { return m_fileExt; }

    QStringList DockManager::workspaces()
    {
        if (d->m_workspaces.isEmpty() || d->m_workspaceListDirty) {
            auto tmp = Utils::toSet(d->m_workspaces);

            QTC_ASSERT(d->m_settings, return {});
            QDir workspaceDir(QFileInfo(d->m_settings->fileName()).path() + QLatin1Char('/')
                              + m_dirName);
            QFileInfoList workspaceFiles
                = workspaceDir.entryInfoList(QStringList() << QLatin1Char('*') + m_fileExt,
                                             QDir::NoFilter,
                                             QDir::Time);
            for (const QFileInfo &fileInfo : workspaceFiles) {
                QString workspaceName = fileNameToWorkspaceName(fileInfo.completeBaseName());
                d->m_workspaceDateTimes.insert(workspaceName, fileInfo.lastModified());
                tmp.insert(workspaceName);
            }

            d->m_workspaceListDirty = false;
            d->m_workspaces = Utils::toList(tmp);
        }
        return d->m_workspaces;
    }

    QSet<QString> DockManager::workspacePresets() const
    {
        if (d->m_workspacePresets.isEmpty()) {
            QDir workspacePresetsDir(d->m_workspacePresetsPath);
            QFileInfoList workspacePresetsFiles
                = workspacePresetsDir.entryInfoList(QStringList() << QLatin1Char('*') + m_fileExt,
                                                    QDir::NoFilter,
                                                    QDir::Time);
            for (const QFileInfo &fileInfo : workspacePresetsFiles)
                d->m_workspacePresets.insert(fileNameToWorkspaceName(fileInfo.completeBaseName()));
        }
        return d->m_workspacePresets;
    }

    QDateTime DockManager::workspaceDateTime(const QString &workspace) const
    {
        return d->m_workspaceDateTimes.value(workspace);
    }

    FilePath DockManager::workspaceNameToFilePath(const QString &workspaceName) const
    {
        QTC_ASSERT(d->m_settings, return {});
        return FilePath::fromString(QFileInfo(d->m_settings->fileName()).path() + QLatin1Char('/')
                                    + m_dirName + QLatin1Char('/')
                                    + workspaceNameToFileName(workspaceName));
    }

    QString DockManager::fileNameToWorkspaceName(const QString &fileName) const
    {
        QString copy = QFileInfo(fileName).baseName();
        copy.replace("_", " ");
        return copy;
    }

    QString DockManager::workspaceNameToFileName(const QString &workspaceName) const
    {
        QString copy = workspaceName;
        copy.replace(" ", "_");
        copy.append(m_fileExt);
        return copy;
    }

    /**
     * Creates \a workspace, but does not actually create the file.
     */
    bool DockManager::createWorkspace(const QString &workspace)
    {
        if (workspaces().contains(workspace))
            return false;

        bool result = write(workspace, saveState(), parentWidget());
        if (result) {
            d->m_workspaces.insert(1, workspace);
            d->m_workspaceDateTimes.insert(workspace, QDateTime::currentDateTime());
            emit workspaceListChanged();
        } else {
            QMessageBox::warning(parentWidget(),
                                 Tr::tr("Cannot Save Workspace"),
                                 Tr::tr("Could not save workspace to file %1")
                                     .arg(workspaceNameToFilePath(d->m_workspaceName)
                                              .toUserOutput()));
        }

        return result;
    }

    bool DockManager::openWorkspace(const QString &workspace)
    {
        // Do nothing if we have that workspace already loaded, exception if it is
        // a preset workspace. In this case we still want to be able to load the
        // default workspace to undo potential user changes.
        if (workspace == d->m_workspaceName && !isWorkspacePreset(workspace))
            return true;

        if (!workspaces().contains(workspace))
            return false;

        // Check if the currently active workspace isn't empty and try to save it
        if (!d->m_workspaceName.isEmpty()) {
            // Allow everyone to set something in the workspace and before saving
            emit aboutToUnloadWorkspace(d->m_workspaceName);
            if (!save())
                return false;
        }

        // Try loading the file
        QByteArray data = loadWorkspace(workspace);
        if (data.isEmpty())
            return false;

        emit openingWorkspace(workspace);
        // If data was loaded from file try to restore its state
        if (!data.isNull() && !restoreState(data))
            return false;

        d->m_workspaceName = workspace;
        emit workspaceLoaded(workspace);

        return true;
    }

    bool DockManager::reloadActiveWorkspace()
    {
        if (!workspaces().contains(activeWorkspace()))
            return false;

        // Try loading the file
        QByteArray data = loadWorkspace(activeWorkspace());
        if (data.isEmpty())
            return false;

        // If data was loaded from file try to restore its state
        if (!data.isNull() && !restoreState(data))
            return false;

        emit workspaceReloaded(activeWorkspace());

        return true;
    }

    /**
     * \brief Shows a dialog asking the user to confirm deleting the workspace \p workspace
     */
    bool DockManager::confirmWorkspaceDelete(const QStringList &workspace)
    {
        const QString title = workspace.size() == 1 ? Tr::tr("Delete Workspace")
                                                    : Tr::tr("Delete Workspaces");
        const QString question = workspace.size() == 1
                                     ? Tr::tr("Delete workspace %1?").arg(workspace.first())
                                     : Tr::tr("Delete these workspaces?\n    %1")
                                           .arg(workspace.join("\n    "));
        return QMessageBox::question(parentWidget(),
                                     title,
                                     question,
                                     QMessageBox::Yes | QMessageBox::No)
               == QMessageBox::Yes;
    }

    /**
     * Deletes \a workspace name from workspace list and the file from disk.
     */
    bool DockManager::deleteWorkspace(const QString &workspace)
    {
        // Remove workspace from internal list
        if (!d->m_workspaces.contains(workspace))
            return false;

        // Remove corresponding workspace file
        const FilePath file = workspaceNameToFilePath(workspace);
        if (file.exists()) {
            if (file.removeFile()) {
                d->m_workspaces.removeOne(workspace);
                emit workspacesRemoved();
                emit workspaceListChanged();
                return true;
            }
        }

        return false;
    }

    void DockManager::deleteWorkspaces(const QStringList &workspaces)
    {
        for (const QString &workspace : workspaces)
            deleteWorkspace(workspace);
    }

    bool DockManager::cloneWorkspace(const QString &original, const QString &clone)
    {
        if (!d->m_workspaces.contains(original))
            return false;

        const FilePath originalPath = workspaceNameToFilePath(original);
        const FilePath clonePath = workspaceNameToFilePath(clone);

        // If the file does not exist, we can still clone
        if (!originalPath.exists() || originalPath.copyFile(clonePath)) {
            d->m_workspaces.insert(1, clone);
            d->m_workspaceDateTimes.insert(clone, clonePath.lastModified());
            emit workspaceListChanged();
            return true;
        }
        return false;
    }

    bool DockManager::renameWorkspace(const QString &original, const QString &newName)
    {
        if (!cloneWorkspace(original, newName))
            return false;

        if (original == activeWorkspace())
            openWorkspace(newName);

        return deleteWorkspace(original);
    }

    bool DockManager::resetWorkspacePreset(const QString &workspace)
    {
        if (!isWorkspacePreset(workspace))
            return false;

        const FilePath fileName = workspaceNameToFilePath(workspace);

        if (!fileName.removeFile())
            return false;

        QDir presetsDir(d->m_workspacePresetsPath);
        bool result = QFile::copy(presetsDir.filePath(workspaceNameToFileName(workspace)),
                                  fileName.toFSPathString());
        if (result)
            d->m_workspaceDateTimes.insert(workspace, QDateTime::currentDateTime());

        return result;
    }

    void DockManager::setModeChangeState(bool value)
    {
        d->m_modeChangeState = value;
    }

    bool DockManager::isModeChangeState() const
    {
        return d->m_modeChangeState;
    }

    void DockManager::importWorkspace(const QString &workspace)
    {
        // Extract workspace name
        QString workspaceName = fileNameToWorkspaceName(workspace);

        // Check if the workspace is already contained in the list of workspaces. If that is the case
        // add a counter to the workspace name.
        if (workspaces().contains(workspaceName)) {
            int i = 2;
            QString copy;
            do {
                copy = workspaceName + QLatin1String(" (") + QString::number(i) + QLatin1Char(')');
                ++i;
            } while (workspaces().contains(copy));
            workspaceName = copy;
        }

        QString fileName = workspaceNameToFileName(workspaceName);
        QFile file(workspace);
        if (!file.exists()) {
            qCInfo(adsLog) << QString("File doesn't exist '%1'").arg(workspace);
            return;
        }

        QDir workspaceDir(QFileInfo(d->m_settings->fileName()).path() + QLatin1Char('/') + m_dirName);

        if (!file.copy(workspaceDir.filePath(fileName))) {
            qCInfo(adsLog) << QString("Could not copy '%1' to '%2' error: %3").arg(
                workspace, workspaceDir.filePath(fileName), file.errorString());
        } else {
            d->m_workspaces.insert(1, workspaceName);
            d->m_workspaceDateTimes.insert(workspaceName,
                                           workspaceNameToFilePath(workspaceName).lastModified());
            d->m_workspaceListDirty = true;
            // After importing the workspace, update the workspace list
            workspaces();
            emit workspaceListChanged();
        }
    }

    void DockManager::exportWorkspace(const QString &target, const QString &workspace)
    {
        // If we came this far the user decided that in case the target already exists to overwrite it.
        // We first need to remove the existing file, otherwise QFile::copy() will fail.
        const FilePath targetFile = FilePath::fromUserInput(target);

        // Remove the file which supposed to be overwritten
        if (targetFile.exists()) {
            if (!targetFile.removeFile()) {
                qCInfo(adsLog) << QString("Couldn't remove '%1'").arg(targetFile.toUserOutput());
                return;
            }
        }

        // Check if the target directory exists
        if (!targetFile.parentDir().exists()) {
            qCInfo(adsLog) << QString("Directory doesn't exist '%1'")
                                  .arg(targetFile.parentDir().toUserOutput());
            return;
        }

        // Check if the workspace exists
        FilePath workspaceFile = workspaceNameToFilePath(workspace);
        if (!workspaceFile.exists()) {
            qCInfo(adsLog) << QString("Workspace doesn't exist '%1'")
                                  .arg(workspaceFile.toUserOutput());
            return;
        }

        // Finally copy the workspace to the target
        const expected_str<void> copyResult = workspaceFile.copyFile(targetFile);
        if (!copyResult) {
            qCInfo(adsLog) << QString("Could not copy '%1' to '%2' error: %3")
                                  .arg(workspace, workspaceFile.toUserOutput(), copyResult.error());
        }
    }

    bool DockManager::write(const QString &workspace, const QByteArray &data, QString *errorString) const
    {
        const FilePath fileName = workspaceNameToFilePath(workspace);

        QDir tmp;
        tmp.mkpath(fileName.toFileInfo().path());
        FileSaver fileSaver(fileName, QIODevice::Text);
        if (!fileSaver.hasError())
            fileSaver.write(data);

        bool ok = fileSaver.finalize();

        if (!ok && errorString)
            *errorString = fileSaver.errorString();

        return ok;
    }

    bool DockManager::write(const QString &workspace, const QByteArray &data, QWidget *parent) const
    {
        QString errorString;
        const bool success = write(workspace, data, &errorString);
        if (!success)
            QMessageBox::critical(parent, ::Utils::Tr::tr("File Error"), errorString);
        return success;
    }

    QByteArray DockManager::loadWorkspace(const QString &workspace) const
    {
        const FilePath fileName = workspaceNameToFilePath(workspace);
        if (fileName.exists()) {
            const expected_str<QByteArray> data = fileName.fileContents();

            if (!data) {
                QMessageBox::warning(parentWidget(),
                                     Tr::tr("Cannot Restore Workspace"),
                                     Tr::tr("Could not restore workspace %1")
                                         .arg(fileName.toUserOutput()));

                qCWarning(adsLog) << QString("Could not restore workspace %1: %2")
                                         .arg(fileName.toUserOutput())
                                         .arg(data.error());

                return {};
            }
            return data.value();
        }
        return {};
    }

    void DockManager::syncWorkspacePresets()
    {
        // Get a list of all workspace presets
        QSet<QString> presets = workspacePresets();

        // Get a list of all available workspaces
        QSet<QString> availableWorkspaces = Utils::toSet(workspaces());
        presets.subtract(availableWorkspaces);

        // Copy all missing workspace presets over to the local workspace folder
        QDir presetsDir(d->m_workspacePresetsPath);
        QDir workspaceDir(QFileInfo(d->m_settings->fileName()).path() + QLatin1Char('/') + m_dirName);
        // Try do create the 'workspaces' directory if it doesn't exist already
        workspaceDir.mkpath(workspaceDir.absolutePath());
        if (!workspaceDir.exists()) {
            qCInfo(adsLog) << QString("Could not make directory '%1')").arg(workspaceDir.absolutePath());
            return;
        }

        for (const auto &preset : presets) {
            QString fileName = workspaceNameToFileName(preset);
            QString filePath = presetsDir.filePath(fileName);
            QFile file(filePath);

            if (file.exists()) {
                if (!file.copy(workspaceDir.filePath(fileName)))
                    qCInfo(adsLog) << QString("Could not copy '%1' to '%2' error: %3").arg(
                        filePath, workspaceDir.filePath(fileName), file.errorString());

                d->m_workspaceListDirty = true;
            }
        }

        // After copying over missing workspace presets, update the workspace list
        workspaces();
    }

    void DockManager::saveStartupWorkspace()
    {
        QTC_ASSERT(d->m_settings, return );
        d->m_settings->setValue(Constants::STARTUP_WORKSPACE_SETTINGS_KEY, activeWorkspace());
    }

    void DockManager::notifyWidgetOrAreaRelocation(QWidget *droppedWidget)
    {
        if (d->m_focusController)
            d->m_focusController->notifyWidgetOrAreaRelocation(droppedWidget);
    }

    void DockManager::notifyFloatingWidgetDrop(FloatingDockContainer *floatingWidget)
    {
        if (d->m_focusController)
            d->m_focusController->notifyFloatingWidgetDrop(floatingWidget);
    }

    void DockManager::setDockWidgetFocused(DockWidget *dockWidget)
    {
        if (d->m_focusController)
            d->m_focusController->setDockWidgetFocused(dockWidget);
    }

} // namespace ADS
