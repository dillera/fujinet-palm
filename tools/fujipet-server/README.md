# FujiPet friend server

A small server for FN FujiPet (`apps/FujiPet`): pets register here, see which
others are registered and which are online, and pets online at the same time
send each other canned messages, delivered at once. Python 3, standard library
only.

```sh
python3 tools/fujipet-server/fujipet_server.py            # TCP 7878, status page on 7879
python3 tools/fujipet-server/fujipet_server.py --port 7878 --http-port 7879 \
        --data fujipet-pets.json
```

Registrations are kept in the `--data` file (default `fujipet-pets.json` in the
current folder), so they survive a restart; who is online is not.

On the Palm, set the server in FN FujiPet's **Options > Settings** as
`host:port`. A Palm reaches it through FujiNet, so `host` is as FujiNet sees
it: `localhost:7878` for FujiNet-PC on the same computer as the server, the
computer's LAN address for a FujiNet on the network.

The status page, `http://<host>:7879/`, lists the pets (online or not) and the
last 50 messages; `/pets.json` has the same as JSON.

## Protocol

Lines of ASCII ending in LF, fields split by `|`, at most 160 bytes. The
Palm opens `N1:TCP://host:7878/` and keeps the connection open while the app
runs.

| Client sends | Server replies |
|---|---|
| `REG\|name\|type\|stage\|age` | `REGOK\|id\|key`: a new pet; the app keeps the id and key |
| `HELLO\|id\|key\|stage\|age` | `HI\|id\|name`, or `ERR\|unknown` / `ERR\|key` |
| `UPD\|name\|type\|stage\|age` | `OK\|UPD`: the pet grew, or a new egg replaced it |
| `LIST` | `PET\|id\|name\|type\|stage\|age\|online` for each pet, then `END\|count` |
| `SAY\|id\|code` | `SENT\|id\|code`, or `ERR\|offline\|id` |
| `PING` | `PONG` |
| `BYE` | the server hangs up |

and sends, whenever it happens:

| Line | Means |
|---|---|
| `ON\|id\|name\|type\|stage\|age` | a pet came online, or changed |
| `OFF\|id` | a pet went offline |
| `MSG\|id\|name\|code` | a message to this pet |

`type` is 0 Cat, 1 Dog, 2 Bunny, 3 Dino; `stage` 0 Egg, 1 Baby, 2 Child,
3 Teen, 4 Adult, 5 Senior, 6 Angel; `code` one of the eight canned messages
(`MESSAGES` in the server, the same list in `apps/FujiPet/petproto.c`). A
client that sends nothing for 90 seconds is dropped, so the app sends `PING`
every 20. `tests/fujipet_server_test.py` exercises all of it.
