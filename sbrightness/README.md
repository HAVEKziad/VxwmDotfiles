# sbrightness

Brightness control for internal and external monitors 

## Usage

```sh
sbrightness <output> <raise|lower|get> [step]
```

`output` is the DRM connector name: `eDP-1`, `HDMI-A-1`, `DP-2`, and so on.
`step` is a brightness percentage, defaulting to 5 if you can't be bothered to specify one.

```sh
sbrightness eDP-1 raise        # internal panel, +5%
sbrightness HDMI-A-1 lower 10  # external monitor, -10%
sbrightness DP-2 get           # current brightness percentage
```

`raise` and `lower` print the resulting brightness percentage to stdout, so callers can read the new value without a separate `get` call.

## Building

```sh
make
make install  # installs to /usr/local/bin by default
```

(Requires `make` and a C compiler)

> Note: it statically links on musl and dynamically links on glibc by default

## External monitor permissions

Your user needs access to `/dev/i2c-*`. Add yourself to the `i2c` group:

```sh
# Linux (most distros)
doas/sudo usermod -aG i2c $USER
```

Without this, `sbrightness` will report `cannot open /dev/i2c-N: Permission denied`

## License

GPL-v3
