#!/bin/bash

# Determine OS platform
UNAME=$(uname | tr "[:upper:]" "[:lower:]")
# If Linux, try to determine specific distribution
if [ "$UNAME" == "linux" ]; then
    # If available, use LSB to identify distribution
    if [ -f /etc/lsb-release -o -d /etc/lsb-release.d ]; then
        export DISTRO=$(lsb_release -i | cut -d: -f2 | sed s/'^\t'//)
    # Otherwise, use release info file
    else
        export DISTRO=$(ls -d /etc/[A-Za-z]*[_-][rv]e[lr]* | grep -v "lsb" | cut -d'/' -f3 | cut -d'-' -f1 | cut -d'_' -f1)
    fi
fi

# For everything else (or if above failed), just use generic identifier
[ "$DISTRO" == "" ] && export DISTRO=$UNAME
unset UNAME

echo "Installing build dependencies..."

if [ "$DISTRO" == "Ubuntu" ]; then
    sudo apt-get install libx11-dev \
                         clang \
                         g++ \
                         libxcomposite-dev \
                         libxi-dev \
                         libxcursor-dev \
                         libgl-dev \
                         libasound2-dev
else
    echo "Currently unsupported distro.  Please install:"
    echo "  libx11-dev "
    echo "  clang "
    echo "  g++ "
    echo "  libx11-dev "
    echo "  libxcomposite-dev "
    echo "  libxcursor-dev "
    echo "  libgl-dev "
    echo "  libasound2-dev"
    echo ""
    echo "Thanks :)"
fi