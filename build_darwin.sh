#!/bin/bash

set -e

export MAIN_DIR=`pwd`
export OUTPUT_BINARY=$MAIN_DIR/.build/bin/onion
export RELEASE_DIR=$MAIN_DIR/.release
export RELEASE_BINARY=$MAIN_DIR/.release/onion/onion_mac
export FILES_DIR=$MAIN_DIR/files
export BUILD_DIR=$MAIN_DIR/.build

if [ -d $RELEASE_DIR ]; then
	echo Cleanup release dir...
	rm -r -f $RELEASE_DIR
fi

if [ -d $RELEASE_DIR ]; then
	echo Cleanup build dir...
	rm -r -f $BUILD_DIR
fi

echo Build...
if [ ! -d $BUILD_DIR ]; then
	mkdir $BUILD_DIR
fi
if [ ! -d $BUILD_DIR/linux ]; then
	mkdir $BUILD_DIR/linux
fi

pushd $BUILD_DIR/linux

cmake ../..
cmake --build . --config Release -j `nproc`

popd

echo Sync...

if [ ! -d $RELEASE_DIR ]; then
	mkdir $RELEASE_DIR
	rm -r -f $RELEASE_DIR/onion
fi

git clone https://$GITHUB_TOKEN@github.com/BareMetalEngine/onion.git $RELEASE_DIR/onion

echo Package...

cp $OUTPUT_BINARY $RELEASE_BINARY
echo Copied into '$RELEASE_BINARY'

$OUTPUT_BINARY glue -action=pack -file="$RELEASE_BINARY" -source="$FILES_DIR"

echo Submit...

pushd $RELEASE_DIR/onion
git add -f onion_mac
git commit --allow-empty -m "Updated compiled Mac binaries"
git push
popd
