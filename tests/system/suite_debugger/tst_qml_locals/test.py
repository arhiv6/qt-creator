# Copyright (C) 2016 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

source("../../shared/qtcreator.py")
source("Tree.py")

def main():
    if os.getenv("SYSTEST_OPENGL_MISSING") == "1":
        test.xfail("This test needs OpenGL - skipping...")
        return
    projName = "simpleQuickUI2.qmlproject"
    projFolder = os.path.dirname(findFile("testdata", "simpleQuickUI2/%s" % projName))
    if not neededFilePresent(os.path.join(projFolder, projName)):
        return
    qmlProjDir = prepareTemplate(projFolder)
    if qmlProjDir == None:
        test.fatal("Could not prepare test files - leaving test")
        return
    qmlProjFile = os.path.join(qmlProjDir, projName)
    # start Creator by passing a .qmlproject file
    startQC(['"%s"' % qmlProjFile])
    if not startedWithoutPluginError():
        return
    waitFor('object.exists(":Qt Creator_Utils::NavigationTreeView")', 10000)
    fancyConfButton = findObject(":*Qt Creator_Core::Internal::FancyToolButton")
    fancyRunButton = findObject(":*Qt Creator.Run_Core::Internal::FancyToolButton")
    fancyDebugButton = findObject(":*Qt Creator.Start Debugging_Core::Internal::FancyToolButton")
    exe, target = getExecutableAndTargetFromToolTip(str(waitForObject(fancyConfButton).toolTip))
    if not (test.verify(fancyRunButton.enabled and fancyDebugButton.enabled,
                        "Verifying Run and Debug are enabled (Qt5 is available).")
            and test.compare(target, Targets.getStringForTarget(Targets.getDefaultKit()),
                             "Verifying selected Target is Qt5.")
            and test.compare(exe, "QML Runtime", "Verifying selected executable is QML Runtime.")):
        earlyExit("Something went wrong opening Qml project - probably missing Qt5.")
        return
    switchViewTo(ViewConstants.PROJECTS)
    switchToBuildOrRunSettingsFor(Targets.getDefaultKit(), ProjectSettings.RUN)
    ensureChecked("{container=':Qt Creator_Core::Internal::MainWindow' text='Enable QML' "
                  "type='QCheckBox' unnamed='1' visible='1'}")
    switchViewTo(ViewConstants.EDIT)
    clickButton(fancyDebugButton)
    locAndExprTV = waitForObject(":Locals and Expressions_Debugger::Internal::WatchTreeView")
    # Locals and Expressions populates treeview only on demand - so the tree must be expanded
    __unfoldTree__()
    items = fetchItems(QModelIndex(), QModelIndex(), locAndExprTV)
    # reduce items to Locals (invisible object)
    items = items.getChild("Inspector")
    if items == None:
        earlyExit("Could not find expected Inspector tree inside Locals and Expressions.")
        return
    # reduce items to outer Rectangle object
    items = items.getChild("QQuickView")
    if items == None:
        earlyExit("Could not find expected QQuickView tree inside Locals and Expressions.")
        return
    items = items.getChild("QQuickRootItem")
    if items == None:
        earlyExit("Could not find expected QQuickRootItem tree inside Locals and Expressions.")
        return
    items = items.getChild("Rectangle")
    if items == None:
        earlyExit("Could not find expected Rectangle tree inside Locals and Expressions.")
        return
    checkForEmptyRows(items)
    check = [[None, 0, {"Properties":1, "Rectangle":2, "Text":1}, {"width":"360", "height":"360"}],
             ["Text", 1, {"Properties":1}, {"text":"Check"}],
             ["Rectangle", 2, {"Properties":1}, {"width":"50", "height":"50", "color":"#008000"}],
             ["Rectangle", 1, {"Properties":1}, {"width":"100", "height":"100", "color":"#ff0000"}]
             ]
    for current in check:
        if current[0]:
            subItem = items.getChild(current[0], current[1])
        else:
            subItem = items
        checkForExpectedValues(subItem, current[2], current[3])
    clickButton(waitForObject(':Debugger Toolbar.Exit Debugger_QToolButton', 5000))
    invokeMenuItem("File", "Exit")

def __unfoldTree__():
    # TODO inspect the qmlengine as well?
    rootIndex = getQModelIndexStr("text='QQuickView'",
                                  ':Locals and Expressions_Debugger::Internal::WatchTreeView')
    unfoldQModelIndex(rootIndex, False)
    quickRootItem = getQModelIndexStr("text='QQuickRootItem'", rootIndex)
    unfoldQModelIndex(quickRootItem, False)
    mainRect = getQModelIndexStr("text='Rectangle'", quickRootItem)
    unfoldQModelIndex(mainRect)
    subItems = ["text='Rectangle'", "text='Rectangle' occurrence='2'", "text='Text'"]
    for item in subItems:
        unfoldQModelIndex(getQModelIndexStr(item, mainRect))

def unfoldQModelIndex(indexStr, includingProperties=True):
    tv = waitForObject(':Locals and Expressions_Debugger::Internal::WatchTreeView')
    # HACK to avoid failing clicks
    tv.scrollToBottom()
    doubleClick(waitForObject(indexStr))
    if includingProperties:
        propIndex = getQModelIndexStr("text='Properties'", indexStr)
        # HACK to avoid failing clicks
        tv.scrollToBottom()
        doubleClick(waitForObject(propIndex))

def fetchItems(index, valIndex, treeView):
    tree = Tree()
    model = treeView.model()
    if index.isValid():
            name = str(model.data(index).toString())
            value = str(model.data(valIndex).toString())
            tree.setName(name)
            tree.setValue(value)
    for row in range(model.rowCount(index)):
        tree.addChild(fetchItems(model.index(row, 0, index),
                                 model.index(row, 2, index), treeView))
    return tree

def checkForEmptyRows(items, isRootCheck=True):
    # check for QTCREATORBUG-9069
    noEmptyRowsFound = True
    if items.getName().strip() == "":
        noEmptyRowsFound = False
        test.fail('Found empty row inside Locals and Expressions', '%s' % items)
    if items.childrenCount():
        for item in items.getChildren():
            noEmptyRowsFound &= checkForEmptyRows(item, False)
    if isRootCheck and noEmptyRowsFound:
        test.passes("No empty rows inside Locals and Expressions found.")
    return noEmptyRowsFound

def checkForExpectedValues(items, expectedChildren, expectedProperties):
    if items == None:
        test.fatal("Got a None object to inspect")
        return
    for subItemName in expectedChildren.keys():
        test.compare(items.countChildOccurrences(subItemName), expectedChildren[subItemName],
                     "Verify number of children named %s for %s" % (subItemName, items.getName()))
    properties = items.getChild("Properties")
    if properties:
        children = properties.getChildren()
        for property,value in expectedProperties.iteritems():
            foundProperty = getProperty(property, children)
            if foundProperty:
                test.compare(foundProperty.getValue(), value, "Verifying value for %s" % property)
            else:
                test.fail("Could not find property %s for object %s" % (property, items.getName()))
    else:
        test.fail("Missing properties for %s" % items.getName())

def getProperty(property, propertyList):
    for prop in propertyList:
        if prop.getName() == property:
            return prop
    return None
