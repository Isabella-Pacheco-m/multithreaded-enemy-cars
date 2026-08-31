FROM japeto/so-tools:v64

WORKDIR /app
COPY . .
RUN g++ -std=c++17 -O2 server.cpp car.cpp car_manager.cpp ws_server.cpp -o server -pthread
EXPOSE 5000

CMD ["./server"]
