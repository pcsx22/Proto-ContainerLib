#!/bin/bash

g++ --std=c++0x container.cpp $(pkg-config --cflags --libs libnl-3.0 libnl-genl-3.0 libnl-route-3.0) -lcgroup -o container

if [ $? -eq 0 ]; then 
	echo "Build Successfull. Run command as sudo ./container --rootfs=/path/to/root"
else
	echo "Compilation error.."
fi


