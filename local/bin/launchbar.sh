#!/usr/bin/env sh

# 1. Kill any stray polybar instances
pkill -q polybar

# 2. Wait until old instances are dead
while pgrep -u $UID -x polybar >/dev/null; do sleep 0.1; done

# 3. Wait for the X server to respond
while ! xprop -root >/dev/null 2>&1; do
    sleep 0.2
done

# 4. Give vxwm 2 full seconds to map its window title atoms
sleep 2.0

# 5. Launch your bar
polybar mybar &
