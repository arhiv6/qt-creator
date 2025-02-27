// Copyright (C) 2023 Tasuku Suzuki
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/editormanager/ieditorfactory.h>

namespace TextEditor::Internal {

class MarkdownEditorFactory final : public Core::IEditorFactory
{
public:
    MarkdownEditorFactory();
};

} // TextEditor::Internal
