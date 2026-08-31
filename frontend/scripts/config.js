// Game configuration.

const GameConfig = {
  GAME_WIDTH: 640,
  GAME_HEIGHT: 840,

  // Enemy cars come from the C++ backend over WebSocket.
  BACKEND_URL: `ws://${location.hostname || 'localhost'}:5000`,

  GAME_SPEED: 4,       // road scroll speed
  PLAYER_SPEED: 3,     // player car movement speed
  LANES: 3,            // must match NUM_LANES in the backend

  BACKGROUND_COLOR: 0x404040,
  ROAD_COLOR: 0x8a8a8a,
  RESOLUTION: 2,
};

const Keys = {
  ARROW_LEFT: 'ArrowLeft',
  ARROW_UP: 'ArrowUp',
  ARROW_RIGHT: 'ArrowRight',
  ARROW_DOWN: 'ArrowDown',
  SPACE: 'Space',
  KEY_B: 'KeyB',
  KEY_M: 'KeyM',
};
