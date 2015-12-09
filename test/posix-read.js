const assert = require('assert');
const crypto = require('crypto');
const net = require('net');

const posixRead = require('../index');

describe('posixRead()', () => {
    it('should detect non-socket objects (undefined)', (done) => {
        try {
            posixRead(undefined, 10, () => {});
            done(new Error('error not thrown'));
        } catch (err) {
            if (err instanceof TypeError
                    && err.message === 'first argument should be a socket')
                return done();
            return done(err);
        }
    });

    it('should detect non-socket objects (number)', (done) => {
        try {
            posixRead(42, 10, () => {});
            done(new Error('error not thrown'));
        } catch (err) {
            if (err instanceof TypeError
                    && err.message === 'first argument should be a socket')
                return done();
            return done(err);
        }
    });

    it('should detect non-socket objects (fake socket)', (done) => {
        try {
            const socket = { _handle: { fd: 2 }, readable: true };
            posixRead(socket, 10, () => {});
            done(new Error('error not thrown'));
        } catch (err) {
            if (err instanceof TypeError
                    && err.message === 'first argument should be a socket')
                return done();
            return done(err);
        }
    });

    it('should detect bad socket (not readable)', (done) => {
        const socket = new net.Socket();
        posixRead(socket, 10, (err) => {
            if (!err)
                return done(new Error('error not thrown'));
            if (err.badStream !== true
                    || err.message !== 'socket is not readable')
                return done(err);
            return done();
        });
    });

    function getNewSocket(callback) {
        const otherEnd = new net.Socket();

        const server = net.createServer(
            { pauseOnConnect: true },
            function onConnection(socket) {
                server.close();
                callback(socket, otherEnd);
                // otherEnd.end();
                // otherEnd.destroy();
            });

        server.listen(function onListening() {
            otherEnd.connect(server.address().port);
        });
    }

    it('should detect bad socket (invalid handle)', (done) => {
        getNewSocket(function onSocket(socket) {
            socket._handle = {};
            posixRead(socket, 10, (err) => {
                if (!err)
                    return done(new Error('error not thrown'));
                if (err.badStream !== true
                        || err.message !== 'malformed socket object, cannot ' +
                                           'get file descriptor')
                    return done(err);
                return done();
            });
        });
    });

    it('should detect bad requested size (wrong type)', (done) => {
        getNewSocket(function onSocket(socket) {
            try {
                posixRead(socket, 'test', () => {});
                done(new Error('error not thrown'));
            } catch (err) {
                if (err instanceof TypeError
                        && err.message === 'second argument should be a ' +
                                           'positive integer')
                    return done();
                return done(err);
            }
        });
    });

    it('should detect bad requested size (negative)', (done) => {
        getNewSocket(function onSocket(socket) {
            try {
                posixRead(socket, -2, () => {});
                done(new Error('error not thrown'));
            } catch (err) {
                if (err instanceof TypeError
                        && err.message === 'second argument should be a ' +
                                           'positive integer')
                    return done();
                return done(err);
            }
        });
    });

    it('should detect bad callback', (done) => {
        getNewSocket(function onSocket(socket) {
            try {
                posixRead(socket, 10, 'callback');
                done(new Error('error not thrown'));
            } catch (err) {
                if (err instanceof TypeError
                        && err.message === 'third argument should be a ' +
                                           'function')
                    return done();
                return done(err);
            }
        });
    });

    it('should read 10 bytes', (done) => {
        getNewSocket(function onSocket(socket, otherEnd) {
            otherEnd.write('ABCDEFGHIJKLMNOPQRSTUVWXYZ', () => {
                posixRead(socket, 10, (err, buffer) => {
                    if (err)
                        return done(err);

                    assert.deepStrictEqual(buffer, new Buffer('ABCDEFGHIJ'));
                    done();
                });
            });
        });
    });

    it('should read 90000 bytes', (done) => {
        getNewSocket(function onSocket(socket, otherEnd) {
            const bigBuf = crypto.randomBytes(99999);
            otherEnd.write(bigBuf, () => {
                posixRead(socket, 90000, (err, buffer) => {
                    if (err)
                        return done(err);

                    assert.deepStrictEqual(buffer, bigBuf.slice(0, 90000));
                    done();
                });
            });
        });
    });

    it('should detect end of stream before having read all', (done) => {
        getNewSocket(function onSocket(socket, otherEnd) {
            otherEnd.end('123456789', () => {
                posixRead(socket, 10, (err) => {
                    if (!err)
                        return done(new Error('error not thrown'));
                    if (err.endOfFile !== true
                            || err.message !== 'reached end of stream (read ' +
                                               '9 bytes)')
                        return done(err);

                    done();
                });
            });
        });
    });

    it('should wait for data to be available', (done) => {
        getNewSocket(function onSocket(socket, otherEnd) {
            posixRead(socket, 10, (err, buffer) => {
                if (err)
                    return done(err);

                assert.deepStrictEqual(buffer, new Buffer('ABCDEFGHIJ'));
                done();
            });
            setTimeout(() => {
                otherEnd.write('ABCDEFGHIJKLMNOPQRSTUVWXYZ');
            }, 10);
        });
    });

    it('should accept data sent by chunks', (done) => {
        getNewSocket(function onSocket(socket, otherEnd) {
            posixRead(socket, 25, (err, buffer) => {
                if (err)
                    return done(err);

                assert.deepStrictEqual(
                    buffer, new Buffer('----------0123456789ABCDE'));
                done();
            });
            otherEnd.write('----------');
            setTimeout(() => {
                otherEnd.write('0123456789');
            }, 10);
            setTimeout(() => {
                otherEnd.write('ABCDEFGHIJKLMNOPQRSTUVWXYZ');
            }, 20);
        });
    });
});
