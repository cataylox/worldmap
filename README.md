# World Map Timezones

A native Linux desktop app in C that opens an `X11` window, creates an OpenGL context with `GLX`, and renders a textured 3D earth globe with live timezone labels and current local times.

This is designed to run on Linux desktops using Mesa as the system OpenGL implementation.

## Dependencies

On Debian/Ubuntu:

```bash
sudo apt install build-essential libx11-dev libgl1-mesa-dev libglu1-mesa-dev mesa-common-dev
```

## Build

```bash
make
```

## Run

```bash
./world-map
```

Controls:

- `Esc` or `q`: quit
- Left mouse drag: rotate the globe east/west
- Resize the window freely; the renderer adapts automatically

## What it shows

- A generated earth texture mapped onto a 3D sphere
- Longitude/latitude grid lines wrapped around the globe
- Curved timezone labels and meridians that rotate with the globe
- Mouse-driven longitudinal rotation
- A real-time daylight/night texture update across the earth

## Data source

The earth texture is generated from Natural Earth `ne_110m_land` data, which is public domain.
