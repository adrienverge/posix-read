#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <nan.h>

/*
 * Returns -1 if the first arg does not look like a net.Socket object
 *         -2 if the file descriptor is not valid
 *         -3 if the stream is not readable
 */
static int get_fd_from_socket(v8::Local<v8::Value> object) {
    v8::Local<v8::String> key;
    v8::Local<v8::Object> socket, handle;
    v8::Local<v8::Value> value;
    v8::Local<v8::String> className;

    /*
     * Make sure the passed object is a Socket, has a TCP handle, and is
     * readable.
     */
    if (!object->IsObject())
        return -1;
    socket = object.As<v8::Object>();

    className = socket->GetConstructorName();
    if (strcmp("Socket", *Nan::Utf8String(className->ToString()))) 
        return -1;

    key = Nan::New<v8::String>("readable").ToLocalChecked();
    if (!socket->Has(key))
        return -1;
    value = socket->Get(key);
    if (!value->IsBoolean() || !Nan::To<bool>(value).FromJust())
        return -3;

    key = Nan::New<v8::String>("_handle").ToLocalChecked();
    if (!socket->Has(key)) {
        Nan::ThrowTypeError("socket has no handle");
        return -4;
    }

    value = socket->Get(key);
    if (!value->IsObject()) {
        Nan::ThrowTypeError("socket has no handle");
        return -4;
    }
    handle = value.As<v8::Object>();

    className = handle->GetConstructorName();
    if (strcmp("TCP", *Nan::Utf8String(className->ToString()))) {
        Nan::ThrowTypeError("socket has no handle");
        return -4;
    }

    key = Nan::New<v8::String>("fd").ToLocalChecked();
    if (!handle->Has(key))
        return -2;

    value = handle->Get(key);
    if (!value->IsNumber())
        return -2;

    int fd = Nan::To<int>(value).FromJust();

    if (fd < 0)
        return -2;

    return fd;
}

void Read(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() < 2) {
        Nan::ThrowTypeError("wrong number of arguments");
        return;
    }

    int fd = get_fd_from_socket(info[0]);

    if (fd < 0) {
        if (fd == -1)
            Nan::ThrowTypeError("first argument should be a socket");
        else if (fd == -2)
            Nan::ThrowError("socket file descriptor is invalid");
        else if (fd == -3)
            Nan::ThrowError("socket is not readable");
        return;
    }

    if (!info[1]->IsNumber() || Nan::To<int>(info[1]).FromJust() <= 0) {
        Nan::ThrowTypeError("second argument should be a positive integer");
        return;
    }
    size_t size = Nan::To<int>(info[1]).FromJust();

    Nan::Callback *callback = new Nan::Callback(info[3].As<v8::Function>());

    // v8::MaybeLocal<v8::Object> buffer = Nan::NewBuffer((uint32_t) size);
    v8::Local<v8::Object> buffer = Nan::NewBuffer((uint32_t) size).ToLocalChecked();
    char *data = node::Buffer::Data(buffer);

    /*
     * Set the socket blocking
     */
    bool was_non_blocking = false;
    int opts = fcntl(fd, F_GETFL);
    if (opts < 0) {
        perror("fcntl(F_GETFL)");
        exit(EXIT_FAILURE);
    }
    if (opts & O_NONBLOCK) {
        was_non_blocking = true;
        opts &= ~O_NONBLOCK;
        if (fcntl(fd, F_SETFL, opts) < 0) {
            perror("fcntl(F_SETFL)");
            exit(EXIT_FAILURE);
        }
    }

    size_t count = 0;
    
    do {
        ssize_t n = read(fd, &data[count], size - count);
        if (n == -1) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN)
                continue;

            // TODO: Free node::Buffer
            char msg[256];
            snprintf(msg, sizeof(msg), "read failed: %s", strerror(errno));
            Nan::ThrowError(msg);
            return;
        } else if (n == 0) {  // end of stream
            // TODO: Free node::Buffer
            char msg[256];
            snprintf(msg, sizeof(msg), "reached end of stream (read %lu bytes)",
                     count);
            Nan::ThrowError(msg);
            return;
            // TODO: break instead (to reset nonblocking
        } else {
            count += n;
        }
    } while (count < size);

    if (was_non_blocking) {
        opts |= O_NONBLOCK;
        if (fcntl(fd, F_SETFL, opts) < 0) {
            perror("fcntl(F_SETFL)");
            exit(EXIT_FAILURE);
        }
    }

    // v8::Local<v8::Number> num = Nan::New(fd);
    // info.GetReturnValue().Set(num);

    info.GetReturnValue().Set(buffer);
}

void Init(v8::Local<v8::Object> exports) {  
    exports->Set(Nan::New("read").ToLocalChecked(),
                 Nan::New<v8::FunctionTemplate>(Read)->GetFunction());
}

NODE_MODULE(posixread, Init)
