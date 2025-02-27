# Copyright (C) 2016 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

# Helper class to create a tree structure
class Tree:
    def __init__(self, name=None, value=None, children=None):
        self.__name__ = name
        self.__value__ = value
        self.__children__ = children

    # getter functions
    def getChild(self, name, occurrence=1):
        if self.__children__:
            for ch in self.__children__:
                if ch.__name__ == name:
                    if occurrence == 1:
                        return ch
                    occurrence -= 1
        return None

    def getChildren(self):
        return self.__children__

    def getName(self):
        return self.__name__

    def getValue(self):
        return self.__value__

    # setter function
    def setChildren(self, children):
        self.__children__ = children

    def setName(self, name):
        self.__name__ = name

    def setValue(self, value):
        self.__value__ = value

    # other functions
    def addChild(self, child):
        if self.__children__ == None:
            self.__children__ = []
        self.__children__.append(child)

    def childrenCount(self):
        if self.__children__:
            return len(self.__children__)
        return 0

    def countChildOccurrences(self, name):
        if not self.__children__:
            return 0
        return map(lambda x: x.getName(), self.__children__).count(name)

    # internal functions
    def __repr__(self):
        return self.__str__()

    def __strIndented__(self, indent):
        result = "%s%s (%s)" % (" " * indent, self.__name__, self.__value__)
        if self.__children__:
            for ch in self.__children__:
                if isinstance(ch, Tree):
                    result += os.linesep + ch.__strIndented__(indent + 2)
                else:
                    result += os.linesep + " " * (indent + 2) + str(ch)
        return result

    def __str__(self):
        return self.__strIndented__(1)
