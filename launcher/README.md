# Launcher source

The `dev` folder contains the Python/PowerShell launcher and C launcher stub
from the 26 Sep 23:02 package. Players should use the complete release ZIP;
the EXE depends on its adjacent `app` and `python` folders.

This is a source snapshot, not a self-contained build environment. Native
recorder CMake files expect the original workspace layout and Windows SDK.
The portable build also needs the reviewed runtime DLLs and a CPython runtime.
See [native reconstruction](../mod/BUILD.md) and the included component notices.
