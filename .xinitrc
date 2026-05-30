#!/bin/sh
gentoo-pipewire-launcher &
~/.local/bin/wall.sh &
picom &
~/.local/bin/launchbar.sh &
exec vxwm
