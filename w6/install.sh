#!/bin/bash

# This install script is for debian based linux distributions only

numprocs=$(nproc) # Workshop 6
# numprocs=$(cat /proc/cpuinfo | grep processor | wc -l) # Workshop 7. Shouldn't make a difference
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"


#Install CMake

sudo apt-get update
sudo apt-get install -y g++ zookeeper libboost-all-dev  libzookeeper-mt2 zookeeperd zookeeper-bin libzookeeper-mt-dev ant check build-essential autoconf libtool pkg-config checkinstall git zlib1g libssl-dev
echo "Instaling cmake 3.0+"
mkdir -p ~/src
cd ~/src
sudo apt-get remove -y cmake
wget http://www.cmake.org/files/v3.19/cmake-3.19.5.tar.gz
tar xf cmake-3.19.5.tar.gz
cd cmake-3.19.5
./configure
make -j${numprocs}
sudo checkinstall -y --pkgname cmake
echo "PATH=/usr/local/bin:$PATH" >> ~/.profile
source ~/.profile


# Install Kubectl
sudo apt-get update
sudo apt-get install -y apt-transport-https ca-certificates curl
sudo curl -LO "https://dl.k8s.io/release/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl"
sudo curl -LO "https://dl.k8s.io/$(curl -L -s https://dl.k8s.io/release/stable.txt)/bin/linux/amd64/kubectl.sha256"
echo "$(<kubectl.sha256) kubectl" | sha256sum --check
sudo install -o root -g root -m 0755 kubectl /usr/local/bin/kubectl

# Install Kind
git clone https://github.com/kubernetes-sigs/kind
cd kind
make
make install

# Install Helm
curl https://baltocdn.com/helm/signing.asc | sudo apt-key add -
sudo apt-get install apt-transport-https --yes
echo "deb https://baltocdn.com/helm/stable/debian/ all main" | sudo tee /etc/apt/sources.list.d/helm-stable-debian.list
sudo apt-get update
sudo apt-get install helm

#Install Conservator
echo "Installing Conservator C++ Zookeeper Wrapper"
mkdir -p ~/src
cd ~/src
git clone https://github.gatech.edu/cs8803-SIC/conservator.git
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

#Install cpprestsdk
mkdir -p ~/src
cd ~/src
git clone https://github.com/microsoft/cpprestsdk.git
cd cpprestsdk
mkdir build && cd build
cmake .. -DCPPREST_EXCLUDE_WEBSOCKETS=ON
make -j2 && sudo make install



# #install etcd client
# mkdir -p ~/src
# cd ~/src
# git clone https://github.com/etcd-cpp-apiv3/etcd-cpp-apiv3.git
# cd etcd-cpp-apiv3
# mkdir build && cd build
# cmake ..
# make -j2 && sudo make install




#install GLOG
mkdir -p ~/src
cd ~/src
git clone https://github.com/google/glog.git
cd glog
cmake -H. -Bbuild -G "Unix Makefiles"
cmake --build build
cmake --build build --target test
sudo cmake --build build --target install
echo "export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH" >> ~/.bashrc
source ~/.bashrc

# Clean up
sudo rm kubectl
sudo rm kubectl.sha256


# Install Azure CLI
sudo apt-get update
sudo apt-get install -y ca-certificates curl apt-transport-https lsb-release gnupg
curl -sL https://packages.microsoft.com/keys/microsoft.asc |
    gpg --dearmor |
    sudo tee /etc/apt/trusted.gpg.d/microsoft.gpg > /dev/null
AZ_REPO=$(lsb_release -cs)
echo "deb [arch=amd64] https://packages.microsoft.com/repos/azure-cli/ $AZ_REPO main" |
    sudo tee /etc/apt/sources.list.d/azure-cli.list
sudo apt-get install -y azure-cli

# START Workshop 7 install script
echo "Installing Casablanca"
sudo apt-get install -y g++ git libboost-all-dev libwebsocketpp-dev openssl libssl-dev ninja-build libxml2-dev uuid-dev libunittest++-dev
mkdir -p src
cd ~/src
git clone https://github.com/Microsoft/cpprestsdk.git casablanca
cd casablanca
mkdir build.release
cd build.release
cmake -G Ninja .. -DCMAKE_BUILD_TYPE=Release
ninja
cd Release/Binaries
./test_runner *_test.so
cd -
sudo ninja install
sudo ldconfig

echo "Creating the CMake Find file"
cd /tmp
wget https://raw.githubusercontent.com/Azure/azure-storage-cpp/master/Microsoft.WindowsAzure.Storage/cmake/Modules/LibFindMacros.cmake
sudo mv LibFindMacros.cmake /usr/local/share/cmake-3.19/Modules
sudo wget https://raw.githubusercontent.com/Azure/azure-storage-cpp/master/Microsoft.WindowsAzure.Storage/cmake/Modules/FindCasablanca.cmake
sudo mv FindCasablanca.cmake /usr/local/share/cmake-3.19/Modules
wget https://raw.githubusercontent.com/Tokutek/mongo/master/cmake/FindSSL.cmake
sudo mv FindSSL.cmake /usr/local/share/cmake-3.19/Modules

echo "Install Azure Storage CPP"
cd ~/src
git clone https://github.com/Azure/azure-storage-cpp.git
cd azure-storage-cpp/Microsoft.WindowsAzure.Storage
mkdir build.release
cd build.release
CASABLANCA_DIR=/usr/local/lib CXX=g++-7 cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/g++
make -j${numprocs}
#Install library
sudo cp ~/src/azure-storage-cpp/Microsoft.WindowsAzure.Storage/build.release/Binaries/*  /usr/local/lib
sudo rm /usr/local/lib/libazurestorage.so
sudo rm /usr/local/lib/libazurestorage.so.7
sudo ln -s /usr/local/lib/libazurestorage.so.7.5 /usr/local/lib/libazurestorage.so # This seemed to fail, but then worked after a delay?
sudo ln -s /usr/local/lib/libazurestorage.so.7.5 /usr/local/lib/libazurestorage.so.7
sudo cp -r ~/src/azure-storage-cpp/Microsoft.WindowsAzure.Storage/includes/* /usr/local/include

sudo ldconfig

#Create the CMake Find File
cd ${DIR}
sudo cp CMakeModules/FindAzureStorageCpp.cmake /usr/local/share/cmake-3.19/Modules
# END Workshop 7 install script


# Intall C++ netlib
mkdir -p ~/src
cd ~/src
git clone --branch 0.13-release https://github.com/cpp-netlib/cpp-netlib.git
cd cpp-netlib/
git submodule update --init
./build.sh
cd build
sudo make install

