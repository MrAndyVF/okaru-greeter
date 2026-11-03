# OKARU Greeter (郁)

**Español** · [English](#english)

Pantalla de inicio de sesión para [greetd](https://github.com/kennylevinsen/greetd), con el mismo lenguaje
visual de [OKARU](https://github.com/MrAndyVF/okaru): elegir usuario y sesión de Wayland, escribir la
contraseña (o usar la huella) y sincronizar fondo, paleta y monitores desde la shell (opcional).

greetd ejecuta `okaru-greeter-session`, que arranca un compositor wlroots incluido y corre el greeter dentro.

## Instalar

Solo se da soporte a Arch Linux y CachyOS. Con el paquete local de este repositorio:

```sh
cd packaging/arch && makepkg -si
```

Configuración de greetd y del greeter en [docs/user](docs/user/index.md) (en inglés).

## Atribución

OKARU Greeter es un proyecto independiente creado y mantenido por MrAndyVF. Parte de su código base proviene
de [Noctalia Greeter](https://github.com/noctalia-dev/noctalia-greeter) (licencia MIT); su aviso de copyright
se conserva en [LICENSE](LICENSE). El código de Wuffs incluido en `third_party/` se distribuye bajo
Apache-2.0 o MIT.

---

## English

Login screen for [greetd](https://github.com/kennylevinsen/greetd) with the same visual language as
[OKARU](https://github.com/MrAndyVF/okaru): pick a user and a Wayland session, enter your password (or use
your fingerprint) and optionally sync wallpaper, palette and monitors from the shell.

greetd runs `okaru-greeter-session`, which starts a bundled wlroots compositor and runs the greeter inside it.

### Install

Only Arch Linux and CachyOS are supported. With the local package in this repository:

```sh
cd packaging/arch && makepkg -si
```

greetd and greeter configuration in [docs/user](docs/user/index.md).

### Attribution

OKARU Greeter is an independent project created and maintained by MrAndyVF. Part of its code base comes from
[Noctalia Greeter](https://github.com/noctalia-dev/noctalia-greeter) (MIT license); its copyright notice is
kept in [LICENSE](LICENSE). The Wuffs code bundled in `third_party/` is available under Apache-2.0 or MIT.
