/*
 * Diary Server client: HTTP/1.0 over Winsock 1.1 (wsock32.dll, which every
 * Windows 95 and 98 has). Winsock is only started when a server is used.
 */

#include <winsock.h>

#include "crt.h"
#include "net.h"

#define kDefaultPort    8642
#define kConnectSeconds 5       /* waiting for the server to accept */
#define kReplySeconds   15      /* waiting for each part of the reply */
#define kMaxReply       (80 * 1024)

static char gHost[64];
static unsigned short gPort = kDefaultPort;
static char gKey[64];
static BOOL gStarted;

static void SetError(char *error, int size, const char *text, const char *detail)
{
    CopyString(error, text, size);
    if (detail != NULL && strlen(error) + strlen(detail) + 1 < (size_t)size)
        strcat(error, detail);
}

void NetSetServer(const char *address, const char *key)
{
    const char *colon = strchr(address, ':');
    int length = colon != NULL ? (int)(colon - address) : (int)strlen(address);

    if (length >= (int)sizeof(gHost))
        length = sizeof(gHost) - 1;
    memcpy(gHost, address, length);
    gHost[length] = 0;

    gPort = kDefaultPort;
    if (colon != NULL) {
        long port = 0;
        const char *p;
        for (p = colon + 1; *p >= '0' && *p <= '9' && port < 65536; p++)
            port = port * 10 + (*p - '0');
        if (port > 0 && port < 65536)
            gPort = (unsigned short)port;
    }
    CopyString(gKey, key, sizeof(gKey));
}

void NetCleanup(void)
{
    if (gStarted)
        WSACleanup();
    gStarted = FALSE;
}

void NetEncode(const char *text, char *out, int outSize)
{
    static const char kHex[] = "0123456789ABCDEF";
    int n = 0;

    for (; *text != 0 && n + 4 < outSize; text++) {
        unsigned char c = *text;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                || c == '-' || c == '.' || c == '_') {
            out[n++] = c;
        } else {
            out[n++] = '%';
            out[n++] = kHex[c >> 4];
            out[n++] = kHex[c & 15];
        }
    }
    out[n] = 0;
}

/* Waits until the socket can be read (or written); FALSE on timeout or error. */
static BOOL WaitSocket(SOCKET s, BOOL forWriting, int seconds)
{
    fd_set set, failed;
    struct timeval timeout;

    FD_ZERO(&set);
    FD_SET(s, &set);
    FD_ZERO(&failed);
    FD_SET(s, &failed);         /* Winsock reports a failed connect here */
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;
    if (select(0, forWriting ? NULL : &set, forWriting ? &set : NULL, &failed, &timeout) <= 0)
        return FALSE;
    return FD_ISSET(s, &set) && !FD_ISSET(s, &failed);
}

static SOCKET Connect(char *error, int errorSize)
{
    struct sockaddr_in address;
    struct hostent *host;
    unsigned long ip, nonBlocking = 1;
    SOCKET s;
    int problem = 0, problemSize = sizeof(problem);

    if (!gStarted) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(1, 1), &data) != 0) {
            SetError(error, errorSize, "Windows networking (Winsock) isn't available.", NULL);
            return INVALID_SOCKET;
        }
        gStarted = TRUE;
    }
    if (gHost[0] == 0) {
        SetError(error, errorSize, "No server is set.", NULL);
        return INVALID_SOCKET;
    }

    ip = inet_addr(gHost);
    if (ip == INADDR_NONE) {
        host = gethostbyname(gHost);
        if (host == NULL) {
            SetError(error, errorSize, "Can't find the server ", gHost);
            return INVALID_SOCKET;
        }
        memcpy(&ip, host->h_addr, sizeof(ip));
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        SetError(error, errorSize, "Can't create a network connection.", NULL);
        return INVALID_SOCKET;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(gPort);
    address.sin_addr.s_addr = ip;

    /* Connect without blocking, so an unreachable server only costs a few seconds. */
    ioctlsocket(s, FIONBIO, &nonBlocking);
    if (connect(s, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR
            && WSAGetLastError() != WSAEWOULDBLOCK) {
        problem = WSAGetLastError();
    } else if (!WaitSocket(s, TRUE, kConnectSeconds)) {
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&problem, &problemSize);
        if (problem == 0)
            problem = WSAETIMEDOUT;
    }
    if (problem != 0) {
        closesocket(s);
        if (problem == WSAECONNREFUSED) {
            SetError(error, errorSize, "The server refused the connection. Is Diary Server running on ", gHost);
            if (strlen(error) + 2 < (size_t)errorSize)
                strcat(error, "?");
        } else if (problem == WSAETIMEDOUT) {
            SetError(error, errorSize, "The server didn't answer: ", gHost);
        } else {
            SetError(error, errorSize, "Can't connect to the server ", gHost);
        }
        return INVALID_SOCKET;
    }
    nonBlocking = 0;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    return s;
}

static BOOL SendAll(SOCKET s, const char *data, long length)
{
    while (length > 0) {
        int sent = send(s, data, length, 0);
        if (sent <= 0)
            return FALSE;
        data += sent;
        length -= sent;
    }
    return TRUE;
}

/* Case-insensitive test whether line starts with name (e.g. "X-Diary-Version:"). */
static BOOL HeaderIs(const char *line, const char *name)
{
    for (; *name != 0; line++, name++) {
        char a = *line, b = *name;
        if (a >= 'A' && a <= 'Z')
            a += 32;
        if (b >= 'A' && b <= 'Z')
            b += 32;
        if (a != b)
            return FALSE;
    }
    return TRUE;
}

/* Splits a raw reply into status, version and body (moved to the buffer's start). */
static BOOL ParseReply(char *buf, long length, HttpResponse *response)
{
    char *end = NULL, *line;
    long i;

    for (i = 0; i + 3 < length; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            end = buf + i;
            break;
        }
    if (end == NULL || strncmp(buf, "HTTP/", 5) != 0)
        return FALSE;
    *end = 0;

    line = strchr(buf, ' ');
    if (line == NULL)
        return FALSE;
    response->status = 0;
    for (line++; *line >= '0' && *line <= '9'; line++)
        response->status = response->status * 10 + (*line - '0');

    response->version[0] = 0;
    for (line = strchr(buf, '\n'); line != NULL; line = strchr(line, '\n')) {
        line++;
        if (HeaderIs(line, "X-Diary-Version:")) {
            const char *v = line + 16;
            int n = 0;
            while (*v == ' ')
                v++;
            while (*v > ' ' && n < (int)sizeof(response->version) - 1)
                response->version[n++] = *v++;
            response->version[n] = 0;
        }
    }

    response->length = length - (end + 4 - buf);
    memmove(buf, end + 4, response->length);
    buf[response->length] = 0;
    response->body = buf;
    return TRUE;
}

BOOL HttpRequest(const char *method, const char *path,
                 const char *body, long bodyLength, const char *base,
                 HttpResponse *response, char *error, int errorSize)
{
    char header[512];
    char *buf;
    SOCKET s;
    long length = 0;
    BOOL ok;

    response->status = 0;
    response->version[0] = 0;
    response->body = NULL;
    response->length = 0;

    if (strlen(path) + strlen(gHost) + strlen(gKey) + 200 > sizeof(header)) {
        SetError(error, errorSize, "The request is too long.", NULL);
        return FALSE;
    }
    wsprintfA(header, "%s %s%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: Diary for Windows\r\n",
              method, path, strchr(path, '?') != NULL ? "&charset=windows-1252" : "?charset=windows-1252",
              gHost);
    if (gKey[0] != 0)
        wsprintfA(header + strlen(header), "X-Diary-Key: %s\r\n", gKey);
    if (base != NULL)
        wsprintfA(header + strlen(header), "X-Diary-Base: %s\r\n", base);
    if (body != NULL)
        wsprintfA(header + strlen(header), "Content-Length: %ld\r\n", bodyLength);
    strcat(header, "\r\n");

    buf = HeapAlloc(GetProcessHeap(), 0, kMaxReply + 1);
    if (buf == NULL) {
        SetError(error, errorSize, "Not enough memory.", NULL);
        return FALSE;
    }
    s = Connect(error, errorSize);
    if (s == INVALID_SOCKET) {
        HeapFree(GetProcessHeap(), 0, buf);
        return FALSE;
    }

    ok = SendAll(s, header, strlen(header)) && (body == NULL || SendAll(s, body, bodyLength));
    while (ok) {
        int got;
        if (!WaitSocket(s, FALSE, kReplySeconds)) {
            ok = FALSE;
            break;
        }
        got = recv(s, buf + length, kMaxReply - length, 0);
        if (got < 0)
            ok = FALSE;
        if (got <= 0)
            break;
        length += got;
        if (length == kMaxReply)
            break;
    }
    closesocket(s);

    if (!ok) {
        HeapFree(GetProcessHeap(), 0, buf);
        SetError(error, errorSize, "The connection to the server broke off.", NULL);
        return FALSE;
    }
    if (!ParseReply(buf, length, response)) {
        HeapFree(GetProcessHeap(), 0, buf);
        SetError(error, errorSize, "The server's reply wasn't understood. Is it a Diary Server?", NULL);
        return FALSE;
    }
    return TRUE;
}

void HttpFree(HttpResponse *response)
{
    if (response->body != NULL)
        HeapFree(GetProcessHeap(), 0, response->body);
    response->body = NULL;
}
