# Enemy Cars — Multi-threading & Shared-Memory (MP1_2026)

A client–server "Enemy Cars" game for the Parallel Programming micro-project.
The **C++ backend** generates enemy cars and moves them with threads; the
**PixiJS frontend** only draws them and runs the local player + score.

```
backend (C++ threads)  --  ws://localhost:5000  -->  frontend (PixiJS)
  spawns + moves enemy cars      JSON snapshots       draws them, local player + collision
```

**This branch runs one threading design.** Each design lives on its own branch
(`main` = design 2). See *Branch layout* below.

## Run it

```bash
docker compose up --build
```

- Frontend: <http://localhost:8080>
  - LEFT / RIGHT change lane, UP / DOWN move, SPACE to restart
  - the panel top-left shows which threading model this branch is running
- Backend: `ws://localhost:5000`

> If port 5000 is busy on your host, change the backend port in `docker-compose.yml`
> (e.g. `"5050:5000"`) and `GameConfig.BACKEND_URL` in `frontend/scripts/config.js`.

### Backend without Docker

```bash
cd backend
g++ -std=c++17 -O2 server.cpp car.cpp car_manager.cpp ws_server.cpp -o server -pthread
PORT=5000 ./server
wscat -c ws://localhost:5000     # watch the JSON frames (npm i -g wscat)
```

## Files

### Backend (`backend/`)

| File | Role | Same on every branch? |
|------|------|-----------------------|
| `car.h` / `car.cpp` | one enemy car: position + movement rules (`move`, `proposeLane`) | yes |
| `car_manager.h` / `car_manager.cpp` | owns the `cars` vector and the thread(s); the shared per-car logic is identical, the threading model is what changes | threading part differs |
| `protocol.h` | builds the JSON snapshot line | yes |
| `ws_server.h` / `ws_server.cpp` | minimal WebSocket server (server→client text only) | yes |
| `server.cpp` | `main`: build the world, `manager.start()`, broadcast a snapshot ~16×/s; `DESIGN_NUMBER` is a constant used only for the JSON `design` field | `DESIGN_NUMBER` differs |

`server.cpp` only calls `CarManager::start()`, `stop()` and `getCarStates()`.

Wire format (one text frame ~every 60 ms):

```json
{"tick":1234,"design":2,"cars":[{"id":7,"lane":1,"y":320,"variant":3}]}
```

`lane` ∈ {0,1,2}; `y` in screen pixels; `variant` = `CarVariant` (0..4);
`design` is this branch's number (for the frontend panel).

### Shared per-car logic (identical on all branches)

In `car_manager.cpp`: `stepCarUnlocked` (move + maybe change lane), `carAheadTooClose`
(gap in each lane, no rear-end crashes), `laneHasRoom`, `isTopmostInBand`,
`wouldCreateWallUnlocked` (a car in a band where all 3 lanes are taken holds a tick if it
is the one furthest back, so a gap always stays open). `proposeLane` (in `car.cpp`) only
moves one lane over, only above `MERGE_ZONE_Y`.

### Frontend (`frontend/`)

- `network.js` — `CarNetwork`: WebSocket client, keeps the latest snapshot.
- `car.js` — `EnemyCar`: draws a car; `applyServer(x,y)` + `interpolate()` slides it
  toward the backend position. `laneToX(lane)` in `game.js` turns a lane index into pixels.
- `game.js` — start / game-over screens, the player (locked to a lane, snaps left/right),
  collision with the player, score (+1 per car that leaves the screen), 1.5 s spawn
  protection at the start of a round, and the read-only design panel.

No power-ups. Player movement, collision and score are all local.

## Branch layout

| Branch | Threading model | Spawner |
|--------|-----------------|---------|
| `diseno-1` | one `std::thread` per car, fixed fleet, each respawns its own car | safe: one car at a time in a free lane |
| `main` / `diseno-2` | one thread updates every car | wave: up to 2 lanes, one left open |
| `diseno-3` | one thread per car color (5); color 0 also spawns/cleans | wave |
| `diseno-4` | pool of worker threads take car ids from a `std::queue` + a scheduler thread | wave |

Each branch keeps `car.{h,cpp}` and the shared per-car logic; only the threading part of
`car_manager.{h,cpp}` and `DESIGN_NUMBER` in `server.cpp` change. Fix a shared bug on one
branch, then `git cherry-pick` it to the others. For the report: `git checkout diseno-N`,
build, test, record.

## Report checklist (MP1_2026)

1. Can it use multiple cores? (1 & 4 yes; 2 & 3 serialise on `carsMutex`.)
2. How easy is it to add cars / how independent is each car?
3. What happens with thousands of cars? (1 = one thread each, collapses; 4 degrades; 2/3 bounded.)
4. Shared variables and where a race could happen? (`cars`, `nextCarId`, `waveCounter`; the queue in 4.)
5. Data structures per design? (see the table.)
