#!/bin/bash

help()
{
	echo "./build.sh [chip] [oper]"
	echo "support chip: gk7205v200, rv1126, rk3588, hi3556v200, rv1126, hi3519DV500"
	echo "support oper:"
	echo "	 all	: Perform all operations"
	echo "	openssl	: Compile the third-party library openssl"
	echo "	zlib	: Compile the third-party library zlib"
	echo "	curl	: Compile the third-party library curl"
	echo "	main	: Compile pack code"
	echo "	pack	: Package the pack library"
	echo "	clean	: clean "

	exit 0
}

CUR_DIR=`pwd`
MEDIA_TYPE=$3
INSTALL_DIR="${CUR_DIR}/_install"

if [ "$1" == "gk7205v200" ]; then
    export PATH="/home/smb/compiler/arm-gcc6.3-linux-uclibceabi/bin/:$PATH"
    HOST="arm-gcc6.3-linux-uclibceabi"
elif [ "$1" == "rv1126" ]; then
    export PATH="/home/smb/compiler/gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf/bin/:$PATH"
    HOST="arm-linux-gnueabihf"
elif [ "$1" == "rk3588" ]; then
    export PATH="/home/smb/compiler/rk3588_host/bin/:$PATH"
    export PERL5LIB="/home/smb/compiler/rk3588_host/lib/perl5/5.32.1:$PERL5LIB"
    HOST="aarch64-buildroot-linux-gnu"
elif [ "$1" == "hi3556v200" ]; then
    export PATH="/home/smb/compiler/arm-himix410-linux/bin/:$PATH"
    HOST="arm-himix410-linux"
elif [ "$1" == "rv1126b" ]; then
    export PATH="/home/smb/compiler/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/:$PATH"
    HOST="aarch64-rockchip1031-linux-gnu"
elif [ "$1" == "hi3519DV500" ]; then
	export PATH="/home/smb/compiler/aarch64-v01c01-linux-gnu-gcc/bin/:$PATH"
	HOST="aarch64-linux-gnu"
else
    help
    exit 0
fi

build_openssl()
{
	SOURCE_DIR=openssl-1.1.1h
	SOURCE_INSTALL_DIR=$INSTALL_DIR/openssl

	cd $CUR_DIR/third_lib
	[ -d $SOURCE_INSTALL_DIR ] && [ "$1" != "rebuild" ] && return
	[ -d $SOURCE_DIR ] && rm -r $SOURCE_DIR
	[ -d $SOURCE_INSTALL_DIR ] && rm -r $SOURCE_INSTALL_DIR

	tar -xvzf $SOURCE_DIR.tar.gz
	cd $SOURCE_DIR
	./Configure no-asm no-async linux-generic32 --prefix=$SOURCE_INSTALL_DIR --cross-compile-prefix=$HOST-
	make && make install
	
	cd $CUR_DIR/third_lib
	[ -d $SOURCE_DIR ] && rm -r $SOURCE_DIR
}

build_zlib()
{
    SOURCE_DIR=zlib-1.3.1
    SOURCE_INSTALL_DIR=$INSTALL_DIR/zlib

    cd $CUR_DIR/third_lib
    [ -d $SOURCE_INSTALL_DIR ] && [ "$1" != "rebuild" ] && return
    [ -d $SOURCE_DIR ] && rm -r $SOURCE_DIR
    [ -d $SOURCE_INSTALL_DIR ] && rm -r $SOURCE_INSTALL_DIR

    tar -xvzf $SOURCE_DIR.tar.gz
    cd $SOURCE_DIR
    ./configure --prefix=${SOURCE_INSTALL_DIR}
    export HOST="$HOST"
    sed -i 's/LDSHARED=gcc/LDSHARED=${HOST}-gcc/g' Makefile
    make CC=${HOST}-gcc AR=${HOST}-ar
    make install
    
    cd $CUR_DIR/third_lib
    [ -d $SOURCE_DIR ] && rm -r $SOURCE_DIR
}

build_curl()
{
    SOURCE_DIR=curl-8.7.1
    SOURCE_INSTALL_DIR=$INSTALL_DIR/curl

    cd $CUR_DIR/third_lib
    [ -d $SOURCE_INSTALL_DIR ] && [ "$1" != "rebuild" ] && return
    [ -d $SOURCE_DIR ] && rm -r $SOURCE_DIR
    [ -d $SOURCE_INSTALL_DIR ] && rm -r $SOURCE_INSTALL_DIR

    tar -xvzf $SOURCE_DIR.tar.gz
    cd $SOURCE_DIR
    ./configure --prefix=${SOURCE_INSTALL_DIR} --host=${HOST} CC=${HOST}-gcc --without-ssl --with-zlib=${SOURCE_INSTALL_DIR} CFLAGS="-fPIC"
    make 
    make install
    
    cd $CUR_DIR/third_lib
    [ -d $SOURCE_DIR ] && rm -r $SOURCE_DIR
}

build_libdatachannel()
{
    SOURCE_DIR=libdatachannel-master
    SOURCE_INSTALL_DIR=$INSTALL_DIR/libdatachannel

    cd $CUR_DIR/third_lib
    [ -d $SOURCE_INSTALL_DIR ] && [ "$1" != "rebuild" ] && return
    [ -d $SOURCE_DIR ] && rm -r $SOURCE_DIR
    [ -d $SOURCE_INSTALL_DIR ] && rm -r $SOURCE_INSTALL_DIR

    tar -xvzf $SOURCE_DIR.tar.gz
    cd $SOURCE_DIR
	make CROSS=${HOST}- HOST=${HOST} \
		LDFLAGS="-pthread -I$INSTALL_DIR/openssl/include -L$INSTALL_DIR/openssl/lib" \
		CXXFLAGS="-std=c++17 -I$INSTALL_DIR/openssl/include -L$INSTALL_DIR/openssl/lib"
    
	mkdir -p $SOURCE_INSTALL_DIR
	mkdir -p $SOURCE_INSTALL_DIR/lib
	cp -raf ./*.a $SOURCE_INSTALL_DIR/lib
	cp -raf ./include $SOURCE_INSTALL_DIR

    cd $CUR_DIR/third_lib
    [ -d $SOURCE_DIR ] && rm -r $SOURCE_DIR
}

build_x264()
{
	SOURCE_DIR=libx264-git
	SOURCE_INSTALL_DIR=$INSTALL_DIR/x264

	cd $CUR_DIR/third_lib
	[ -d $SOURCE_INSTALL_DIR ] && [ "$1" != "rebuild" ] && return
	[ -d $SOURCE_DIR ] && rm -r $SOURCE_DIR
	[ -d $SOURCE_INSTALL_DIR ] && rm -r $SOURCE_INSTALL_DIR

	tar -xvf $SOURCE_DIR.tar.xz
	cd $SOURCE_DIR
	./configure --prefix=$SOURCE_INSTALL_DIR --cross-prefix=$HOST- --host=$HOST --disable-asm --disable-opencl --enable-static 
	make && make install
	
	cd $CUR_DIR/third_lib
	[ -d $SOURCE_DIR ] && rm -r $SOURCE_DIR
}

build_ffmpeg()
{
	SOURCE_DIR=ffmpeg-7.0
	SOURCE_INSTALL_DIR=$INSTALL_DIR/ffmpeg

	cd $CUR_DIR/third_lib
	[ -d $SOURCE_INSTALL_DIR ] && [ "$1" != "rebuild" ] && return
	[ -d $SOURCE_DIR ] && rm -r $SOURCE_DIR
	[ -d $SOURCE_INSTALL_DIR ] && rm -r $SOURCE_INSTALL_DIR

	tar -xvzf $SOURCE_DIR.tar.gz
	cd $SOURCE_DIR
	export PKG_CONFIG_PATH="$INSTALL_DIR/x264/lib/pkgconfig:$PKG_CONFIG_PATH"
	./configure --cross-prefix=$HOST- --enable-cross-compile --target-os=linux --cc=$HOST-gcc --arch=arm --prefix=$SOURCE_INSTALL_DIR \
		--enable-static --enable-gpl --enable-nonfree --enable-swscale --enable-ffmpeg --disable-armv5te --disable-yasm --enable-libx264 --enable-protocol=rtmp \
		--extra-cflags=-I$INSTALL_DIR/x264/include --extra-ldflags=-L$INSTALL_DIR/x264/lib --extra-libs="-lpthread" --pkg-config="pkg-config --static"
	make -j4 && make install
	
	cd $CUR_DIR/third_lib
	[ -d $SOURCE_DIR ] && rm -r $SOURCE_DIR
}

build_main()
{
    cd $CUR_DIR
    if [ -d $CUR_DIR/build ]; then
        rm -r $CUR_DIR/build
    fi 
    if [ -d $INSTALL_DIR/nat ]; then
        rm -r $INSTALL_DIR/nat
    fi 

    mkdir build
    cd $CUR_DIR/build
    cmake .. -DCMAKE_C_COMPILER=${HOST}-gcc -DCMAKE_CXX_COMPILER=${HOST}-g++ -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}/nat -DMEDIA_TYPE=$MEDIA_TYPE
    make
    make install
}

build_pack()
{
	CUR_DATE=$(date +"%Y%m%d")
    PACK_DIR=nat_$1_$CUR_DATE
    [ -d $PACK_DIR ] && rm -r $PACK_DIR
    [ -f $PACK_DIR.tar.gz ] && rm -r $PACK_DIR.tar.gz
    
    cd $CUR_DIR
    mkdir $PACK_DIR
    cd $PACK_DIR
    cp $INSTALL_DIR/nat/* ./ -raf

    cd ..
    tar -cvzf $PACK_DIR.tar.gz $PACK_DIR
    rm $PACK_DIR -r
}

build_clean()
{
	cd $CUR_DIR

	rm $INSTALL_DIR -r
	rm build -r
	rm nat_*.tar.gz -r
}

[ -z "$2" ] && help
[ "$2" == "help" ] && help && exit
[ "$2" == "openssl" ] && build_openssl rebuild && exit
[ "$2" == "zlib" ] && build_zlib rebuild && exit
[ "$2" == "curl" ] && build_curl rebuild && exit
[ "$2" == "libdatachannel" ] && build_libdatachannel rebuild && exit
[ "$2" == "x264" ] && build_x264 rebuild && exit
[ "$2" == "ffmpeg" ] && build_ffmpeg rebuild && exit
[ "$2" == "main" ] && build_main && exit
[ "$2" == "pack" ] && build_pack $1 && exit
[ "$2" == "clean" ] && build_clean && exit

if  [ "$2" == "all" ];then
	build_openssl
    build_zlib
    build_curl
	build_libdatachannel
	if [ "$3" == "ffmpeg" ]; then
		build_x264
		build_ffmpeg
	fi
    build_main
    build_pack $1
else
    help
    exit 0
fi
