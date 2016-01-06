#!/bin/bash
adb push /Users/damotou/Documents/workspace/memscan/libs/armeabi/scanmem /data/local/

adb shell chmod /data/local/scanmem 777
adb shell  /data/local/scanmem


