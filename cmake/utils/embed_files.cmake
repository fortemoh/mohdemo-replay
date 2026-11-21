# embed_files.cmake (cmake -P): a C table of files compiled into the client,
# the in-memory file set the filesystem reads when no file on disk or in a
# pak has the name (FS_SetBuiltinFiles). -DROOT=<dir the names are relative
# to> -DOUT=<the .c made> -DFILES=<the files, ;-separated>
set(text "// made by cmake/utils/embed_files.cmake: do not edit\n\ntypedef struct builtinFile_s {\n    const char *name;\n    const unsigned char *data;\n    int size;\n} builtinFile_t;\n\n")
set(table "")
set(i 0)
foreach(f ${FILES})
    file(RELATIVE_PATH name ${ROOT} ${f})
    file(READ ${f} hex HEX)
    string(LENGTH "${hex}" n)
    math(EXPR n "${n} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
    string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" hex "${hex}")
    string(APPEND text "static const unsigned char file${i}[] = {\n${hex}0};\n")
    string(APPEND table "    {\"${name}\", file${i}, ${n}},\n")
    math(EXPR i "${i} + 1")
endforeach()
string(APPEND text "\nconst builtinFile_t builtin_files[] = {\n${table}    {0, 0, 0}\n};\n")
file(WRITE ${OUT}.tmp "${text}")
file(COPY_FILE ${OUT}.tmp ${OUT} ONLY_IF_DIFFERENT)
file(REMOVE ${OUT}.tmp)
