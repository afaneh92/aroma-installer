## AROMA-INSTALLER

## How-to compile it:

```sh
export ALLOW_MISSING_DEPENDENCIES=true
. build/envsetup.sh
lunch full_yourdevice-eng
make aroma_installer
make aroma_installer_zip
```

AROMA is an advanced update-binary for Android that contains many features
like Wizard Installation, Touch User Interface (AROMA UI), Customizable Packages,
System Inspecting, Themeable, and User Interactive.

* Programming Language
  It was application which runs on Android device but not on Android OS
  Environment that the native applications usually use Java.
  AROMA Installer use C (Pure C, not C++), so it may run on another
  platform like x86 with small modifications ofcourse.

* Used Libraries
  ZLIB - ofcourse, it was the most awesome thing in computer world
  PNG, MinZIP, Freetype.
  All library need to compiled as static to make sure it runs
  without any dependency issue in any devices.

* Official Binary
  I Only support ARM device, but it was opensourced, so anyone
  can play with the source and compiled it for they own devices.
