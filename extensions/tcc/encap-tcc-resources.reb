REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Gather and Compress redistributable includes/libs for TCC"
    File: %encap-tcc-resources.reb  ; used by MAKE-EMITTER

    Rights: {
        Copyright 2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        In order for the TCC compiler to build useful programs, it depends
        on the C standard library.  This means the system you are compiling
        onto must have %include/ and %lib/ directories with appropriate
        files for your OS.  In addition, TCC patches its own overrides for
        includes like <stddef.h>, so that they interact with its code
        generation instead of something like GCC's "intrinsics".  It does
        this by prioritizing its `-I` include paths on top:

        https://stackoverflow.com/q/53154898/

        But for this to work, the appropriate files must be on the user's
        disk.  It's generally assumed that Linux systems have a working C
        compiler with headers corresponding to its libc, so only the TCC
        specific include overrides and service lib (libtcc1.a) are needed.
        These can be obtained via `sudo apt-get install tcc`, but that
        only works if the distribution's TCC matches the TCC that was
        linked into the Rebol that was built.  In the general case, the
        Rebol should probably embed the appropriate files into the binary
        and unpack them on demand when run on a new system.

        Windows is slightly different, because one doesn't generally assume
        a "standard" compiler is available.  TCC provides a basic set of
        include and lib stubs for Windows libc, in addition to the overrides
        like it has on linux.  This all should be packed into the executable
        as well.
    }
    Notes: {
        !!! This file is a work in progress.  As a first step, it just packs
        an auxiliary .zip file that users can download and unzip somewhere
        manually.  Ultimately it would be more turnkey, once there's a way
        for extensions to add their own subfolders to the encapping process.
    }
]

; This script is run from %extensions/tcc
; For now, just go back up a few steps

change-dir %../../  ; assume this is the TOP_DIR
top-dir: what-dir
comment [
    top-dir: try as file! try get-env "TOP_DIR"
    exists? top-dir or [
        print ["TOP_DIR does not exist" top-dir]
        fail [
            "TOP_DIR should be set to where the git repository for Rebol was"
            "cloned into.  (This should be REBOL_SOURCE_DIR or something"
            "more clear, but it's what was configured on Travis CI)"
        ]
    ]
]

output-dir: top-dir/build/

do %tools/common.r
args: parse-args system/script/args  ; either from command line or DO/ARGS

do %tools/systems.r
system-config: config-system args/OS_ID

all [
    config-tccdir: try local-to-file try get-env "CONFIG_TCCDIR"
    (exists? config-tccdir) or [
        print ["CONFIG_TCCDIR setting is invalid" config-tccdir]
        false
    ]

    tcc-libtcc1-file: as file! try (
        get-env "TCC_LIBTCC1_FILE" else [config-tccdir/libtcc1.a]
    )
    (exists? tcc-libtcc1-file) or [
        print ["TCC_LIBTCC1_FILE setting is invalid" tcc-libtcc1-file]
        false
    ]
] else [
    fail [
        "TCC Extension requires CONFIG_TCCDIR environment variable to be"
        "set.  This is semi-standardized, as it's where compiled TCC"
        "programs look for the runtime (see `tcc_set_lib_dir()`).  If the"
        "file %libtcc1.a is somewhere other than that directory, then"
        "TCC_LIBTCC1_FILE should be its location.  See the README:"
        http://github.com/metaeducation/ren-c/extensions/tcc/README.md
    ]
]

; !!! If you want to get the zip dialect to use a specific filename, you
; can do so by reading the binary data literally and storing a file.  But
; it can only store directories relatively by their actual names, and if
; you get an absolute path stored unless you are *in* the directory.  Work
; around it for now by changing to the config-tccdir, where the nested
; directories are being stored.
;
; https://forum.rebol.info/t/1172
;
change-dir config-tccdir


; Embedded files for supporting basic compilation that need to be extracted to
; the local filesystem when running a COMPILE.  A more elegant solution than
; extraction would involve changes to TCC, proposed here:
;
; http://lists.nongnu.org/archive/html/tinycc-devel/2018-12/msg00011.html
;
encap: compose [
    ;
    ; !!! Is it worth it to put %rebol.h in a `rebol` subdirectory?
    ;
    %rebol.h (read top-dir/build/prep/include/rebol.h)

    ; Only a few special headers needed for TCC, that are selectively
    ; used to override the system standard ones with `-I`:
    ;
    ; https://repo.or.cz/tinycc.git/tree/HEAD:/include
    ; https://stackoverflow.com/q/53154898
    ;
    %include/  ; (??? config-tccdir/include) - see dir note above

    ; See README.md for an explanation of %libtcc1.a
    ;
    %libtcc1.a (read tcc-libtcc1-file)

    ; https://repo.or.cz/tinycc.git/blob/HEAD:/win32/tcc-win32.txt
    ;
    ; The Windows directory contains some other junk, like examples.
    ;
    ((if system-config/os-base = 'Windows [
        compose [
            ; typically %(...)/win32/include
            %win32/include/  ; (???) - see dir note above
            %win32/lib/  ; (???) - see dir note above
        ]
    ]))
]


print ["MAKING ZIP FILE:" output-dir/tcc-encap.zip]
zip/deep/verbose output-dir/tcc-encap.zip encap

print ["(ULTIMATELY WE WANT TO ENCAP THAT DIRECTLY INTO THE TCC EXTENSION)"]
