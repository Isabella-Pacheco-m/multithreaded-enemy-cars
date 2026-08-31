/**
 * CarNetwork
 * Reads enemy cars from the backend over WebSocket.
 * Each message looks like:
 *   {"tick":123,"design":2,"cars":[{"id":7,"lane":1,"y":320,"variant":3}]}
 * We keep the latest list; game.js reads it every frame.
 */
class CarNetwork {
  constructor(url = GameConfig.BACKEND_URL) {
    this.url = url;
    this.cars = [];
    this.design = 0;   // which threading model the backend is running
    this.connect();
  }

  connect() {
    this.socket = new WebSocket(this.url);

    this.socket.onmessage = (event) => {
      const message = JSON.parse(event.data);
      this.cars = message.cars || [];
      this.design = message.design || 0;
    };

    // If the connection drops, clear the cars and try again in 1 second.
    this.socket.onclose = () => {
      this.cars = [];
      setTimeout(() => this.connect(), 1000);
    };
  }

  getSnapshot() {
    return this.cars;
  }
}
