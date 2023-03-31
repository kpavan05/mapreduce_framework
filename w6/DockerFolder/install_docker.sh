#!/bin/bash

# This install script is for debian based linux distributions only

numprocs=$(nproc)
# Script to build your protobuf, c++ binaries, and docker images here
# mkdir -p ~/src
# cd ~/src
# git clone https://pkkk3:Gatech@123@github.gatech.edu/cs8803-SIC/workshop6-c.git

echo "Instaling cmake 3.0+"
mkdir -p ~/src
cd ~/src
# sudo apt-get remove -y cmake
# apt-get remove -y cmake
wget http://www.cmake.org/files/v3.19/cmake-3.19.5.tar.gz
tar xf cmake-3.19.5.tar.gz
cd cmake-3.19.5
./configure
make -j${numprocs}
sudo checkinstall -y --pkgname cmake
# checkinstall -y --pkgname cmake
echo "PATH=/usr/local/bin:$PATH" >> ~/.profile


#Install Conservator
echo "Installing Conservator C++ Zookeeper Wrapper"
mkdir -p ~/src
cd ~/src
# git clone https://github.gatech.edu/cs8803-SIC/conservator.git
cp ~/conservator.tar.gz .
tar xf conservator.tar.gz 
cd conservator
cmake .
make -j${numprocs}
sudo checkinstall -y --pkgname conservator

# Install C++ GRPC
echo "Installing GRPC"
mkdir -p ~/src
cd ~/src
git clone --recurse-submodules -b v1.35.0 https://github.com/grpc/grpc
cd grpc
mkdir -p cmake/build
cd cmake/build
cmake \
	  -DCMAKE_BUILD_TYPE=Release \
	    -DgRPC_INSTALL=ON \
	      -DgRPC_BUILD_TESTS=OFF \
	        -DgRPC_SSL_PROVIDER=package \
		  ../..
make -j${numprocs}
sudo checkinstall -y --pkgname grpc
sudo ldconfig
# checkinstall -y --pkgname grpc
# ldconfig

#install GLOG
mkdir -p ~/src
cd ~/src
git clone https://github.com/google/glog.git
cd glog
cmake -H. -Bbuild -G "Unix Makefiles"
cmake --build build
cmake --build build --target test
sudo cmake --build build --target install
# cmake --build build --target install
echo "export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH" >> ~/.bashrc
source ~/.bashrc

echo "untar the workshop directory"
mkdir -p ~/src
cd ~/src
cp ~/workshop6-c.tar.gz .
tar xf workshop6-c.tar.gz
