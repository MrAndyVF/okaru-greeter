---
title: Installation
description: Install OKARU Greeter and connect it to greetd.
sidebar:
  order: 1
---
import { Tabs, TabItem } from '@astrojs/starlight/components';

Install OKARU Greeter, point greetd at the installed session wrapper, and then
switch display managers. The steps below put those changes in the order they
should be made.

## 1. Check prerequisites

Every installation needs **greetd** and **D-Bus** on the machine where the
greeter runs. Install desktop sessions such as niri or Hyprland separately.

Sections 3–6 assume systemd.



## 2. Install OKARU Greeter

OKARU Greeter only supports Arch Linux and CachyOS. Build the local package from this repository:

```sh
cd packaging/arch && makepkg -si
```

## 3. Configure greetd


greetd must launch **`okaru-greeter-session`**, not the
`okaru-greeter` executable. Find the wrapper installed on your system:

```sh
command -v okaru-greeter-session
```

Open `/etc/greetd/config.toml` and merge the following values into its existing
`[default_session]` section. Do not blindly overwrite an existing greetd
configuration:

```toml
[default_session]
command = "/usr/bin/okaru-greeter-session"
user = "greeter"
```

Replace `command` with the full path reported above. Distribution packages
commonly install it under `/usr/bin`; the path may differ for another prefix.
Keep the existing greeter account if your configuration uses a user other than
`greeter`.

Some packages configure greetd automatically. In that case, verify that the
existing `command` points to `okaru-greeter-session` and leave the rest of
the configuration intact.

### Initialize greeter state

Some packages and declarative modules prepare greeter state automatically.
If yours does, verify its configured account and state directory. Otherwise,
initialize the state after configuring greetd's session user.

The shipped tmpfiles rule creates `/var/lib/okaru-greeter` with mode `0750`
and ownership `greeter:greeter` on systems that process it. If greetd uses a
different greeter account, create `/etc/tmpfiles.d/okaru-greeter.conf` with
this line, replacing `ACCOUNT` and `GROUP` with that account's name and primary
group:

```text
d /var/lib/okaru-greeter 0750 ACCOUNT GROUP -
```

On systemd, apply the override so the directory has the right owner now and
after reboot:

```sh
sudo systemd-tmpfiles --create okaru-greeter.conf
```

Initialize the configuration files, or migrate state from an older installation:

```sh
sudo okaru-greeter-apply-appearance --setup-system
```

This creates or updates `greeter.toml` and `sync.toml` without editing PAM.
It also creates the state directory on systems without tmpfiles processing.
When changing the greeter account, apply the tmpfiles override and run the
helper again. Existing state-directory ownership takes precedence over the
user in greetd's configuration when the helper resolves the account. Pass
`GREETER_USER` explicitly if the existing owner differs from the intended user:

```sh
sudo env GREETER_USER=ACCOUNT okaru-greeter-apply-appearance --setup-system
```

## 4. Replace the current display manager safely

:::caution
Changing display managers can stop the graphical login immediately. Before
continuing, switch to a TTY or keep another root-capable shell open so you can
recover if greetd does not start.
:::

On a systemd distribution, first identify the display manager currently behind
the `display-manager.service` alias:

```sh
systemctl status display-manager.service
```

Service unit names vary. The actual unit may be `gdm.service`, `sddm.service`,
`lightdm.service`, or something else. Disable the unit shown on your system;
do not copy one of these examples without checking:

```sh
sudo systemctl disable --now UNIT.service
```

If no other display manager is installed or enabled, there is nothing to
disable. Do not leave another display manager enabled alongside greetd: both
can compete for the login screen on the next boot.

## 5. Enable greetd

After the previous display manager is disabled, enable and start greetd:

```sh
sudo systemctl enable --now greetd
```

## 6. Reboot and verify

Reboot to confirm greetd starts cleanly:

```sh
sudo reboot
```

The OKARU Greeter login screen should appear and list the desktop sessions
installed on the system. If it does not, return to the TTY or root shell you
kept available and inspect greetd:

```sh
systemctl status greetd
```

See [Troubleshooting](troubleshooting.md) for recovery steps and logging
details.

## Optional next steps

- Install and enable **accountsservice** (usually the `accounts-daemon`
  service) to show user avatars. A fallback appears when it is unavailable or
  no `IconFile` is set.
- Adjust the greeter in [Configuration](configuration.md), including the
  initial desktop session or user.
- Set up optional appearance sharing in
  [Sync with OKARU](sync.md). That guide is the source of truth for
  compatibility, authorization, and security details.
- See [Displays](displays.md) and [Input and keyboard](input.md) for
  platform-specific customization.
