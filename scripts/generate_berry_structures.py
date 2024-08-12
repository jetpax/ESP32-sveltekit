# Copyright (C) 2023  Stephan Hadinger & Theo Arends
# Copyright (C) 2024  theelims
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

Import("env")

import os
import glob
import subprocess
from os.path import join


CURRENT_DIR = os.getcwd()
BERRY_GEN_DIR = join(env.subst("$PROJECT_DIR"), "lib","berry")
buildFlags = env.ParseFlags(env["BUILD_FLAGS"])

def feature_is_set(flag):
    for define in buildFlags.get("CPPDEFINES"):
        # print(define)
        if (isinstance(define, list) and define[0] == flag and define[1] == "1"):
            return True
    return False

# generate all precompiled Berry structures from multiple modules
def build_berry_structures():
    os.chdir(BERRY_GEN_DIR)
    fileList = glob.glob(join(BERRY_GEN_DIR, "generate", "*"))
    for filePath in fileList:
        try:
            os.remove(filePath)
            print("Deleting file : ", filePath)
        except:
            print("Error while deleting file : ", filePath)
    cmd = (env["PYTHONEXE"],join("tools","coc","coc"),"-o","generate","src",join("..","berry_port"),join("..","berry_mapping","src"),"-c",join("..","berry_port","berry_conf.h"))
    returncode = subprocess.call(cmd, shell=False)
    os.chdir(CURRENT_DIR)
    return

if feature_is_set("FT_BERRY"):
    build_berry_structures()




