
workspace "sox"
configurations {
  "Debug",
  "Release"
}
platforms {
  "windows",
  "macosx",
  "macosxARM",
  "linux64",
  "linuxARM64",
  -- etc.
}

-- enable tracing for debug builds
filter "configurations:Debug"
  symbols "On"
  defines {
    "_DEBUG",
    "TRACY_ENABLE"
  }

filter "configurations:Release"
  optimize "On"
  defines { "NDEBUG" }

-- Architecture is ARM64 by default
architecture "ARM64"

filter "system:windows"
  defines {
    "SOX_WINDOWS",
    "WIN32",
    "_WIN32"
  }

filter "system:macosx"
  defines {
    "SOX_MACOS"
  }

filter "platforms:macosxARM or linuxARM64"
  architecture "ARM"

filter "system:linux"
  buildoptions {
    "-std=c11",
    "-D_DEFAULT_SOURCE",
    "-D_POSIX_C_SOURCE=200112L",
    "-mcmodel=large",
    "-fPIE",
    "-Werror=return-type",
    "-Werror=implicit-function-declaration"
  }
  defines {
    "SOX_LINUX"
  }

solution "sox"
  filename "sox"
  location "projects"

  startproject "sox"

project "sox"
  kind "ConsoleApp"
  language "C"
  targetdir( "build" )
  defines {
  }
  flags {
    "MultiProcessorCompile"
  }
  debugdir "."

  libdirs {
    "build"
  }

  includedirs {
    "src",
    "ext/argtable3/src"
  }

  files {
    "src/**.h",
    "src/**.c",
    "ext/argtable3/src/argtable3.c",
    "ext/argtable3/src/arg_*.c"
  }

  -- ignore all testing files and old runtime implementation
  removefiles {
    "src/test/**",
    "src/**_test.*",
    "src/native/runtime.c"  -- Replaced by libsox_runtime
  }

  -- enable tracing for debug builds
  filter "configurations:Debug"
    links {
      --  "tracy"
    }
    sysincludedirs {
      --  "ext/tracy"
    }

  filter "system:linux"
    libdirs {
      os.findlib("m"),
      os.findlib("c")
    }
    links {
      "c",
      "dl",
      "m",
      "pthread",
    }

  filter "system:macosx"
    links {
      "Cocoa.framework",
      "IOKit.framework",
      "c",
      --  "tracy",
    }

  filter "system:windows"
    defines {
      "_CRT_SECURE_NO_WARNINGS"
    }
    disablewarnings {
      "4005"
    }
    editAndContinue "Off"
    links {
      "winstd"
    }
    sysincludedirs {
      "ext/winstd"
    }

  filter {}
   --  filter "files:src/main.c"
   --    compileas "Objective-C"

project "test"
  kind "ConsoleApp"
  language "C"
  targetdir( "build" )
  debugdir ( "." )
  defines {
    "SOX_UNIT"
  }

  links {
    "munit",
    "wasm_verify",
    "sox_runtime",
    "sox_runtime_shared"
  }

  libdirs {
    "build"
  }

  sysincludedirs {
    "ext",
    "ext/argtable3/src",
    "wasm_verify"
  }

  includedirs {
    "src",
    "wasm_verify"
  }

  files {
    "src/**.h",
    "src/**.c",
    "ext/argtable3/src/argtable3.c",
    "ext/argtable3/src/arg_*.c"
  }

  -- ignore the sox main and simplified native runtime (duplicate symbols with runtime_api.c)
  removefiles {
    "src/main.c",
    "src/native/runtime.c"
  }

  filter "system:macosx"
    links {
      "c"
    }
    linkoptions {
      "-rpath", "@loader_path"
    }

  filter "system:linux"
    libdirs {
      os.findlib("m"),
      os.findlib("c")
    }
    links {
      "c",
      "m",
      "pthread",
    }

  filter "system:windows"
    defines {
      "_CRT_SECURE_NO_WARNINGS"
    }
    disablewarnings {
      "4005"
    }
    -- Turn off edit and continue
    editAndContinue "Off"

  filter "platforms:windows"
    -- if on windows include the win32 impl of unix utils
    sysincludedirs {
      "ext/winstd"
    }

  filter {}

-- External Libraries

project "munit"
  kind "StaticLib"
  language "C"
  targetdir( "build" )

  files {
    "ext/munit/**.h",
    "ext/munit/**.c"
  }

if (system == windows) then
project "winstd"
  kind "StaticLib"
  language "C"
  targetdir( "build" )

  files { "ext/winstd/**" }
end
-- Static Runtime Library
project "sox_runtime"
  kind "StaticLib"
  language "C"
  targetdir("build")
  
  defines { "SOX_RUNTIME_BUILD" }
  
  includedirs {
    "src",
    "src/runtime_lib"
  }
  
  files {
    "src/runtime_lib/**.h",
    "src/runtime_lib/**.c"
  }
  
  filter "system:macosx"
    defines { "SOX_RUNTIME_MACOS" }
    buildoptions { "-fvisibility=hidden" }
  
  filter "system:linux"
    defines { "SOX_RUNTIME_LINUX" }
    buildoptions { "-fvisibility=hidden", "-fPIC" }
  
  filter "system:windows"
    defines { "SOX_RUNTIME_WINDOWS" }
  
  filter {}

-- Shared Runtime Library  
project "sox_runtime_shared"
  kind "SharedLib"
  language "C"
  targetdir("build")
  
  defines { "SOX_RUNTIME_BUILD", "SOX_RUNTIME_SHARED" }
  
  includedirs {
    "src",
    "src/runtime_lib"
  }
  
  files {
    "src/runtime_lib/**.h",
    "src/runtime_lib/**.c"
  }
  
  filter "system:macosx"
    defines { "SOX_RUNTIME_MACOS" }
    buildoptions { "-fvisibility=hidden" }
    linkoptions { "-dynamiclib" }
  
  filter "system:linux"
    defines { "SOX_RUNTIME_LINUX" }
    buildoptions { "-fvisibility=hidden", "-fPIC" }
    linkoptions { "-shared" }
  
  filter "system:windows"
    defines { "SOX_RUNTIME_WINDOWS", "_WINDLL" }
  
  filter {}
