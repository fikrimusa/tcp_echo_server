# TCP Chat Server

A multi-client TCP chat server and client written in C++17. Clients authenticate with a username and password, then can send messages that are broadcast to all other connected clients in real time. The server acts as a broker — it does not participate in the chat, only forwards messages between clients.

## How It Works

### Connection Flow
1. Client connects to the server
2. Server assigns a unique request ID and sends it to the client
3. Client sends a login request (username + SHA-256 hashed password)
4. Server validates credentials against `storage.json` and replies with success or failure
5. On success, the client enters the chat loop

### Chat Flow
1. Client types a message and sends a `ChatMessage` to the server
2. Server receives it, verifies the sender is authenticated, and broadcasts it to all other connected clients
3. Each receiving client's background thread picks it up and prints it to the terminal

### Security
- Passwords are hashed with SHA-256 on the client before being sent — the raw password never travels over the network
- The server re-stamps the sender's username on every message using its own stored record, preventing username spoofing

## Message Protocol

All messages use a fixed 4-byte header followed by a type-specific payload. Fields are in network byte order.

```
MessageHeader (4 bytes)
  uint16_t msgSize   — total message size
  uint8_t  msgType   — message type (see table below)
  uint8_t  reqId     — request ID
```

| Type | Name          | Direction       | Payload |
|------|---------------|-----------------|---------|
| 0    | LoginRequest  | Client → Server | username[32] + password[65] (SHA-256 hex) |
| 1    | LoginResponse | Server → Client | status uint16 (0=fail, 1=ok) |
| 2    | ChatMessage   | Client ↔ Server | username[32] + text[256] |

## Build

**Dependencies:** `g++` (C++17), `libssl-dev`, `nlohmann-json3-dev`

```bash
make        # build server and client binaries to bin/
make clean  # remove build artifacts
```

## Run

Start the server:
```bash
make run-server
```

Connect a client (in a separate terminal):
```bash
make run-client
```

Up to **3 clients** can connect simultaneously. Type `exit` in the server terminal to shut it down gracefully. Type `exit` in a client terminal to disconnect.

## Credentials

User accounts are stored in `storage.json`:

```json
{
  "users": [
    { "username": "User1", "password_hash": "<sha256>" },
    { "username": "User2", "password_hash": "<sha256>" },
    { "username": "User3", "password_hash": "<sha256>" }
  ]
}
```

Default accounts (password: `password`):

| Username | Password |
|----------|----------|
| User1    | password |
| User2    | password |
| User3    | password |

To add a new user, generate the SHA-256 hash of their password:

```bash
echo -n "yourpassword" | sha256sum
```

Then add the entry to `storage.json`.

## Roadmap / Production Grade

Things that would be needed to take this to a production-ready system:

**Security**
- TLS/SSL encryption — currently messages are sent in plaintext over the network; wrap the socket layer with OpenSSL or use a library like `libtls`
- Salted password hashing — replace plain SHA-256 with bcrypt or Argon2 to protect against rainbow table attacks
- Session tokens — issue a signed token after login instead of trusting the persistent connection state
- Rate limiting — prevent brute-force login attempts

**Scalability**
- Replace `select()` with `epoll` — `select()` has an FD limit (`FD_SETSIZE`, typically 1024); `epoll` scales to thousands of connections
- Thread pool — spawn a worker thread per client or use a fixed pool instead of handling all clients on one thread
- Dynamic client limit — replace the hardcoded `MAXCLIENT = 3` with a configurable value
- Persistent message storage — log chat history to a database so clients can catch up on missed messages

**Reliability**
- Reconnection logic — clients should attempt to reconnect automatically on disconnect
- Heartbeat / keepalive — detect dead connections that didn't close cleanly
- Graceful server drain — notify clients before shutdown instead of abruptly closing connections

**Usability**
- Chat rooms / channels — let clients join named rooms instead of a single global broadcast
- User registration — add a sign-up flow rather than manually editing `storage.json`
- Timestamps on messages
- TUI — replace the raw terminal with a proper split-pane interface (e.g. using ncurses) so incoming messages don't interrupt typing

## Project Structure

```
.
├── include/
│   ├── socket.hpp            # SocketServer and SocketClient class declarations
│   └── messageProtocol.hpp   # Wire protocol structs
├── src/
│   ├── socketServer.cpp      # Server implementation + main()
│   └── socketClient.cpp      # Client implementation + main()
├── storage.json              # User credentials
└── Makefile
```

## License

MIT License — see [LICENSE](LICENSE).
