# TCP Echo Server

A TCP client-server application written in C++17 with login authentication.

## Features

- Multi-client support (up to 3 simultaneous connections)
- Login authentication with SHA-256 password hashing
- CRC32 username integrity check
- Graceful shutdown via `exit` command
- Binary message protocol with typed headers

## Protocol

| Type | Name | Direction |
|------|------|-----------|
| 0 | LoginRequest | Client → Server |
| 1 | LoginResponse | Server → Client |
| 2 | EchoRequest | Client → Server |

## Build

```bash
make        # build server and client
make clean  # remove binaries
```

Requires: `g++` (C++17), `libssl-dev`, `nlohmann-json`

## Run

Start the server in one terminal:

```bash
make run-server
```

Connect a client in another terminal:

```bash
make run-client
```

Type `exit` in the server terminal to shut down.

## Authentication

Credentials are stored in `storage.json` as a SHA-256 password hash:

```json
{
  "users": [
    {
      "username": "testuser",
      "password_hash": "<sha256 of password>"
    }
  ]
}
```

Generate a hash:

```bash
echo -n "yourpassword" | sha256sum
```
