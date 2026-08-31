/**
 * Game logic.
 * The player car is local. The enemy cars come from the C++ backend over
 * WebSocket (see network.js); here we just draw them and check if one hits
 * the player.
 */

const keyboard = new KeyBoard().addEvents();
const audioManager = new AudioManager();
const network = new CarNetwork();

// After a round starts, ignore collisions for this long so the player is not
// killed instantly by traffic that was already on the road.
const SPAWN_PROTECTION_MS = 1500;

window.onload = function () {
  const width = GameConfig.GAME_WIDTH;
  const height = GameConfig.GAME_HEIGHT;

  const app = new PIXI.Application({
    width: width,
    height: height,
    backgroundColor: GameConfig.BACKGROUND_COLOR,
    resolution: GameConfig.RESOLUTION,
  });

  app.loader.add('player', 'assets/BlackOut.png');
  app.loader.add('enemy1', 'assets/RedStrip.png');
  app.loader.add('enemy2', 'assets/BlueStrip.png');
  app.loader.add('enemy3', 'assets/GreenStrip.png');
  app.loader.add('enemy4', 'assets/PinkStrip.png');
  app.loader.add('enemy5', 'assets/WhiteStrip.png');
  for (let i = 0; i <= 63; i++) {
    app.loader.add(`exp-${i}`, `assets/explosion/frame00${(i < 10 ? '0' : '')}${i}.png`);
  }

  app.loader.onComplete.add(startGame);
  app.loader.load();

  function startGame() {
    app.stage.sortableChildren = true;
    document.getElementById('game').appendChild(app.view);

    const scenario = new GameBackground(width, height, GameConfig.GAME_SPEED, GameConfig.LANES);
    app.stage.addChild(scenario.container);

    // The backend sends a lane index (0..LANES-1); turn it into the pixel X
    // of that lane's centre.
    function laneToX(lane) {
      const roadWidth = scenario.xRoadEnd - scenario.xRoadStart;
      return scenario.xRoadStart + (roadWidth / GameConfig.LANES) * (lane + 0.5);
    }

    // --- HUD ---
    const scoreText = makeText('SCORE: 0', 24, 0xFFFFFF);
    scoreText.x = 20;
    scoreText.y = 20;
    scoreText.zIndex = 100;
    app.stage.addChild(scoreText);

    const fpsText = makeText('FPS: 0', 12, 0xFFFFFF);
    fpsText.x = 20;
    fpsText.y = 54;
    fpsText.zIndex = 100;
    fpsText.visible = false;   // toggle with B
    app.stage.addChild(fpsText);

    const muteText = makeText('[M] MUTED', 14, 0xFF0000);
    muteText.x = 20;
    muteText.y = height - 30;
    muteText.zIndex = 100;
    muteText.visible = false;
    app.stage.addChild(muteText);

    // --- Player ---
    const playerCar = new PlayerCar(app, scenario.xRoadStart, scenario.xRoadEnd, height, GameConfig.PLAYER_SPEED);
    playerCar.setPosition(width / 2, height - 120);
    playerCar.sprite.zIndex = 10;
    playerCar.explosion.zIndex = 20;
    app.stage.addChild(playerCar.sprite);
    app.stage.addChild(playerCar.explosion);

    // --- Start screen ---
    const startScreen = makeOverlay(width, height);
    startScreen.zIndex = 300;
    const title = makeText('PP RACING', 40, 0xffea00);
    centerX(title, width);
    title.y = 140;
    startScreen.addChild(title);
    const startHint = makeText('Press an arrow key to start', 20, 0xFFFFFF);
    centerX(startHint, width);
    startHint.y = 300;
    startScreen.addChild(startHint);
    const controls = makeText('LEFT / RIGHT change lane, UP / DOWN move', 16, 0xCCCCCC);
    centerX(controls, width);
    controls.y = 350;
    startScreen.addChild(controls);
    app.stage.addChild(startScreen);

    // --- Game over screen ---
    const gameOverScreen = makeOverlay(width, height);
    gameOverScreen.zIndex = 300;
    const gameOverText = makeText('GAME OVER', 52, 0xFF0000);
    centerX(gameOverText, width);
    gameOverText.y = 160;
    gameOverScreen.addChild(gameOverText);
    const finalScoreText = makeText('SCORE: 0', 32, 0xFFFF00);
    finalScoreText.y = 260;
    gameOverScreen.addChild(finalScoreText);
    const againText = makeText('Press SPACE to play again', 20, 0x00FF00);
    centerX(againText, width);
    againText.y = 340;
    gameOverScreen.addChild(againText);
    gameOverScreen.visible = false;
    app.stage.addChild(gameOverScreen);

    // --- State ---
    let started = false;
    let lost = false;
    let score = 0;
    let roundStartAt = 0;
    let playerLane = 1;   // player is locked to a lane (0..2), no in-between
    let enemyCars = [];   // { ...EnemyCar, serverId }

    function movePlayerToLane(lane) {
      playerLane = Math.max(0, Math.min(GameConfig.LANES - 1, lane));
      playerCar.setPosition(laneToX(playerLane), playerCar.sprite.y);
    }

    function startRound() {
      enemyCars.forEach(e => app.stage.removeChild(e.sprite));
      enemyCars = [];
      score = 0;
      scoreText.text = 'SCORE: 0';
      lost = false;
      started = true;
      roundStartAt = Date.now();
      startScreen.visible = false;
      gameOverScreen.visible = false;
      playerCar.setPosition(laneToX(1), height - 120);
      playerLane = 1;
      playerCar.explosion.visible = false;
      playerCar.explosion.gotoAndStop(0);
      audioManager.startBackgroundMusic();
    }

    // --- Input ---
    window.addEventListener('keydown', (event) => {
      const arrows = [Keys.ARROW_UP, Keys.ARROW_DOWN, Keys.ARROW_LEFT, Keys.ARROW_RIGHT];
      if (!started && arrows.includes(event.code)) {
        startRound();
      } else if (lost && event.code === Keys.SPACE) {
        startRound();
      } else if (started && !lost && event.code === Keys.ARROW_LEFT) {
        movePlayerToLane(playerLane - 1);
      } else if (started && !lost && event.code === Keys.ARROW_RIGHT) {
        movePlayerToLane(playerLane + 1);
      } else if (event.code === Keys.KEY_B) {
        fpsText.visible = !fpsText.visible;
      } else if (event.code === Keys.KEY_M) {
        muteText.visible = audioManager.toggleMute();
      }
    });

    // --- Design panel: shows which threading model the backend is running ---
    const designRows = document.querySelectorAll('#designbar .opt');

    // --- Enemy cars: match sprites to the backend snapshot ---
    function syncEnemyCars() {
      const snapshot = network.getSnapshot();
      const liveIds = new Set();

      for (let i = 0; i < snapshot.length; i++) {
        const sc = snapshot[i];
        liveIds.add(sc.id);

        let ec = enemyCars.find(e => e.serverId === sc.id);
        if (!ec) {
          ec = new EnemyCar(app, scenario.xRoadStart, scenario.xRoadEnd, height, GameConfig.GAME_SPEED, sc.variant);
          ec.serverId = sc.id;
          ec.sprite.zIndex = 5;
          enemyCars.push(ec);
          app.stage.addChild(ec.sprite);
        }
        ec.applyServer(laneToX(sc.lane), sc.y);
      }

      // An id the backend stopped sending = that car left the screen = evaded.
      for (let i = enemyCars.length - 1; i >= 0; i--) {
        if (!liveIds.has(enemyCars[i].serverId)) {
          app.stage.removeChild(enemyCars[i].sprite);
          enemyCars.splice(i, 1);
          if (!lost) {
            score++;
            scoreText.text = 'SCORE: ' + score;
          }
        }
      }
    }

    // Overlap of two bounding boxes (getBounds returns top-left x/y + size).
    function hits(a, b) {
      return a.x < b.x + b.width &&
             a.x + a.width > b.x &&
             a.y < b.y + b.height &&
             a.y + a.height > b.y;
    }

    // --- Main loop ---
    app.ticker.add(() => {
      fpsText.text = 'FPS: ' + Math.ceil(PIXI.Ticker.shared.FPS);

      // Highlight the design the backend is actually running (from the stream).
      designRows.forEach(row => {
        row.classList.toggle('active', Number(row.dataset.design) === network.design);
      });

      if (!started) {
        startHint.alpha = 0.5 + 0.5 * Math.sin(Date.now() / 200);
        return;
      }

      scenario.animate();
      syncEnemyCars();

      // Up/down is free movement; left/right jumps between lanes (in the keydown handler).
      if (!lost) {
        if (keyboard.isKeyPress(Keys.ARROW_UP)) playerCar.moveUp();
        if (keyboard.isKeyPress(Keys.ARROW_DOWN)) playerCar.moveDown();
      }

      const protecting = Date.now() - roundStartAt < SPAWN_PROTECTION_MS;
      const playerBox = playerCar.sprite.getBounds();

      for (let i = 0; i < enemyCars.length; i++) {
        enemyCars[i].interpolate();

        if (!lost && !protecting && hits(enemyCars[i].sprite.getBounds(), playerBox)) {
          lost = true;
          playerCar.explode();
          audioManager.playCollision();
          audioManager.stopBackgroundMusic();
          finalScoreText.text = 'SCORE: ' + score;
          centerX(finalScoreText, width);
          gameOverScreen.visible = true;
        }
      }
    });
  }
};

// --- small UI helpers ---

function makeText(str, size, color) {
  return new PIXI.Text(str, {
    fontFamily: 'Arial',
    fontSize: size,
    fill: color,
    stroke: 'black',
    strokeThickness: 4,
  });
}

function centerX(node, width) {
  node.x = (width - node.width) / 2;
}

function makeOverlay(width, height) {
  const c = new PIXI.Container();
  const bg = new PIXI.Graphics();
  bg.beginFill(0x000000, 0.8);
  bg.drawRect(0, 0, width, height);
  bg.endFill();
  c.addChild(bg);
  return c;
}
