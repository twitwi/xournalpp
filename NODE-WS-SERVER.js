const WebSocket = require('ws');
const server = new WebSocket.Server({
  port: 14444
});

let sockets = [];
server.on('connection', function(socket) {
  console.log("New Client!")
  sockets.push(socket);

  // When you receive a message, send that message to every socket.
  socket.on('message', function(msg) {
    sockets.forEach(s => {if (socket !== s) s.send(msg)});
  });

  // When a socket closes, or disconnects, remove it from the array.
  socket.on('close', function() {
    sockets = sockets.filter(s => s !== socket);
  });
});
