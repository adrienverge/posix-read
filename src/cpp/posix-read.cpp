/*
 * Copyright (c) 2015 Adrien Vergé
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <nan.h>

/*
 * Try to check a given argument is actually a `net.Socket` instance. This is
 * not a strict check and can be easily be fooled. But at least, it should
 * prevent some trivial programming errors.
 */
static bool LooksLikeASocket(v8::Local<v8::Value> object) {
    if (!object->IsObject())
        return false;
    v8::Local<v8::Object> socket = object.As<v8::Object>();

    v8::Local<v8::String> className = socket->GetConstructorName();
    if (strcmp("Socket", *Nan::Utf8String(className->ToString())))
        return false;

    return true;
}

/*
 * Checks if the socket has the 'readable' property set to true.
 */
static bool SocketIsReadable(v8::Local<v8::Object> socket) {
    v8::Local<v8::String> key =
            Nan::New<v8::String>("readable").ToLocalChecked();

    if (!socket->Has(key))
        return false;

    v8::Local<v8::Value> value = socket->Get(key);
    return value->IsBoolean() && Nan::To<bool>(value).FromJust();
}

/*
 * Make sure the passed socket has a TCP handle with an associated fd. Returns
 * the fd on success, or -1 in case of error.
 */
static int GetFdFromSocket(v8::Local<v8::Object> socket) {
    v8::Local<v8::String> key;
    v8::Local<v8::Object> handle;
    v8::Local<v8::Value> value;
    v8::Local<v8::String> className;

    key = Nan::New<v8::String>("_handle").ToLocalChecked();
    if (!socket->Has(key))
        return -1;

    value = socket->Get(key);
    if (!value->IsObject())
        return -1;
    handle = value.As<v8::Object>();

    className = handle->GetConstructorName();
    if (strcmp("TCP", *Nan::Utf8String(className->ToString())))
        return -1;

    key = Nan::New<v8::String>("fd").ToLocalChecked();
    if (!handle->Has(key))
        return -1;

    value = handle->Get(key);
    if (!value->IsNumber())
        return -1;

    int fd = Nan::To<int>(value).FromJust();

    if (fd < 0)
        return -1;

    return fd;
}

/*
 * Equivalent of:
 *
 * const err = new Error(message);
 * err[property] = true;
 */
static v8::Local<v8::Value> ErrorWithProperty(const char *property,
                                              const char *message) {
    v8::Local<v8::Value> error = Nan::Error(message);

    v8::Local<v8::String> key = Nan::New<v8::String>(property)
            .ToLocalChecked();
    error.As<v8::Object>()->Set(key, Nan::True());

    return error;
}

class PosixReadWorker : public Nan::AsyncWorker {
 private:
    int fd;
    bool fd_was_non_blocking;

    size_t size;
    char *data;

    const char *error_prop = NULL;

    /*
     * Set the socket blocking, if it was not.
     */
    int SetBlocking() {
        int opts = fcntl(fd, F_GETFL);
        if (opts == -1)
            return -1;

        fd_was_non_blocking = opts & O_NONBLOCK;

        if (fd_was_non_blocking) {
            opts &= ~O_NONBLOCK;
            if (fcntl(fd, F_SETFL, opts) == -1)
                return -1;
        }

        return 0;
    }

    /*
     * Reset the socket like in the mode (blocking vs. non-blocking) it was.
     */
    int UnsetBlocking() {
        if (fd_was_non_blocking) {
            int opts = fcntl(fd, F_GETFL);
            if (opts == -1)
                return -1;

            opts |= O_NONBLOCK;
            if (fcntl(fd, F_SETFL, opts) == -1)
                return -1;
        }

        return 0;
    }

 public:
    PosixReadWorker(Nan::Callback *callback, int fd, size_t size)
            : Nan::AsyncWorker(callback), fd(fd), size(size) { }

    ~PosixReadWorker() {}

    /*
     * Executed inside the worker-thread. It is not safe to access V8, or V8
     * data structures here, so everything we need for input and output should
     * go on `this`.
     */
    void Execute() {
        static char msg[256];
        size_t count = 0;

        data = reinterpret_cast<char *>(malloc(size));
        if (data == NULL) {
            error_prop = "systemError";
            snprintf(msg, sizeof(msg), "malloc failed: %s", strerror(errno));
            SetErrorMessage(msg);
            return;
        }

        if (SetBlocking()) {
            error_prop = "systemError";
            snprintf(msg, sizeof(msg), "fnctl failed: %s", strerror(errno));
            SetErrorMessage(msg);
            free(data);
            return;
        }

        do {
            ssize_t n = read(fd, &data[count], size - count);
            if (n == -1) {
                if (errno == EINTR)
                    continue;

                error_prop = "systemError";
                snprintf(msg, sizeof(msg), "read failed: %s", strerror(errno));
                SetErrorMessage(msg);
                free(data);
                break;
            } else if (n == 0) {  // end of stream
                error_prop = "endOfFile";
                snprintf(msg, sizeof(msg),
                         "reached end of stream (read %lu bytes)", count);
                SetErrorMessage(msg);
                free(data);
                break;
            } else {
                count += n;
            }
        } while (count < size);

        if (UnsetBlocking()) {
            if (ErrorMessage() == NULL) {
                error_prop = "systemError";
                snprintf(msg, sizeof(msg), "fnctl failed: %s", strerror(errno));
                SetErrorMessage(msg);
                free(data);
            }
        }
    }

    /*
     * Executed when the async work is complete this function will be run
     * inside the main event loop so it is safe to use V8 again.
     */
    void HandleOKCallback() {
        Nan::HandleScope scope;

        v8::Local<v8::Object> buffer =
                Nan::NewBuffer(data, (uint32_t) size).ToLocalChecked();

        v8::Local<v8::Value> argv[] = { Nan::Null(), buffer };
        callback->Call(2, argv);
    }

    void HandleErrorCallback() {
        Nan::HandleScope scope;

        v8::Local<v8::Value> argv[] = {
                ErrorWithProperty(error_prop, ErrorMessage()) };
        callback->Call(1, argv);
    }
};

void Read(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() != 3) {
        Nan::ThrowTypeError("wrong number of arguments");
        return;
    }

    /*
     * Get 'socket' argument.
     */
    if (!LooksLikeASocket(info[0])) {
        Nan::ThrowTypeError("first argument should be a socket");
        return;
    }
    v8::Local<v8::Object> socket = info[0].As<v8::Object>();

    /*
     * Get 'size' argument.
     */
    if (!info[1]->IsNumber() || Nan::To<int>(info[1]).FromJust() <= 0) {
        Nan::ThrowTypeError("second argument should be a positive integer");
        return;
    }
    size_t size = Nan::To<int>(info[1]).FromJust();

    /*
     * Get 'callback' argument.
     */
    if (!info[2]->IsFunction()) {
        Nan::ThrowTypeError("third argument should be a function");
        return;
    }
    Nan::Callback *callback = new Nan::Callback(info[2].As<v8::Function>());

    /*
     * Run-time checks. They don't throw (since these are not programmer errors)
     * but callback(error).
     */
    if (!SocketIsReadable(socket)) {
        v8::Local<v8::Value> argv[] = {
                ErrorWithProperty("badStream", "socket is not readable") };
        callback->Call(1, argv);
        return;
    }
    // Check if the 'socket' argument is well-formed and extract its file
    // descriptor.
    int fd = GetFdFromSocket(socket);
    if (fd == -1) {
        v8::Local<v8::Value> argv[] = { ErrorWithProperty(
                "badStream",
                "malformed socket object, cannot get file descriptor") };
        callback->Call(1, argv);
        return;
    }

    Nan::AsyncQueueWorker(new PosixReadWorker(callback, fd, size));
    return;
}

void Init(v8::Local<v8::Object> exports) {
    exports->Set(Nan::New("read").ToLocalChecked(),
                 Nan::New<v8::FunctionTemplate>(Read)->GetFunction());
}

NODE_MODULE(posixread, Init)
