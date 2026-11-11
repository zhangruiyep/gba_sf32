import os
import rtconfig

# Check SDK
SIFLI_SDK = os.getenv('SIFLI_SDK')
if not SIFLI_SDK:
    print("Please run set_env.bat in root folder of SIFLI SDK to set environment.")
    exit()
from building import *

# Prepare environment.
PrepareEnv()

################################## change rtconfig.xxx to customize build ########################################

# Add bootloader project
AddBootLoader(SIFLI_SDK, rtconfig.CHIP)

# Set default compile options
SifliEnv()

# The gbarecomp runtime is C++20 and unwinds its dispatch loop with C++
# exceptions; the SDK default compiles C++ with -fno-exceptions and no
# unwind tables. Local group flags do not reach CXXFLAGS, so patch the
# global ones before PrepareBuilding() copies them into the env.
rtconfig.CXXFLAGS += ' -std=c++20 -fexceptions -funwind-tables -fno-threadsafe-statics'

TARGET = rtconfig.OUTPUT_DIR + rtconfig.TARGET_NAME + '.' + rtconfig.TARGET_EXT

# Prepare building environment
objs = PrepareBuilding(None)
env = GetCurrentEnv()
# make a building
DoBuilding(TARGET, objs)

# Add flash table
AddFTAB(SIFLI_SDK, rtconfig.CHIP)

# Generate download .bat script
GenDownloadScript(env)
