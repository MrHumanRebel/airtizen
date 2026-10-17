cmake_minimum_required(VERSION 3.4.1)
aux_source_directory(. plist_src)
# cnary.c is an upstream standalone test program, not library code. Its main
# can silently replace the PPAPI launcher when this archive precedes libppapi.
list(FILTER plist_src EXCLUDE REGEX "/cnary\\.c$")
set(DIR_SRCS ${plist_src})
add_library( plist
        STATIC
        ${DIR_SRCS})
