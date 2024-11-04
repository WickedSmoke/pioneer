options [
    audio-api: 'faun    "Audio API ('faun 'sdl)"
    make-dep: true
]

if make-dep [
lib %fmt [
    include_from [%contrib/fmt/include %contrib/fmt/include/fmt]
    sources_from %contrib/fmt/src [%format.cc %os.cc]
]

lib %imgui [
    cxxflags "-DIMGUI_IMPL_OPENGL_LOADER_GLEW=1"
    include_from [
        %contrib %contrib/imgui %contrib/imgui/backends
        %/usr/include/SDL2
    ]
    sources_from %contrib/imgui %.cpp
    sources [%contrib/imgui/backends/imgui_impl_sdl2.cpp]
]

lib %lua [
    objdir %.copr/lua_obj
    cflags "-DLUA_USE_APICHECK"
    linux [cflags "-DLUA_USE_POSIX"]
    include_from %contrib/lua/src
    sources_from %contrib/lua/src %.c
]

lib %lz4 [
    cflags "-DXXH_NAMESPACE=LZ4_"
    include_from %contrib/lz4
    sources_from %contrib/lz4 %.c
]

lib %support [
    cxxflags "-Wno-unused-parameter"    ; Profiler.cpp
    include_from [
        %contrib/jenkins
        %contrib/nanosockets
        %contrib/PicoDDS
        %contrib/profiler
        %/usr/include/SDL2  ; lookup3.c
    ]
    sources [
        %contrib/jenkins/lookup3.c
        %contrib/nanosockets/nanosockets.c
        %contrib/PicoDDS/PicoDDS.cpp
        %contrib/profiler/Profiler.cpp
    ]
]
]

pi-default: does [
    include_from [
        %src/core
        %src/lua
        %contrib
        %contrib/fmt/include
        %contrib/imgui
        %contrib/pcg-cpp
        %/usr/include/SDL2
        %/usr/include/sigc++-2.0
        %/usr/lib64/sigc++-2.0/include
    ]
    linux [
        cxxflags "-Wno-unused-parameter -Wno-unused-but-set-parameter"
        cxxflags "-Wno-implicit-fallthrough"
    ]
]

lib %pioneer-core [
    pi-default
    include_from [
        %contrib/fmt/include/fmt
        %src
        %src/graphics
    ]
    sources_from %src/core %.cpp
]

lib %pioneer-lua [
    pi-default
    objdir %.copr/pilua_obj ; Lua.cpp exists in both src/lua & src/scenegraph.
    include_from [
        %contrib/lua
        %contrib/lua/src
        %src
        %src/galaxy
        %src/graphics
        %src/lua/core
        %src/scenegraph
        %src/ship
        %src/sound
    ]
    sources_from %src/lua %.cpp
    sources_from %src/lua/core %.cpp
]

ifn exists? %src/buildopts.h [
    write %src/buildopts.h {{
        #ifndef BUILDOPTS_H
        #define BUILDOPTS_H

        #define PIONEER_EXTRAVERSION "4c57fb9f5"
        #define PIONEER_VERSION "20240710"
        #define PIONEER_DATA_DIR "/usr/local/share/pioneer/data"
        #define REMOTE_LUA_REPL_PORT

        #define WITH_OBJECTVIEWER 1
        #define WITH_DEVKEYS 1
        #define HAS_FPE_OPS 1

        #endif
    }}
]

lib %pioneer-lib [
    pi-default
    cxxflags "-DIMGUI_DEFINE_MATH_OPERATORS"

    include_from [
        %/usr/include/freetype2
        %contrib/fmt/include/fmt
        %contrib/lua
        %src
    ]

    sfiles: make block! 64
    foreach it read %src [
        if eq? %.cpp skip tail it -4 [
            append sfiles join %src/ it
        ]
    ]
    sources difference sfiles [
        %src/main.cpp
        %src/modelcompiler.cpp
        %src/savegamedump.cpp
        %src/tests.cpp
        %src/textstress.cpp
        %src/uitest.cpp
    ]

    src-dirs: [
        %src/collider
        %src/galaxy
        %src/graphics
        %src/graphics/dummy
        %src/graphics/opengl
       ;%src/gui
        %src/math
        %src/pigui
        %src/scenegraph
        %src/ship
        %src/terrain
        %src/text
    ]
    include_from src-dirs
    foreach dir src-dirs [
        sources_from dir %.cpp
    ]

    include_from %src/sound
    sources_from %src/sound [
        %AmbientSounds.cpp
        %SoundMusic.cpp
    ]
    sources to-block either eq? audio-api 'faun
        %src/backend/Sound_faun.cpp
        %src/sound/Sound.cpp   ; backend/Sound_sdl.cpp

    linux [
        sources [
            %src/posix/FileSystemPosix.cpp
            %src/posix/OSPosix.cpp
        ]
    ]
    win32 [
        sources [
            %src/win32/FileSystemWin32.cpp
            %src/win32/OSWin32.cpp
            %src/win32/TextUtils.cpp
        ]
    ]
]

exe %pioneer-kr [
    pi-default
    opengl
    include_from [
        %contrib/json
        %src
    ]
    libs_from %. [
        ; Circular dependency between -lua & -lib.
        %pioneer-lua %pioneer-lib %pioneer-lua %pioneer-core
        %fmt %imgui %lua %lz4 %support
    ]
    libs either eq? audio-api 'faun %faun %vorbisfile
    linux [
        libs [%assimp %GLEW %SDL2_image %SDL2 %sigc-2.0]
    ]
    win32 [
        libs [%assimp %GLEW %SDL2_image %SDL2 %sigc-2.0 %ws2_32]
    ]
    sources [%src/main.cpp]
]
