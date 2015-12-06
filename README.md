# posixread

Do POSIX read on files and sockets with Node.js.

[![Build Status](https://travis-ci.org/adrienverge/posixread.svg?branch=master)](https://travis-ci.org/adrienverge/posixread)

## Motivation

In Node.js, reading from sockets is performed using `uv_read_start()` from
`libuv`. This consumes data from the file descriptor whenever it is available,
but consumes it all. There is not built-in way to read a fixed amount, let's say
`n` bytes.

This is a problem when you want to read *only* `n` bytes and leave the rest in
the socket (so that, for instance, another process reads remaining data).

posixread is a module to perform a POSIX read on a socket. That way, only `n`
bytes are brought up to user-space and the rest remains in kernel-space, ready
to be `read(2)` by any other process.

## Usage

### Warning

For posixread to work, your code must prevent `libuv` to start reading from the
socket. That means the socket must have the `pauseOnCreate` property.

In practice: if you get the socket from a `net.Server`, this server has to be
created with the `pauseOnConnect` set to `true`.

### Example

```js
const net = require('net');
const posixread = require('posixread');

const server = net.createServer({ pauseOnConnect: true }, function (socket) {
    // Just got an incoming connection. Let's read 10 bytes from it (but DO NOT
    // consume more than 10 bytes from the socket).
    posixread.read(socket, 10, function (err, buffer) {
        if (err)
            return process.stderr.write('error: ' + err);

        return process.stdout.write('10 first bytes: ' + buffer);
    });
}).listen(1234);
```

## License

MIT license
