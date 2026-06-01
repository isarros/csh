# CSH for Windows

## Overview
CSP - Custom commands for Windows. Refer to https://github.com/GATE-Space/csp-playground/blob/csp-playground-windows-v2/README.md for a full usage example.

## Install the dependencies
```
sudo apt update
sudo apt install -y pipx ninja-build
pipx ensurepath
export PATH="$HOME/.local/bin:$PATH"
pipx install meson

sudo apt install -y libcurl4-openssl-dev cmake pkg-config

sudo apt install -y libzmq3-dev

sudo apt install libsocketcan-dev can-utils pkg-config
```

## Clone the repo and checkout the branch
```
git clone --recurse-submodules https://github.com/isarros/csh.git
cd csh
git checkout csh-for-windows
git submodule update --init --recursive

./configure
./install
```