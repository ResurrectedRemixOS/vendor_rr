/*
 * Copyright (C) 2026 Resurrection Remix
 *
 * The Qualcomm CSR framing follows Duck-ToolBox by Eltavine, with its
 * native V1 path by Wang Han (MIT), https://github.com/eltavine/Duck-ToolBox
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Vendor side of Duck RKP's hardware CSRs.
//
// A legacy request is three big-endian length-prefixed blobs: the server
// challenge, the raw P-256 COSE_Key, and the EEK certificate array. This
// process builds Qualcomm command 0x311e (production mode, a fresh 32-byte
// MAC key, the HMAC of the one-key array, and the EEK chain). The body is
// written into KeymasterUtils' shared buffer and sent with sendCmd. Command
// 0x3124 is the same body when 0x311e returns an empty slot or mink status 1.
// The MAC tag plus the TA's DeviceInfo and ProtectedData bytes are returned
// unchanged. The caller splices them into the CSR.
//
// A V2 request starts with opcode 0x31220002, then the challenge and the
// COSE_Key. The body {4: challenge, 57: bstr([COSE_Key])} uses the same
// shared buffer. Command 0x3125 is tried on its own session before 0x3122.
// An empty status word is not treated as the whole mapping: the bytes outside
// the request are scanned for a CSR. The returned map's field 61 is the
// AuthenticatedRequest the caller posts. send_cmd's private buffer is not
// used. On this TA it comes back with a zero length.

#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <climits>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <limits>
#include <string>
#include <vector>

#ifndef RR_RKP_SELFTEST
#include <android-base/unique_fd.h>
#include <android/log.h>
#include <dlfcn.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "rr-rkp-v1", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "rr-rkp-v1", __VA_ARGS__)
#else
#define LOGI(...) fprintf(stderr, __VA_ARGS__)
#define LOGE(...) fprintf(stderr, __VA_ARGS__)
#endif

namespace {

constexpr uint32_t kCommand = 0x311e;
// libqtikeymaster4 keymint_generate_csr_FIXED. Same body as 0x311e.
constexpr uint32_t kCommandV1Fixed = 0x3124;
constexpr uint32_t kCommandV2 = 0x3122;
// libqtikeymaster4 keymint_generate_csr_v2_FIXED. Same body as 0x3122.
constexpr uint32_t kCommandV2Fixed = 0x3125;
// Larger than any challenge blob length, so it cannot be read as a V1 request.
constexpr uint32_t kOpcodeV2 = 0x31220002;
constexpr size_t kBufferSize = 0xa000;
constexpr size_t kMaxRequest = 0x9000;
constexpr size_t kMaxChallenge = 64;
constexpr size_t kMaxCoseKey = 4096;
constexpr size_t kMaxEek = 0x8000;
constexpr int kIoTimeoutMs = 20000;

constexpr int kStatusTransport = 1;
constexpr int kStatusBadResponse = 2;
constexpr int kStatusLibrary = 3;
constexpr int kStatusBadRequest = 4;

const char kLibraryPath[] = "/vendor/lib64/libkeymasterdeviceutils.so";
const char kSocketEnv[] = "ANDROID_SOCKET_rr_rkp_v1";

struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;

    uint8_t byte() {
        if (p >= end) {
            ok = false;
            return 0;
        }
        return *p++;
    }

    uint64_t argument(uint8_t additional) {
        if (additional < 24) return additional;
        size_t width = 0;
        if (additional == 24) width = 1;
        else if (additional == 25) width = 2;
        else if (additional == 26) width = 4;
        else if (additional == 27) width = 8;
        else {
            ok = false;
            return 0;
        }
        uint64_t value = 0;
        for (size_t i = 0; i < width; i++) {
            value = (value << 8) | byte();
        }
        return value;
    }

    void skip_item() {
        const uint8_t header = byte();
        if (!ok) return;
        const uint8_t major = header >> 5;
        const uint8_t additional = header & 31;
        if (additional == 31) {
            ok = false;
            return;
        }
        const uint64_t count = argument(additional);
        if (!ok) return;
        switch (major) {
            case 0:
            case 1:
            case 7:
                return;
            case 2:
            case 3:
                if (count > static_cast<uint64_t>(end - p)) {
                    ok = false;
                    return;
                }
                p += count;
                return;
            case 4:
                for (uint64_t i = 0; i < count; i++) skip_item();
                return;
            case 5:
                for (uint64_t i = 0; i < count; i++) {
                    skip_item();
                    skip_item();
                }
                return;
            case 6:
                skip_item();
                return;
            default:
                ok = false;
                return;
        }
    }
};

bool read_int_key(Reader* reader, int64_t* key) {
    const uint8_t header = reader->byte();
    if (!reader->ok) return false;
    const uint8_t major = header >> 5;
    const uint8_t additional = header & 31;
    if (additional == 31 || (major != 0 && major != 1)) return false;
    const uint64_t magnitude = reader->argument(additional);
    if (!reader->ok || magnitude > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return false;
    }
    *key = major == 0 ? static_cast<int64_t>(magnitude)
                      : -1 - static_cast<int64_t>(magnitude);
    return true;
}

bool read_bstr(Reader* reader, std::vector<uint8_t>* out) {
    const uint8_t header = reader->byte();
    if (!reader->ok || (header >> 5) != 2 || (header & 31) == 31) return false;
    const uint64_t length = reader->argument(header & 31);
    if (!reader->ok || length > kBufferSize ||
        length > static_cast<uint64_t>(reader->end - reader->p)) {
        return false;
    }
    out->assign(reader->p, reader->p + length);
    reader->p += length;
    return true;
}

// Field 59 and 60 carry the CBOR values the CSR splices. Their bstr headers
// stay here. An extra field, a rewrite, or trailing bytes is a bad response.
bool parse_v1_response(const uint8_t* data, size_t length, std::vector<uint8_t>* device_info,
                       std::vector<uint8_t>* protected_data) {
    Reader reader{data, data + length};
    const uint8_t header = reader.byte();
    if (!reader.ok || (header >> 5) != 5 || (header & 31) == 31) return false;
    const uint64_t count = reader.argument(header & 31);
    if (!reader.ok || count != 2) return false;
    bool got_info = false;
    bool got_protected = false;
    for (uint64_t i = 0; i < count; i++) {
        int64_t key = 0;
        if (!read_int_key(&reader, &key)) return false;
        if (key == 59 || key == 60) {
            std::vector<uint8_t> value;
            if (!read_bstr(&reader, &value) || value.empty()) return false;
            if (key == 59) {
                if (got_info) return false;
                *device_info = std::move(value);
                got_info = true;
            } else {
                if (got_protected) return false;
                *protected_data = std::move(value);
                got_protected = true;
            }
        } else {
            reader.skip_item();
            if (!reader.ok) return false;
        }
    }
    return reader.ok && reader.p == reader.end && got_info && got_protected;
}

bool valid_eek(const uint8_t* data, size_t length) {
    Reader reader{data, data + length};
    const uint8_t header = reader.byte();
    if (!reader.ok || (header >> 5) != 4 || (header & 31) == 31) return false;
    const uint64_t count = reader.argument(header & 31);
    if (!reader.ok || count == 0 || count > 16) return false;
    for (uint64_t i = 0; i < count; i++) {
        const uint8_t certificate = reader.byte();
        if (!reader.ok || (certificate >> 5) != 4 || (certificate & 31) == 31) return false;
        const uint64_t fields = reader.argument(certificate & 31);
        if (!reader.ok || fields != 4) return false;
        for (uint64_t field = 0; field < fields; field++) reader.skip_item();
        if (!reader.ok) return false;
    }
    return reader.ok && reader.p == reader.end;
}

void append_bstr(std::vector<uint8_t>* out, const uint8_t* data, size_t length) {
    if (length < 24) {
        out->push_back(static_cast<uint8_t>(0x40 | length));
    } else if (length < 256) {
        out->push_back(0x58);
        out->push_back(static_cast<uint8_t>(length));
    } else if (length < 65536) {
        out->push_back(0x59);
        out->push_back(static_cast<uint8_t>(length >> 8));
        out->push_back(static_cast<uint8_t>(length));
    } else {
        out->push_back(0x5a);
        out->push_back(static_cast<uint8_t>(length >> 24));
        out->push_back(static_cast<uint8_t>(length >> 16));
        out->push_back(static_cast<uint8_t>(length >> 8));
        out->push_back(static_cast<uint8_t>(length));
    }
    if (length > 0) out->insert(out->end(), data, data + length);
}

// HMAC-SHA256 over COSE MAC0 of the one-element key array. The MAC key is
// the random 32 bytes placed in request field 56, not the all-zero HBK.
bool mac_tag(const uint8_t key[32], const uint8_t* cose_key, size_t cose_length, uint8_t tag[32]) {
    std::vector<uint8_t> payload;
    payload.reserve(cose_length + 1);
    payload.push_back(0x81);
    payload.insert(payload.end(), cose_key, cose_key + cose_length);

    const uint8_t protected_header[] = {0xa1, 0x01, 0x05};
    std::vector<uint8_t> structure;
    structure.push_back(0x84);
    structure.push_back(0x64);
    structure.insert(structure.end(), {'M', 'A', 'C', '0'});
    append_bstr(&structure, protected_header, sizeof(protected_header));
    append_bstr(&structure, nullptr, 0);
    append_bstr(&structure, payload.data(), payload.size());

    unsigned int out_length = 0;
    return HMAC(EVP_sha256(), key, 32, structure.data(), structure.size(), tag, &out_length) !=
                   nullptr &&
           out_length == 32;
}

// Canonical map order is encoded key length, then lexicographic bytes.
// Key 4 is one byte. Keys 55..58 are two bytes, so the order is fixed.
bool build_request(const uint8_t* challenge, size_t challenge_length, const uint8_t* cose_key,
                   size_t cose_length, const uint8_t* eek, size_t eek_length,
                   const uint8_t mac_key[32], std::vector<uint8_t>* request, uint8_t tag[32]) {
    if (challenge_length == 0 || challenge_length > kMaxChallenge || cose_length == 0 ||
        cose_length > kMaxCoseKey || (cose_key[0] & 0xe0) != 0xa0 || eek_length == 0 ||
        eek_length > kMaxEek || !valid_eek(eek, eek_length)) {
        return false;
    }
    if (!mac_tag(mac_key, cose_key, cose_length, tag)) return false;

    std::vector<uint8_t> body;
    body.push_back(0xa5);
    body.push_back(0x04);
    append_bstr(&body, challenge, challenge_length);
    body.push_back(0x18);
    body.push_back(55);
    body.push_back(0x00);
    body.push_back(0x18);
    body.push_back(56);
    append_bstr(&body, mac_key, 32);
    body.push_back(0x18);
    body.push_back(57);
    append_bstr(&body, tag, 32);
    body.push_back(0x18);
    body.push_back(58);
    append_bstr(&body, eek, eek_length);

    // init() writes the command word. The body that follows it is only the map.
    *request = std::move(body);
    return request->size() <= kMaxRequest;
}

// Qualcomm's CSR V2 body: a two-entry map, challenge at 4 and one COSE_Key at 57.
// The command word is written separately by KeymasterUtils::init.
bool encode_csr_v2(uint8_t* out, size_t capacity, const uint8_t* challenge, size_t challenge_length,
                   const uint8_t* cose_key, size_t cose_length, size_t* written) {
    if (out == nullptr || written == nullptr || challenge_length == 0 ||
        challenge_length > kMaxChallenge || cose_length == 0 || cose_length > kMaxCoseKey ||
        (cose_key[0] & 0xe0) != 0xa0) {
        return false;
    }
    std::vector<uint8_t> keys;
    keys.reserve(cose_length + 1);
    keys.push_back(0x81);
    keys.insert(keys.end(), cose_key, cose_key + cose_length);

    std::vector<uint8_t> body;
    body.push_back(0xa2);
    body.push_back(0x04);
    append_bstr(&body, challenge, challenge_length);
    body.push_back(0x18);
    body.push_back(57);
    append_bstr(&body, keys.data(), keys.size());
    if (body.size() > capacity) return false;
    memcpy(out, body.data(), body.size());
    *written = body.size();
    return true;
}

// sendCmd returns the CBOR after its status word. Field 61 is the CSR. A bare
// array is already that CSR.
bool parse_v2_response(const uint8_t* data, size_t length, std::vector<uint8_t>* csr) {
    if (data == nullptr || csr == nullptr || length == 0 || length > kBufferSize) return false;
    if ((data[0] >> 5) == 4) {
        csr->assign(data, data + length);
        return true;
    }
    Reader reader{data, data + length};
    const uint8_t header = reader.byte();
    if (!reader.ok || (header >> 5) != 5 || (header & 31) == 31) return false;
    const uint64_t count = reader.argument(header & 31);
    if (!reader.ok || count == 0 || count > 16) return false;
    bool found = false;
    for (uint64_t i = 0; i < count; i++) {
        int64_t key = 0;
        if (!read_int_key(&reader, &key)) return false;
        if (key == 61) {
            if (found || !read_bstr(&reader, csr) || csr->empty()) return false;
            found = true;
        } else {
            reader.skip_item();
            if (!reader.ok) return false;
        }
    }
    return reader.ok && reader.p == reader.end && found;
}

bool cbor_item_size(const uint8_t* data, size_t length, size_t* size) {
    Reader reader{data, data + length};
    reader.skip_item();
    if (!reader.ok || reader.p <= data) return false;
    *size = static_cast<size_t>(reader.p - data);
    return true;
}

void append_hex(std::string* out, const uint8_t* data, size_t length) {
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < length; i++) {
        out->push_back(kHex[data[i] >> 4]);
        out->push_back(kHex[data[i] & 0xf]);
    }
}

// init() stores the command at byte 0 and the body at byte 4, then zeros the
// rest of the mapping. Bytes that still match that image are the request.
// A map or array anywhere else that parses is the TA's CSR. noise records
// the first unexpected bytes so a miss is visible in the socket error.
bool find_shared_response(const uint8_t* buffer, size_t buffer_size, uint32_t command,
                          const uint8_t* body, size_t body_length, bool want_v1,
                          std::vector<uint8_t>* payload, std::string* noise) {
    if (buffer == nullptr || payload == nullptr || body == nullptr ||
        body_length > buffer_size || buffer_size < 4 + body_length) {
        return false;
    }
    const uint8_t prefix[4] = {
            static_cast<uint8_t>(command),
            static_cast<uint8_t>(command >> 8),
            static_cast<uint8_t>(command >> 16),
            static_cast<uint8_t>(command >> 24),
    };
    const size_t request_end = 4 + body_length;
    size_t first_noise = buffer_size;
    for (size_t i = 0; i < buffer_size; i++) {
        const bool in_request = i < request_end;
        const uint8_t original = i < 4 ? prefix[i] : (in_request ? body[i - 4] : 0);
        if ((in_request && buffer[i] == original) || buffer[i] == 0) continue;
        if (first_noise == buffer_size) first_noise = i;
        const uint8_t major = buffer[i] >> 5;
        if (major != 4 && major != 5) continue;
        size_t item = 0;
        if (!cbor_item_size(buffer + i, buffer_size - i, &item) || item == 0 ||
            item > buffer_size - i) {
            continue;
        }
        if (want_v1) {
            std::vector<uint8_t> device_info;
            std::vector<uint8_t> protected_data;
            if (!parse_v1_response(buffer + i, item, &device_info, &protected_data)) continue;
        } else if (!parse_v2_response(buffer + i, item, payload)) {
            continue;
        }
        payload->assign(buffer + i, buffer + i + item);
        return true;
    }
    if (noise != nullptr && first_noise < buffer_size) {
        size_t n = buffer_size - first_noise;
        if (n > 8) n = 8;
        *noise = "buf+" + std::to_string(first_noise) + "=";
        append_hex(noise, buffer + first_noise, n);
    }
    return false;
}

#ifndef RR_RKP_SELFTEST

std::string hex_command(uint32_t command) {
    char text[9];
    snprintf(text, sizeof(text), "%x", command);
    return text;
}

using ConstructorFn = void (*)(void*, int32_t);
using DestructorFn = void (*)(void*);
using InitFn = void (*)(void*, uint32_t, void*);
using SendCmdFn = int32_t (*)(void*, const void*, void*);

struct KeymasterBlob {
    uint8_t* data;
    size_t data_length;
};

struct Session {
    alignas(16) uint8_t object[0x40] = {};
    ConstructorFn constructor = nullptr;
    DestructorFn destructor = nullptr;
    InitFn init = nullptr;
    SendCmdFn send_cmd = nullptr;
    void* library = nullptr;
    bool live = false;
    std::string error;

    Session() = default;
    ~Session() { reset(); }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void reset() {
        if (live && destructor != nullptr) destructor(object);
        live = false;
        if (library != nullptr) dlclose(library);
        library = nullptr;
        destructor = nullptr;
        init = nullptr;
        send_cmd = nullptr;
    }

    bool open() {
        library = dlopen(kLibraryPath, RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) {
            error = std::string("dlopen: ") + (dlerror() != nullptr ? dlerror() : "unknown");
            return false;
        }
        constructor = reinterpret_cast<ConstructorFn>(dlsym(
                library, "_ZN14keymasterutils14KeymasterUtilsC1E26keymaster_security_level_t"));
        destructor = reinterpret_cast<DestructorFn>(
                dlsym(library, "_ZN14keymasterutils14KeymasterUtilsD1Ev"));
        // send_cmd is the private-buffer entry. This TA does not return a CSR
        // through it. Requiring the symbol keeps the library identity check.
        void* send = dlsym(library, "_ZN14keymasterutils14KeymasterUtils8send_cmdEPvjS1_j");
        init = reinterpret_cast<InitFn>(
                dlsym(library, "_ZN14keymasterutils14KeymasterUtils4initEjP16keymaster_blob_t"));
        send_cmd = reinterpret_cast<SendCmdFn>(dlsym(
                library, "_ZN14keymasterutils14KeymasterUtils7sendCmdERK16keymaster_blob_tPS1_"));
        if (constructor == nullptr || destructor == nullptr || send == nullptr ||
            init == nullptr || send_cmd == nullptr) {
            error = std::string("dlsym: ") + (dlerror() != nullptr ? dlerror() : "missing symbol");
            reset();
            return false;
        }
        memset(object, 0, sizeof(object));
        // security_level 1 is TRUSTED_ENVIRONMENT.
        constructor(object, 1);
        live = true;
        return true;
    }

    bool reopen() {
        reset();
        return open();
    }

    // V2's payload is the AuthenticatedRequest array. V1's payload is the map.
    static bool accept_payload(const uint8_t* data, size_t length, bool want_v1,
                               std::vector<uint8_t>* payload) {
        if (want_v1) {
            std::vector<uint8_t> device_info;
            std::vector<uint8_t> protected_data;
            if (!parse_v1_response(data, length, &device_info, &protected_data)) return false;
            payload->assign(data, data + length);
            return true;
        }
        return parse_v2_response(data, length, payload);
    }

    // Fresh session, shared buffer, one command. retryable is an empty slot
    // or mink status 1. A negative TA status is returned as-is.
    bool invoke(uint32_t command, const uint8_t* body, size_t body_length, bool want_v1,
                std::vector<uint8_t>* payload, int32_t* call, bool* retryable,
                std::string* failure) {
        *retryable = false;
        *call = 0;
        if (!reopen()) {
            *failure = error.empty() ? "KeymasterUtils is not open" : error;
            return false;
        }
        KeymasterBlob request{};
        init(object, command, &request);
        if (request.data == nullptr || body_length > request.data_length) {
            *failure = "KeymasterUtils init did not return a shared buffer";
            return false;
        }
        const uint8_t prefix[4] = {
                static_cast<uint8_t>(command),
                static_cast<uint8_t>(command >> 8),
                static_cast<uint8_t>(command >> 16),
                static_cast<uint8_t>(command >> 24),
        };
        if (memcmp(request.data - 4, prefix, sizeof(prefix)) != 0) {
            *failure = "shared buffer command was not written";
            return false;
        }
        memcpy(request.data, body, body_length);
        request.data_length = body_length;
        uint8_t* buffer = request.data - 4;
        KeymasterBlob response{};
        *call = send_cmd(object, &request, &response);
        const bool header_payload = *call == 0 && response.data != nullptr &&
                                    response.data_length > 0 && response.data_length <= kBufferSize;
        if (header_payload &&
            accept_payload(response.data, response.data_length, want_v1, payload)) {
            return true;
        }
        std::string noise;
        if (find_shared_response(buffer, kBufferSize, command, body, body_length, want_v1, payload,
                                 &noise)) {
            return true;
        }
        if (header_payload || (*call == 0 && response.data != nullptr &&
                               response.data_length > kBufferSize)) {
            *failure = "0x" + hex_command(command) + " TA response is not a CSR";
            if (!noise.empty()) *failure += " " + noise;
            return false;
        }
        *retryable = (*call == 0 || *call == 1);
        if (*call != 0) {
            *failure = "sendCmd 0x" + hex_command(command) + " returned " + std::to_string(*call);
        } else {
            *failure = "0x" + hex_command(command) + " empty";
        }
        if (!noise.empty()) *failure += " " + noise;
        return false;
    }

    bool try_commands(const uint32_t* commands, size_t count, const uint8_t* body,
                      size_t body_length, bool want_v1, std::vector<uint8_t>* payload,
                      int32_t* ta_status, std::string* failure) {
        std::string attempts;
        for (size_t i = 0; i < count; i++) {
            bool retryable = false;
            std::string one;
            if (invoke(commands[i], body, body_length, want_v1, payload, ta_status, &retryable,
                       &one)) {
                LOGI("command 0x%s csr %zu bytes", hex_command(commands[i]).c_str(),
                     payload->size());
                return true;
            }
            if (!attempts.empty()) attempts += "; ";
            attempts += one;
            if (!retryable) {
                *failure = attempts;
                return false;
            }
            if (i + 1 < count) {
                LOGI("%s; trying 0x%s", one.c_str(), hex_command(commands[i + 1]).c_str());
            }
        }
        *failure = attempts;
        return false;
    }

    // 0x3125 is attempted on its own session. A following 0x3122 used to leave
    // the next invoke at mink status 1. A real TA status, including -18, stops.
    bool csr_v2(const uint8_t* challenge, size_t challenge_length, const uint8_t* cose_key,
                size_t cose_length, std::vector<uint8_t>* csr, int32_t* ta_status,
                std::string* failure) {
        std::vector<uint8_t> body(kMaxRequest);
        size_t written = 0;
        if (!encode_csr_v2(body.data(), body.size(), challenge, challenge_length, cose_key,
                           cose_length, &written)) {
            *failure = "challenge or COSE key is not usable";
            return false;
        }
        body.resize(written);
        static const uint32_t kCommands[] = {kCommandV2Fixed, kCommandV2};
        return try_commands(kCommands, sizeof(kCommands) / sizeof(kCommands[0]), body.data(),
                            body.size(), false, csr, ta_status, failure);
    }

    // 0x311e, then 0x3124 when that slot is empty or the invoke returns 1.
    bool csr_v1(const uint8_t* body, size_t body_length, std::vector<uint8_t>* payload,
                int32_t* ta_status, std::string* failure) {
        static const uint32_t kCommands[] = {kCommand, kCommandV1Fixed};
        return try_commands(kCommands, sizeof(kCommands) / sizeof(kCommands[0]), body,
                            body_length, true, payload, ta_status, failure);
    }
};

bool write_all(int fd, const void* data, size_t length) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < length) {
        const ssize_t wrote = write(fd, bytes + done, length - done);
        if (wrote < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (wrote == 0) return false;
        done += static_cast<size_t>(wrote);
    }
    return true;
}

bool read_all(int fd, void* data, size_t length) {
    uint8_t* bytes = static_cast<uint8_t*>(data);
    size_t done = 0;
    while (done < length) {
        const ssize_t got = read(fd, bytes + done, length - done);
        if (got < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (got == 0) return false;
        done += static_cast<size_t>(got);
    }
    return true;
}

bool write_u32(int fd, uint32_t value) {
    uint8_t bytes[4] = {
            static_cast<uint8_t>(value >> 24),
            static_cast<uint8_t>(value >> 16),
            static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value),
    };
    return write_all(fd, bytes, sizeof(bytes));
}

bool read_u32(int fd, uint32_t* value) {
    uint8_t bytes[4];
    if (!read_all(fd, bytes, sizeof(bytes))) return false;
    *value = (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
             (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
    return true;
}

bool write_blob(int fd, const uint8_t* data, size_t length) {
    return write_u32(fd, static_cast<uint32_t>(length)) &&
           (length == 0 || write_all(fd, data, length));
}

bool read_blob(int fd, std::vector<uint8_t>* out, size_t max_length) {
    uint32_t length = 0;
    if (!read_u32(fd, &length) || length > max_length) return false;
    out->assign(length, 0);
    return length == 0 || read_all(fd, out->data(), length);
}

void reply_error(int fd, int status, int32_t detail, const std::string& message) {
    const std::string text = message.size() > 1024 ? message.substr(0, 1024) : message;
    if (!write_u32(fd, static_cast<uint32_t>(status)) ||
        !write_u32(fd, static_cast<uint32_t>(detail)) ||
        !write_blob(fd, reinterpret_cast<const uint8_t*>(text.data()), text.size())) {
        LOGI("client disconnected while reporting %s", text.c_str());
    }
}

void set_timeouts(int fd) {
    timeval limit = {};
    limit.tv_sec = kIoTimeoutMs / 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
}

bool loader_failure(const std::string& message) {
    return message.rfind("dlopen", 0) == 0 || message.rfind("dlsym", 0) == 0 ||
           message.rfind("KeymasterUtils", 0) == 0;
}

void handle_v2(int fd, Session* session) {
    std::vector<uint8_t> challenge;
    std::vector<uint8_t> cose_key;
    if (!read_blob(fd, &challenge, kMaxChallenge) || !read_blob(fd, &cose_key, kMaxCoseKey)) {
        reply_error(fd, kStatusBadRequest, 0, "truncated or oversized request");
        return;
    }
    std::vector<uint8_t> csr;
    int32_t ta_status = 0;
    std::string failure;
    if (!session->csr_v2(challenge.data(), challenge.size(), cose_key.data(), cose_key.size(), &csr,
                         &ta_status, &failure)) {
        LOGE("%s", failure.c_str());
        reply_error(fd, loader_failure(failure) ? kStatusLibrary : kStatusTransport, ta_status,
                    failure);
        return;
    }
    LOGI("v2 csr %zu bytes", csr.size());
    if (!write_u32(fd, 0) || !write_blob(fd, csr.data(), csr.size())) {
        LOGI("client disconnected after a V2 TA response");
    }
}

void handle_client(int fd, Session* session) {
    set_timeouts(fd);
    uint32_t head = 0;
    if (!read_u32(fd, &head)) return;
    if (head == kOpcodeV2) {
        handle_v2(fd, session);
        return;
    }
    std::vector<uint8_t> challenge;
    std::vector<uint8_t> cose_key;
    std::vector<uint8_t> eek;
    if (head > kMaxChallenge) {
        reply_error(fd, kStatusBadRequest, 0, "truncated or oversized request");
        return;
    }
    challenge.assign(head, 0);
    if ((head != 0 && !read_all(fd, challenge.data(), head)) ||
        !read_blob(fd, &cose_key, kMaxCoseKey) || !read_blob(fd, &eek, kMaxEek)) {
        reply_error(fd, kStatusBadRequest, 0, "truncated or oversized request");
        return;
    }
    uint8_t mac_key[32];
    uint8_t tag[32];
    if (RAND_bytes(mac_key, sizeof(mac_key)) != 1) {
        reply_error(fd, kStatusBadRequest, 0, "RAND_bytes failed");
        return;
    }
    std::vector<uint8_t> request;
    if (!build_request(challenge.data(), challenge.size(), cose_key.data(), cose_key.size(),
                       eek.data(), eek.size(), mac_key, &request, tag)) {
        reply_error(fd, kStatusBadRequest, 0, "challenge, COSE key, or EEK chain is not usable");
        return;
    }
    std::vector<uint8_t> payload;
    int32_t ta_status = 0;
    std::string failure;
    if (!session->csr_v1(request.data(), request.size(), &payload, &ta_status, &failure)) {
        LOGE("%s", failure.c_str());
        reply_error(fd, loader_failure(failure) ? kStatusLibrary : kStatusTransport, ta_status,
                    failure);
        return;
    }
    std::vector<uint8_t> device_info;
    std::vector<uint8_t> protected_data;
    if (!parse_v1_response(payload.data(), payload.size(), &device_info, &protected_data)) {
        reply_error(fd, kStatusBadResponse, 0, "TA response is not DeviceInfo plus ProtectedData");
        return;
    }
    LOGI("v1 csr evidence deviceInfo=%zu protectedData=%zu", device_info.size(),
         protected_data.size());
    if (!write_u32(fd, 0) || !write_blob(fd, tag, sizeof(tag)) ||
        !write_blob(fd, device_info.data(), device_info.size()) ||
        !write_blob(fd, protected_data.data(), protected_data.size())) {
        LOGI("client disconnected after a TA response");
    }
}

int listen_fd() {
    const char* env = getenv(kSocketEnv);
    if (env == nullptr) {
        LOGE("missing %s", kSocketEnv);
        return -1;
    }
    char* end = nullptr;
    errno = 0;
    const long parsed = strtol(env, &end, 10);
    if (errno != 0 || end == env || *end != '\0' || parsed < 0 || parsed > INT_MAX) {
        LOGE("bad %s", kSocketEnv);
        return -1;
    }
    const int fd = static_cast<int>(parsed);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (listen(fd, 4) != 0) {
        LOGE("listen failed: %s", strerror(errno));
        return -1;
    }
    return fd;
}

#endif

}  // namespace

#ifndef RR_RKP_SELFTEST

int main() {
    signal(SIGPIPE, SIG_IGN);
    const int server = listen_fd();
    if (server < 0) return 1;

    Session session;
    if (!session.open()) {
        LOGE("%s", session.error.c_str());
    } else {
        LOGI("KeymasterUtils ready");
    }

    for (;;) {
        pollfd waiting = {server, POLLIN, 0};
        const int polled = poll(&waiting, 1, -1);
        if (polled < 0) {
            if (errno == EINTR) continue;
            LOGE("poll failed: %s", strerror(errno));
            return 1;
        }
        android::base::unique_fd client(accept4(server, nullptr, nullptr, SOCK_CLOEXEC));
        if (client.get() < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            LOGE("accept failed: %s", strerror(errno));
            sleep(1);
            continue;
        }
        handle_client(client.get(), &session);
    }
}

#else

static int fail(const char* message) {
    fprintf(stderr, "selftest: %s\n", message);
    return 1;
}

static bool expect_bytes(const std::vector<uint8_t>& got, const uint8_t* want, size_t length) {
    return got.size() == length && memcmp(got.data(), want, length) == 0;
}

int main() {
    const uint8_t x[] = {0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47, 0xf8, 0xbc, 0xe6,
                         0xe5, 0x63, 0xa4, 0x40, 0xf2, 0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb,
                         0x33, 0xa0, 0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96};
    const uint8_t y[] = {0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b, 0x8e, 0xe7, 0xeb,
                         0x4a, 0x7c, 0x0f, 0x9e, 0x16, 0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31,
                         0x5e, 0xce, 0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5};
    std::vector<uint8_t> cose;
    cose.push_back(0xa5);
    cose.push_back(0x01);
    cose.push_back(0x02);
    cose.push_back(0x03);
    cose.push_back(0x26);
    cose.push_back(0x20);
    cose.push_back(0x01);
    cose.push_back(0x21);
    append_bstr(&cose, x, sizeof(x));
    cose.push_back(0x22);
    append_bstr(&cose, y, sizeof(y));

    const uint8_t challenge[32] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                                   3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    const uint8_t mac_key[32] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                                 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
    const uint8_t eek[] = {0x81, 0x84, 0x40, 0xa0, 0x40, 0x40};
    uint8_t tag[32];
    std::vector<uint8_t> request;
    if (!build_request(challenge, sizeof(challenge), cose.data(), cose.size(), eek, sizeof(eek),
                       mac_key, &request, tag)) {
        return fail("build_request rejected the fixture");
    }
    const uint8_t want_tag[] = {0x45, 0xca, 0xe1, 0x13, 0xe3, 0xef, 0x7c, 0x80, 0x3f, 0xad, 0xe8,
                                0xba, 0x99, 0x31, 0x42, 0x7f, 0x6a, 0x22, 0xb9, 0x7e, 0xeb, 0xf0,
                                0x53, 0xbc, 0x7c, 0x5d, 0xce, 0x49, 0x52, 0x59, 0x22, 0x05};
    if (memcmp(tag, want_tag, sizeof(want_tag)) != 0) return fail("mac tag mismatch");
    if (request.size() < 4 || request[0] != 0xa5 || request[1] != 0x04 || request[2] != 0x58 ||
        request[3] != 0x20) {
        return fail("challenge field");
    }
    if (!valid_eek(eek, sizeof(eek)) || valid_eek(nullptr, 0)) return fail("eek validator");
    const uint8_t empty_array[] = {0x80};
    if (valid_eek(empty_array, sizeof(empty_array))) return fail("empty eek accepted");

    const uint8_t device[] = {0xa1, 0x01, 0x02};
    const uint8_t protected_data[] = {0x84, 0x40, 0xa0, 0x40, 0x40};
    const uint8_t payload[] = {0xa2, 0x18, 0x3b, 0x43, 0xa1, 0x01, 0x02, 0x18, 0x3c, 0x45,
                               0x84, 0x40, 0xa0, 0x40, 0x40};
    std::vector<uint8_t> got_device;
    std::vector<uint8_t> got_protected;
    if (!parse_v1_response(payload, sizeof(payload), &got_device, &got_protected) ||
        !expect_bytes(got_device, device, sizeof(device)) ||
        !expect_bytes(got_protected, protected_data, sizeof(protected_data))) {
        return fail("response parse");
    }
    const uint8_t swapped[] = {0xa2, 0x18, 0x3c, 0x45, 0x84, 0x40, 0xa0, 0x40, 0x40,
                               0x18, 0x3b, 0x43, 0xa1, 0x01, 0x02};
    if (!parse_v1_response(swapped, sizeof(swapped), &got_device, &got_protected) ||
        !expect_bytes(got_device, device, sizeof(device))) {
        return fail("swapped fields");
    }
    const uint8_t one_field[] = {0xa1, 0x18, 0x3b, 0x43, 0xa1, 0x01, 0x02};
    if (parse_v1_response(one_field, sizeof(one_field), &got_device, &got_protected)) {
        return fail("one-field map accepted");
    }
    const uint8_t trailing[] = {0xa2, 0x18, 0x3b, 0x43, 0xa1, 0x01, 0x02, 0x18, 0x3c, 0x45,
                                0x84, 0x40, 0xa0, 0x40, 0x40, 0x00};
    if (parse_v1_response(trailing, sizeof(trailing), &got_device, &got_protected)) {
        return fail("trailing byte accepted");
    }

    uint8_t encoded[256];
    size_t encoded_length = 0;
    if (!encode_csr_v2(encoded, sizeof(encoded), challenge, sizeof(challenge), cose.data(),
                       cose.size(), &encoded_length) ||
        encoded[0] != 0xa2 || encoded[1] != 0x04 || encoded[2] != 0x58 || encoded[3] != 0x20) {
        return fail("v2 request encoding");
    }
    const uint8_t wrapped[] = {0xa1, 0x18, 0x3d, 0x43, 0x84, 0x40, 0x40};
    std::vector<uint8_t> got_csr;
    if (!parse_v2_response(wrapped, sizeof(wrapped), &got_csr) || got_csr.size() != 3 ||
        got_csr[0] != 0x84) {
        return fail("v2 field 61");
    }
    const uint8_t bare[] = {0x84, 0x40, 0x40, 0x40};
    if (!parse_v2_response(bare, sizeof(bare), &got_csr) || got_csr.size() != sizeof(bare)) {
        return fail("v2 bare array");
    }
    const uint8_t missing[] = {0xa1, 0x04, 0x40};
    if (parse_v2_response(missing, sizeof(missing), &got_csr)) return fail("v2 map without 61");

    const size_t request_end = 4 + request.size();
    std::vector<uint8_t> image(request_end + 64, 0);
    image[0] = 0x1e;
    image[1] = 0x31;
    memcpy(image.data() + 4, request.data(), request.size());
    const size_t planted = request_end + 16;
    memcpy(image.data() + planted, payload, sizeof(payload));
    std::vector<uint8_t> found;
    std::string noise;
    if (!find_shared_response(image.data(), image.size(), kCommand, request.data(), request.size(),
                              true, &found, &noise) ||
        !expect_bytes(found, payload, sizeof(payload))) {
        return fail("shared buffer scan");
    }
    std::vector<uint8_t> only(request_end + 64, 0);
    only[0] = 0x1e;
    only[1] = 0x31;
    memcpy(only.data() + 4, request.data(), request.size());
    noise.clear();
    if (find_shared_response(only.data(), only.size(), kCommand, request.data(), request.size(),
                             true, &found, &noise) ||
        !noise.empty()) {
        return fail("request scanned as a CSR");
    }
    const size_t noise_at = request_end + 8;
    only[noise_at] = 0x01;
    noise.clear();
    if (find_shared_response(only.data(), only.size(), kCommand, request.data(), request.size(),
                             true, &found, &noise) ||
        noise != "buf+" + std::to_string(noise_at) + "=0100000000000000") {
        return fail("unexpected buffer bytes");
    }
    fprintf(stderr, "selftest ok\n");
    return 0;
}

#endif
