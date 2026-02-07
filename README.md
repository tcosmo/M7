# ScoreTracker

Uses MuseScore (hash f17ef59fa2a2fb216b11cc865cdb76545610ed58)

## Build

### One-time configure

```bash
# Debug (default)
cmake -B build/debug -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)

# Release
cmake -B build/release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)
```

### Build (incremental)

```bash
cmake --build build/debug
# or
cmake --build build/release
```

### Run

```bash
./build/debug/scoretracker <musicxml> <audio>
```
