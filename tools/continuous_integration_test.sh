#!/bin/bash

# Continuous integration script. Builds and installs
# shadow-plugin-tor and necessary dependencies, runs a simulation,
# and validates the results.

# Preconditions:
#  * CC environment var set to preferred C compiler (gcc or clang)
#  * ubuntu18 packages are available.
#  * Run as a sudoer.
#  * ./shadow-tor-plugin is source directory for shadow-tor-plugin.
#  * ./shadow is source directory for shadow.
#  * ./tgen is source directory for tgen.

set -euo pipefail

TRACE_CMD='echo \
&& echo "********************************" \
&& echo $FUNCNAME $@ \
&& echo "********************************"'

install_compiler () (
    eval "$TRACE_CMD"
    case "$1" in
      gcc)   sudo apt-get install -y gcc g++ ;;
      clang) sudo apt-get install -y clang ;;
      *) echo "bad cc=$1" && exit 1 ;;
    esac
)

install_shadow_build_deps () (
    eval "$TRACE_CMD"
    sudo apt-get install -y \
        cmake \
        make \
        xz-utils \
        python \
        libglib2.0-0 \
        libglib2.0-dev \
        libigraph0v5 \
        libigraph0-dev \
        libc-dbg \
        python-pyelftools
)

install_shadow () (
    eval "$TRACE_CMD"
    cd $1
    ./setup build
    ./setup install
)

install_tgen_build_deps () (
    eval "$TRACE_CMD"
    sudo apt-get install -y \
        cmake \
        libglib2.0 \
        libglib2.0-dev \
        libigraph0-dev \
        libigraph0v5
)

install_tgen () (
    eval "$TRACE_CMD"
    cd $1
    mkdir -p build
    cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=$2
    make install
)

install_shadow_plugin_tor_build_deps () (
    eval "$TRACE_CMD"
    sudo apt-get install -y \
        autoconf \
        automake \
        liblzma5 \
        liblzma-dev \
        zlib1g-dev
)

install_shadow_plugin_tor () (
    eval "$TRACE_CMD"
    cd $1
    ./setup dependencies -y
    ./setup build -y
    ./setup install
)

run_simulation () (
    eval "$TRACE_CMD"
    SIMULATION_DIR=$1
    SHADOW_PLUGIN_TOR_SRC=$2
    SHADOW_BIN=$3

    mkdir -p $SIMULATION_DIR
    cd $SIMULATION_DIR

    tar xaf $SHADOW_PLUGIN_TOR_SRC/resource/shadowtor-minimal-config.tar.xz
    mv shadowtor-minimal-config shadowtor-minimal
    cd shadowtor-minimal
    $SHADOW_BIN shadow.config.xml > shadow.log
)

validate_simulation () (
    eval "$TRACE_CMD"
    SIMULATION_DIR=$1

    COMPLETE=`find $SIMULATION_DIR/shadowtor-minimal/shadow.data/hosts -name '*client*.log' -exec grep transfer-complete \{\} \; | wc -l`
    if [ $COMPLETE -ne 40 ]
    then
      echo Expected 40 transfers got $COMPLETE
      return 1
    fi
)

main () (
    INSTALL_PREFIX=$HOME/.shadow
    TGEN_SRC=$PWD/tgen
    SHADOW_SRC=$PWD/shadow
    SHADOW_PLUGIN_TOR_SRC=$PWD/shadow-plugin-tor
    SIMULATION_DIR=$PWD/simulation

    install_compiler $CC

    install_shadow_build_deps
    install_shadow $SHADOW_SRC $INSTALL_PREFIX

    install_tgen_build_deps
    install_tgen $TGEN_SRC $INSTALL_PREFIX

    install_shadow_plugin_tor_build_deps
    install_shadow_plugin_tor $SHADOW_PLUGIN_TOR_SRC $INSTALL_PREFIX
    
    run_simulation $SIMULATION_DIR $SHADOW_PLUGIN_TOR_SRC $INSTALL_PREFIX/bin/shadow
    validate_simulation $SIMULATION_DIR
)

main

