#!/bin/sh

WALLPAPER_DIR="$HOME/Pictures/Wallpapers/"

if [ ! -d "$WALLPAPER_DIR" ]; then
  echo "Directory $WALLPAPER_DIR not found."
  exit 1
fi

while true; do
  RANDOM_WALL=$(find "$WALLPAPER_DIR" -type f | shuf -n 1)
  
  if [ -n "$RANDOM_WALL" ]; then
    feh --bg-fil "$RANDOM_WALL"
  fi
  sleep 600
done
