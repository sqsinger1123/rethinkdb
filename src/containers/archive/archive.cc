// Copyright 2010-2013 RethinkDB, all rights reserved.
#include "containers/archive/archive.hpp"

#include <string.h>
#include <netinet/in.h>

#include <algorithm>

#include "containers/uuid.hpp"
#include "rpc/serialize_macros.hpp"

const char *archive_result_as_str(archive_result_t archive_result) {
    switch (archive_result) {
    case ARCHIVE_SUCCESS:
        return "ARCHIVE_SUCCESS";
        break;
    case ARCHIVE_SOCK_ERROR:
        return "ARCHIVE_SOCK_ERROR";
        break;
    case ARCHIVE_SOCK_EOF:
        return "ARCHIVE_SOCK_EOF";
        break;
    case ARCHIVE_RANGE_ERROR:
        return "ARCHIVE_RANGE_ERROR";
        break;
    case ARCHIVE_GENERIC_ERROR:
        return "ARCHIVE_GENERIC_ERROR";
        break;
    default:
        unreachable();
    }
}

int64_t force_read(read_stream_t *s, void *p, int64_t n) {
    rassert(n >= 0);

    char *chp = static_cast<char *>(p);
    int64_t written_so_far = 0;
    while (n > 0) {
        int64_t res = s->read(chp, n);
        if (res == 0) {
            return written_so_far;
        }
        if (res == -1) {
            // We don't communicate what data has been read so far.
            // I'm guessing callers to force_read don't care.
            return -1;
        }
        rassert(res <= n);

        written_so_far += res;
        chp += res;
        n -= res;
    }
    return written_so_far;
}

write_message_t::~write_message_t() {
    while (write_buffer_t *buffer = buffers_.head()) {
        buffers_.remove(buffer);
        delete buffer;
    }
}

void write_message_t::append(const void *p, int64_t n) {
    while (n > 0) {
        if (buffers_.empty() || buffers_.tail()->size == write_buffer_t::DATA_SIZE) {
            buffers_.push_back(new write_buffer_t);
        }

        write_buffer_t *b = buffers_.tail();
        int64_t k = std::min<int64_t>(n, write_buffer_t::DATA_SIZE - b->size);

        memcpy(b->data + b->size, p, k);
        b->size += k;
        p = static_cast<const char *>(p) + k;
        n = n - k;
    }
}

size_t write_message_t::size() const {
    size_t ret = 0;
    for (write_buffer_t *h = buffers_.head(); h != NULL; h = buffers_.next(h)) {
        ret += h->size;
    }
    return ret;
}

int send_write_message(write_stream_t *s, const write_message_t *msg) {
    intrusive_list_t<write_buffer_t> *list = const_cast<write_message_t *>(msg)->unsafe_expose_buffers();
    for (write_buffer_t *p = list->head(); p; p = list->next(p)) {
        int64_t res = s->write(p->data, p->size);
        if (res == -1) {
            return -1;
        }
        rassert(res == p->size);
    }
    return 0;
}

write_message_t &operator<<(write_message_t &msg, const uuid_u &uuid) {
    rassert(!uuid.is_unset());
    msg.append(uuid.data(), uuid_u::static_size());
    return msg;
}

MUST_USE archive_result_t deserialize(read_stream_t *s, uuid_u *uuid) {
    int64_t sz = uuid_u::static_size();
    int64_t res = force_read(s, uuid->data(), sz);

    if (res == -1) { return ARCHIVE_SOCK_ERROR; }
    if (res < sz) { return ARCHIVE_SOCK_EOF; }
    rassert(res == sz);
    return ARCHIVE_SUCCESS;
}

write_message_t &operator<<(write_message_t &msg, const in6_addr &addr) {
    msg.append(&addr.s6_addr, sizeof(addr.s6_addr));
    return msg;
}

MUST_USE archive_result_t deserialize(read_stream_t *s, in6_addr *addr) {
    int64_t sz = sizeof(addr->s6_addr);
    int64_t res = force_read(s, &addr->s6_addr, sz);

    if (res == -1) { return ARCHIVE_SOCK_ERROR; }
    if (res < sz) { return ARCHIVE_SOCK_EOF; }
    rassert(res == sz);
    return ARCHIVE_SUCCESS;
}

RDB_IMPL_SERIALIZABLE_1(in_addr, s_addr);
