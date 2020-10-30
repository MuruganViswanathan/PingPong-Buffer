#!/bin/sh
# 
# Description: Script to build for local host and ARM target using Toolchain.
# 
# 
# $DateTime: 2020/10/01 12:34:56 $
# $Revision: #1 $
# $Author: murugan $
# 
# 
# 

if [ -d bin ] ; then
	rm -rf bin
        mkdir bin
	mkdir -p bin/arm
	mkdir -p bin/local
fi

# Build for arm using toolchain

echo "Building for local..."
g++ -g -std=c++17 -pthread -Wall Client.cpp -o bin/local/Client

echo "done."


