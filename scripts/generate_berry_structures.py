# Copyright (C) 2023  Stephan Hadinger & Theo Arends
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

# generate all precompiled Berry structures from multiple modules
CURRENT_DIR = os.getcwd()
BERRY_GEN_DIR = join(env.subst("$PROJECT_DIR"), "lib","berry")
os.chdir(BERRY_GEN_DIR)
fileList = glob.glob(join(BERRY_GEN_DIR, "generate", "*"))
for filePath in fileList:
    try:
        os.remove(filePath)
        print("Deleting file : ", filePath)
    except:
        print("Error while deleting file : ", filePath)
cmd = (env["PYTHONEXE"],join("tools","coc","coc"),"-o","generate","src",join("..","berry_port"),"-c",join("..","berry_port","berry_conf.h"))
returncode = subprocess.call(cmd, shell=False)
os.chdir(CURRENT_DIR)
