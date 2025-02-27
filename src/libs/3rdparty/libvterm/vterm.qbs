Project {
    QtcLibrary {
        name: "vterm"
        type: "staticlibrary"

        Depends { name: "cpp" }
        cpp.includePaths: base.concat("include")

        Group {
            prefix: "src/"
            files: [
                "encoding.c",
                "fullwidth.inc",
                "keyboard.c",
                "mouse.c",
                "parser.c",
                "pen.c",
                "rect.h",
                "screen.c",
                "state.c",
                "unicode.c",
                "utf8.h",
                "vterm.c",
                "vterm_internal.h",
            ]
        }

        Export {
            Depends { name: "cpp" }
            cpp.includePaths: base.concat("include")
        }
    }
}
