---
title: Building from source
description: Build and install OKARU Greeter on distributions without a package.
sidebar:
  order: 1.5
---

Use this route when your distribution does not provide a OKARU Greeter
package. Install the build dependencies for your distribution, then build from
a cloned OKARU Greeter checkout.

## Build dependencies

### Arch Linux / CachyOS

OKARU Greeter only supports Arch Linux and CachyOS.

```sh
sudo pacman -S --needed base-devel meson just \
  greetd dbus \
  wayland wayland-protocols wlroots0.20 libinput \
  libglvnd freetype2 fontconfig \
  cairo pango harfbuzz \
  libxkbcommon glib2 \
  tomlplusplus nlohmann-json stb \
  libwebp libjxl librsvg libxml2
```

## Build and install

From a cloned OKARU Greeter checkout, build and install a release under the
system prefix:

```sh
meson setup build-release --prefix=/usr --buildtype=release
just build-release
sudo meson install -C build-release
```

After configuring greetd's session user, initialize greeter state with the
installed helper:

```sh
sudo okaru-greeter-apply-appearance --setup-system
```

See [state initialization](installation.md#initialize-greeter-state) for
directory ownership and tmpfiles overrides for custom greeter accounts.

The portable setup script also prepares
`/var/lib/okaru-greeter/`, `greeter.toml`, and `sync.toml` for the greetd user
and adjusts greetd's PAM stack for the detected systemd or elogind runtime
module. Run it when that PAM adjustment is appropriate for your distribution:

```sh
sudo ./scripts/setup_greeter_system.sh
```

The state helper above also works on systems without tmpfiles processing,
where it creates the directory as well. Set `GREETER_USER` explicitly if its
existing owner differs from the account you intend to use.

Continue with [Configure greetd](installation.md#3-configure-greetd) to merge
the installed session wrapper into your greetd configuration and complete the
safe display-manager cutover.
