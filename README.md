# ScoreTracker

Uses MuseScore (hash f17ef59fa2a2fb216b11cc865cdb76545610ed58)

## Build

### Debug (default)

```bash
cmake -B build/debug -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)
cmake --build build/debug
./build/debug/scoretracker <musicxml> <audio>
```

### Release

```bash
cmake -B build/release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)
cmake --build build/release
./build/release/scoretracker <musicxml> <audio>
```
