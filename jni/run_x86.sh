#!/bin/bash
adb push /Users/damotou/Documents/workspace/memscan/libs/x86/scanmem /data/local/
adb push /Users/damotou/Documents/workspace/memscan/libs/x86/memclient /data/local/
adb shell chmod /data/local/scanmem 777
adb shell chmod /data/local/memclient 777



