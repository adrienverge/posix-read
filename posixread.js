const net = require('net');
const posixread = require('./index');


const server = net.createServer({ pauseOnConnect: true }, function (socket) {
    // console.log(socket);
    // console.log(socket.read(10));
    console.log(posixread.read(socket, 10));
});

server.listen(1234, function() {
    console.log("kineticd: opened server on %j", 1234);
});

