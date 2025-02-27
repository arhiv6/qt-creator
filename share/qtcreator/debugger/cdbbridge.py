# Copyright (C) 2016 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

import inspect
import os
import sys
import cdbext
import re
import threading
from utils import TypeCode

sys.path.insert(1, os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe()))))

from dumper import DumperBase, SubItem


class FakeVoidType(cdbext.Type):
    def __init__(self, name, dumper):
        cdbext.Type.__init__(self)
        self.typeName = name.strip()
        self.dumper = dumper

    def name(self):
        return self.typeName

    def bitsize(self):
        return self.dumper.ptrSize() * 8

    def code(self):
        if self.typeName.endswith('*'):
            return TypeCode.Pointer
        if self.typeName.endswith(']'):
            return TypeCode.Array
        return TypeCode.Void

    def unqualified(self):
        return self

    def target(self):
        code = self.code()
        if code == TypeCode.Pointer:
            return FakeVoidType(self.typeName[:-1], self.dumper)
        if code == TypeCode.Void:
            return self
        try:
            return FakeVoidType(self.typeName[:self.typeName.rindex('[')], self.dumper)
        except:
            return FakeVoidType('void', self.dumper)

    def targetName(self):
        return self.target().name()

    def arrayElements(self):
        try:
            return int(self.typeName[self.typeName.rindex('[') + 1:self.typeName.rindex(']')])
        except:
            return 0

    def stripTypedef(self):
        return self

    def fields(self):
        return []

    def templateArgument(self, pos, numeric):
        return None

    def templateArguments(self):
        return []


class Dumper(DumperBase):
    def __init__(self):
        DumperBase.__init__(self)
        self.outputLock = threading.Lock()
        self.isCdb = True

    def enumValue(self, nativeValue):
        val = nativeValue.nativeDebuggerValue()
        # remove '0n' decimal prefix of the native cdb value output
        return val.replace('(0n', '(')

    def fromNativeValue(self, nativeValue):
        self.check(isinstance(nativeValue, cdbext.Value))
        val = self.Value(self)
        val.name = nativeValue.name()
        val._type = self.fromNativeType(nativeValue.type())
        # There is no cdb api for the size of bitfields.
        # Workaround this issue by parsing the native debugger text for integral types.
        if val._type.code == TypeCode.Integral:
            try:
                integerString = nativeValue.nativeDebuggerValue()
            except UnicodeDecodeError:
                integerString = ''  # cannot decode - read raw
            if integerString == 'true':
                val.ldata = int(1).to_bytes(1, byteorder='little')
            elif integerString == 'false':
                val.ldata = int(0).to_bytes(1, byteorder='little')
            else:
                integerString = integerString.replace('`', '')
                integerString = integerString.split(' ')[0]
                if integerString.startswith('0n'):
                    integerString = integerString[2:]
                    base = 10
                elif integerString.startswith('0x'):
                    base = 16
                else:
                    base = 10
                signed = not val._type.name.startswith('unsigned')
                try:
                    val.ldata = int(integerString, base).to_bytes(val._type.size(),
                                                                  byteorder='little', signed=signed)
                except:
                    # read raw memory in case the integerString can not be interpreted
                    pass
        if val._type.code == TypeCode.Enum:
            val.ldisplay = self.enumValue(nativeValue)
        val.isBaseClass = val.name == val._type.name
        val.nativeValue = nativeValue
        val.laddress = nativeValue.address()
        return val

    def nativeTypeId(self, nativeType):
        self.check(isinstance(nativeType, cdbext.Type))
        name = nativeType.name()
        if name is None or len(name) == 0:
            c = '0'
        elif name == 'struct {...}':
            c = 's'
        elif name == 'union {...}':
            c = 'u'
        else:
            return name
        typeId = c + ''.join(['{%s:%s}' % (f.name(), self.nativeTypeId(f.type()))
                              for f in nativeType.fields()])
        return typeId

    def fromNativeType(self, nativeType):
        self.check(isinstance(nativeType, cdbext.Type))
        typeId = self.nativeTypeId(nativeType)
        if self.typeData.get(typeId, None) is not None:
            return self.Type(self, typeId)

        if nativeType.name().startswith('void'):
            nativeType = FakeVoidType(nativeType.name(), self)

        code = nativeType.code()
        if code == TypeCode.Pointer:
            if not nativeType.name().startswith('<function>'):
                targetType = self.lookupType(nativeType.targetName(), nativeType.moduleId())
                if targetType is not None:
                    return self.createPointerType(targetType)
            code = TypeCode.Function

        if code == TypeCode.Array:
            # cdb reports virtual function tables as arrays those ar handled separetly by
            # the DumperBase. Declare those types as structs prevents a lookup to a
            # none existing type
            if not nativeType.name().startswith('__fptr()') and not nativeType.name().startswith('<gentype '):
                targetType = self.lookupType(nativeType.targetName(), nativeType.moduleId())
                if targetType is not None:
                    return self.createArrayType(targetType, nativeType.arrayElements())
            code = TypeCode.Struct

        tdata = self.TypeData(self, typeId)
        tdata.name = nativeType.name()
        tdata.lbitsize = nativeType.bitsize()
        tdata.code = code
        tdata.moduleName = nativeType.module()
        if code == TypeCode.Struct:
            tdata.lfields = lambda value: \
                self.listFields(nativeType, value)
            tdata.lalignment = lambda: \
                self.nativeStructAlignment(nativeType)
        if code == TypeCode.Enum:
            tdata.enumDisplay = lambda intval, addr, form: \
                self.nativeTypeEnumDisplay(nativeType, intval, form)
        tdata.templateArguments = lambda: \
            self.listTemplateParameters(nativeType.name())
        return self.Type(self, typeId)

    def listFields(self, nativeType, value):
        if value.address() is None or value.address() == 0:
            raise Exception("")
        nativeValue = value.nativeValue
        if nativeValue is None:
            nativeValue = cdbext.createValue(value.address(), nativeType)
        index = 0
        nativeMember = nativeValue.childFromIndex(index)
        while nativeMember is not None:
            yield self.fromNativeValue(nativeMember)
            index += 1
            nativeMember = nativeValue.childFromIndex(index)

    def nativeStructAlignment(self, nativeType):
        #DumperBase.warn("NATIVE ALIGN FOR %s" % nativeType.name)
        def handleItem(nativeFieldType, align):
            a = self.fromNativeType(nativeFieldType).alignment()
            return a if a > align else align
        align = 1
        for f in nativeType.fields():
            align = handleItem(f.type(), align)
        return align

    def nativeTypeEnumDisplay(self, nativeType, intval, form):
        value = self.nativeParseAndEvaluate('(%s)%d' % (nativeType.name(), intval))
        if value is None:
            return ''
        return self.enumValue(value)

    def enumExpression(self, enumType, enumValue):
        ns = self.qtNamespace()
        return ns + "Qt::" + enumType + "(" \
            + ns + "Qt::" + enumType + "::" + enumValue + ")"

    def pokeValue(self, typeName, *args):
        return None

    def parseAndEvaluate(self, exp):
        return self.fromNativeValue(self.nativeParseAndEvaluate(exp))

    def nativeParseAndEvaluate(self, exp):
        return cdbext.parseAndEvaluate(exp)

    def isWindowsTarget(self):
        return True

    def isQnxTarget(self):
        return False

    def isArmArchitecture(self):
        return False

    def isMsvcTarget(self):
        return True

    def qtCoreModuleName(self):
        modules = cdbext.listOfModules()
        # first check for an exact module name match
        for coreName in ['Qt6Core', 'Qt6Cored', 'Qt5Cored', 'Qt5Core', 'QtCored4', 'QtCore4']:
            if coreName in modules:
                self.qtCoreModuleName = lambda: coreName
                return coreName
        # maybe we have a libinfix build.
        for pattern in ['Qt6Core.*', 'Qt5Core.*', 'QtCore.*']:
            matches = [module for module in modules if re.match(pattern, module)]
            if matches:
                coreName = matches[0]
                self.qtCoreModuleName = lambda: coreName
                return coreName
        return None

    def qtDeclarativeModuleName(self):
        modules = cdbext.listOfModules()
        for declarativeModuleName in ['Qt6Qmld', 'Qt6Qml', 'Qt5Qmld', 'Qt5Qml']:
            if declarativeModuleName in modules:
                self.qtDeclarativeModuleName = lambda: declarativeModuleName
                return declarativeModuleName
        matches = [module for module in modules if re.match('Qt[56]Qml.*', module)]
        if matches:
            declarativeModuleName = matches[0]
            self.qtDeclarativeModuleName = lambda: declarativeModuleName
            return declarativeModuleName
        return None

    def qtHookDataSymbolName(self):
        hookSymbolName = 'qtHookData'
        coreModuleName = self.qtCoreModuleName()
        if coreModuleName is not None:
            hookSymbolName = '%s!%s%s' % (coreModuleName, self.qtNamespace(), hookSymbolName)
        else:
            resolved = cdbext.resolveSymbol('*' + hookSymbolName)
            if resolved:
                hookSymbolName = resolved[0]
            else:
                hookSymbolName = '*%s' % hookSymbolName
        self.qtHookDataSymbolName = lambda: hookSymbolName
        return hookSymbolName

    def qtDeclarativeHookDataSymbolName(self):
        hookSymbolName = 'qtDeclarativeHookData'
        declarativeModuleName = self.qtDeclarativeModuleName()
        if declarativeModuleName is not None:
            hookSymbolName = '%s!%s%s' % (declarativeModuleName, self.qtNamespace(), hookSymbolName)
        else:
            resolved = cdbext.resolveSymbol('*' + hookSymbolName)
            if resolved:
                hookSymbolName = resolved[0]
            else:
                hookSymbolName = '*%s' % hookSymbolName

        self.qtDeclarativeHookDataSymbolName = lambda: hookSymbolName
        return hookSymbolName

    def qtNamespace(self):
        namespace = ''
        qstrdupSymbolName = '*qstrdup'
        coreModuleName = self.qtCoreModuleName()
        if coreModuleName is not None:
            qstrdupSymbolName = '%s!%s' % (coreModuleName, qstrdupSymbolName)
        resolved = cdbext.resolveSymbol(qstrdupSymbolName)
        if resolved:
            name = resolved[0].split('!')[1]
            namespaceIndex = name.find('::')
            if namespaceIndex > 0:
                namespace = name[:namespaceIndex + 2]
        self.qtNamespace = lambda: namespace
        self.qtCustomEventFunc = self.parseAndEvaluate(
            '%s!%sQObject::customEvent' %
            (self.qtCoreModuleName(), namespace)).address()
        return namespace

    def qtVersion(self):
        qtVersion = None
        try:
            qtVersion = self.parseAndEvaluate(
                '((void**)&%s)[2]' % self.qtHookDataSymbolName()).integer()
        except:
            if self.qtCoreModuleName() is not None:
                try:
                    versionValue = cdbext.call(self.qtCoreModuleName() + '!qVersion()')
                    version = self.extractCString(self.fromNativeValue(versionValue).address())
                    (major, minor, patch) = version.decode('latin1').split('.')
                    qtVersion = 0x10000 * int(major) + 0x100 * int(minor) + int(patch)
                except:
                    pass
        if qtVersion is None:
            qtVersion = self.fallbackQtVersion
        self.qtVersion = lambda: qtVersion
        return qtVersion

    def putVtableItem(self, address):
        funcName = cdbext.getNameByAddress(address)
        if funcName is None:
            self.putItem(self.createPointerValue(address, 'void'))
        else:
            self.putValue(funcName)
            self.putType('void*')
            self.putAddress(address)

    def putVTableChildren(self, item, itemCount):
        p = item.address()
        for i in range(itemCount):
            deref = self.extractPointer(p)
            if deref == 0:
                n = i
                break
            with SubItem(self, i):
                self.putVtableItem(deref)
                p += self.ptrSize()
        return itemCount

    def ptrSize(self):
        size = cdbext.pointerSize()
        self.ptrSize = lambda: size
        return size

    def stripQintTypedefs(self, typeName):
        if typeName.startswith('qint'):
            prefix = ''
            size = typeName[4:]
        elif typeName.startswith('quint'):
            prefix = 'unsigned '
            size = typeName[5:]
        else:
            return typeName
        if size == '8':
            return '%schar' % prefix
        elif size == '16':
            return '%sshort' % prefix
        elif size == '32':
            return '%sint' % prefix
        elif size == '64':
            return '%sint64' % prefix
        else:
            return typeName

    def lookupType(self, typeNameIn, module=0):
        if len(typeNameIn) == 0:
            return None
        typeName = self.stripQintTypedefs(typeNameIn)
        if self.typeData.get(typeName, None) is None:
            nativeType = self.lookupNativeType(typeName, module)
            if nativeType is None:
                return None
            _type = self.fromNativeType(nativeType)
            if _type.typeId != typeName:
                self.registerTypeAlias(_type.typeId, typeName)
            return _type
        return self.Type(self, typeName)

    def lookupNativeType(self, name, module=0):
        if name.startswith('void'):
            return FakeVoidType(name, self)
        return cdbext.lookupType(name, module)

    def reportResult(self, result, args):
        cdbext.reportResult('result={%s}' % result)

    def readRawMemory(self, address, size):
        mem = cdbext.readRawMemory(address, size)
        if len(mem) != size:
            raise Exception("Invalid memory request: %d bytes from 0x%x" % (size, address))
        return mem

    def findStaticMetaObject(self, type):
        typeName = type.name
        if type.moduleName is not None:
            typeName = type.moduleName + '!' + typeName
        ptr = cdbext.getAddressByName(typeName + '::staticMetaObject')
        return ptr

    def warn(self, msg):
        self.put('{name="%s",value="",type="",numchild="0"},' % msg)

    def fetchVariables(self, args):
        self.resetStats()
        (ok, res) = self.tryFetchInterpreterVariables(args)
        if ok:
            self.reportResult(res, args)
            return

        self.setVariableFetchingOptions(args)

        self.output = []

        self.currentIName = 'local'
        self.put('data=[')
        self.anonNumber = 0

        variables = []
        for val in cdbext.listOfLocals(self.partialVariable):
            dumperVal = self.fromNativeValue(val)
            dumperVal.lIsInScope = dumperVal.name not in self.uninitialized
            variables.append(dumperVal)

        self.handleLocals(variables)
        self.handleWatches(args)

        self.put('],partial="%d"' % (len(self.partialVariable) > 0))
        self.put(',timings=%s' % self.timings)

        if self.forceQtNamespace:
            self.qtNamespaceToReport = self.qtNamespace()

        if self.qtNamespaceToReport:
            self.put(',qtnamespace="%s"' % self.qtNamespaceToReport)
            self.qtNamespaceToReport = None

        self.reportResult(''.join(self.output), args)
        self.output = []

    def report(self, stuff):
        sys.stdout.write(stuff + "\n")

    def findValueByExpression(self, exp):
        return cdbext.parseAndEvaluate(exp)

    def nativeDynamicTypeName(self, address, baseType):
        return None  # Does not work with cdb

    def nativeValueDereferenceReference(self, value):
        return self.nativeValueDereferencePointer(value)

    def nativeValueDereferencePointer(self, value):
        def nativeVtCastValue(nativeValue):
            # If we have a pointer to a derived instance of the pointer type cdb adds a
            # synthetic '__vtcast_<derived type name>' member as the first child
            if nativeValue.hasChildren():
                vtcastCandidate = nativeValue.childFromIndex(0)
                vtcastCandidateName = vtcastCandidate.name()
                if vtcastCandidateName.startswith('__vtcast_'):
                    # found a __vtcast member
                    # make sure that it is not an actual field
                    for field in nativeValue.type().fields():
                        if field.name() == vtcastCandidateName:
                            return None
                    return vtcastCandidate
            return None

        nativeValue = value.nativeValue
        if nativeValue is None:
            if not self.isExpanded():
                raise Exception("Casting not expanded values is to expensive")
            nativeValue = self.nativeParseAndEvaluate('(%s)0x%x' % (value.type.name, value.pointer()))
        castVal = nativeVtCastValue(nativeValue)
        if castVal is not None:
            val = self.fromNativeValue(castVal)
        else:
            val = self.Value(self)
            val.laddress = value.pointer()
            val._type = value.type.dereference()
            val.nativeValue = value.nativeValue

        return val

    def callHelper(self, rettype, value, function, args):
        raise Exception("cdb does not support calling functions")

    def nameForCoreId(self, id):
        for dll in ['Utilsd', 'Utils']:
            idName = cdbext.call('%s!Utils::nameForId(%d)' % (dll, id))
            if idName is not None:
                break
        return self.fromNativeValue(idName)

    def putCallItem(self, name, rettype, value, func, *args):
        return

    def symbolAddress(self, symbolName):
        res = self.nativeParseAndEvaluate(symbolName)
        return None if res is None else res.address()
