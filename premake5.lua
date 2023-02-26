
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

-- Architecture is x86_64 by default
architecture "x86_64"

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
    "src"
  }

  files {
    "src/**.h",
    "src/**.c"
  }

  -- ignore all testing files
  removefiles {
    "src/test/**",
    "src/**_test.*"
  }

  -- enable tracing for debug builds
  filter "configurations:Debug"
    links {
      --  "tracy"
    }
    sysincludedirs {
      --  "ext/tracy"
    }

  if (system == linux) then
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
  end

  if (system == macosx) then
    links {
      "Cocoa.framework",
      "IOKit.framework",
      "c",
      --  "tracy",
    }
  end

  if (system == windows) then
  
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

  end
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
    "munit"
  }

  libdirs {
    "build"
  }

  sysincludedirs {
    "ext"
  }

  includedirs {
    "src"
  }

  files {
    "src/**.h",
    "src/**.c",

  }

  -- ignore the sox main
  removefiles {
    "src/main.c"
  }

  if (system == macosx) then
    links {
      "c"
    }
  end

  if (system == linux) then
    libdirs {
      os.findlib("m"),
      os.findlib("c")
    }
    links {
      "c",
      "m",
      "pthread",
    }
  end

  if (system == windows) then
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
  end

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