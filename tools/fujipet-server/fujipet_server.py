#!/usr/bin/env python3
"""FujiPet friend server, for FN FujiPet on the Palm (apps/FujiPet).

Pets register here, see which others are registered and which are online,
and pets online at the same time send each other canned messages. A Palm
reaches it through FujiNet's N1: device as N1:TCP://host:7878/.

The protocol is lines of ASCII ending in LF, fields split by '|', at most
160 bytes. Client to server:

    REG|name|type|stage|age        register a new pet  -> REGOK|id|key
    HELLO|id|key|stage|age         come back online    -> HI|id|name
    UPD|name|type|stage|age        the pet changed     -> OK|UPD
    LIST                           every pet           -> PET|id|name|type|stage|age|online ...
                                                          END|count
    SAY|id|code                    a canned message    -> SENT|id|code, or ERR|offline|id
    PING                                               -> PONG
    BYE                                                   (the server hangs up)

Server to client, whenever it happens:

    ON|id|name|type|stage|age      a pet came online, or changed
    OFF|id                         a pet went offline
    MSG|id|name|code               a message to this pet

Errors are ERR|reason. A pet is online while its connection is: a client
that sends nothing for IDLE_TIMEOUT seconds is dropped, so clients PING.
type, stage and code are numbers; TYPES, STAGES and MESSAGES name them, in
the same order as apps/FujiPet/pet.c and petproto.c.

A status page on the HTTP port lists the pets and the last messages;
/pets.json has the same as JSON.
"""
import argparse
import asyncio
import collections
import html
import json
import os
import secrets
import time

TYPES = ["Cat", "Dog", "Bunny", "Dino"]
STAGES = ["Egg", "Baby", "Child", "Teen", "Adult", "Senior", "Angel"]
MESSAGES = [
    "Hi, friend!",
    "Want to play?",
    "I'm hungry!",
    "You're the best!",
    "Let's be pals!",
    "Yay! Hooray!",
    "Good night!",
    "Bye bye!",
]
NAME_MAX = 10
MAX_LINE = 160
IDLE_TIMEOUT = 90
MAX_AGE = 99


def log_line(text):
    print(time.strftime("%H:%M:%S"), text, flush=True)


def clean_name(text):
    """Letters, digits, spaces and a few marks; no field separator."""
    keep = "".join(c for c in text if c.isalnum() or c in " -'.")
    return keep.strip()[:NAME_MAX]


def number(text, low, high):
    value = int(text)
    if not low <= value <= high:
        raise ValueError(text)
    return value


class Session:
    def __init__(self, writer):
        self.writer = writer
        self.pet_id = None
        peer = writer.get_extra_info("peername")
        self.peer = f"{peer[0]}:{peer[1]}" if peer else "?"

    def send(self, *fields):
        line = "|".join(str(f) for f in fields) + "\n"
        try:
            self.writer.write(line.encode("latin-1", "replace"))
        except (ConnectionError, RuntimeError):
            pass


class FriendServer:
    def __init__(self, data_path, log=log_line):
        self.data_path = data_path
        self.log = log
        self.pets = {}
        self.next_id = 1
        self.online = {}
        self.sessions = set()
        self.messages = collections.deque(maxlen=50)
        self.load()

    # ---- Storage -------------------------------------------------------

    def load(self):
        if not self.data_path or not os.path.exists(self.data_path):
            return
        with open(self.data_path) as f:
            data = json.load(f)
        self.next_id = data.get("next_id", 1)
        for pet in data.get("pets", []):
            self.pets[pet["id"]] = pet

    def save(self):
        if not self.data_path:
            return
        data = {"next_id": self.next_id, "pets": sorted(self.pets.values(), key=lambda p: p["id"])}
        tmp = self.data_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(data, f, indent=1)
        os.replace(tmp, self.data_path)

    # ---- Presence ------------------------------------------------------

    def broadcast(self, skip, *fields):
        for session in self.sessions:
            if session is not skip:
                session.send(*fields)

    def announce(self, session, pet):
        self.broadcast(session, "ON", pet["id"], pet["name"], pet["type"], pet["stage"], pet["age"])

    def login(self, session, pet):
        old = self.online.get(pet["id"])
        if old is not None and old is not session:
            # The same pet from a new connection: the old one is stale.
            old.pet_id = None
            old.writer.close()
        session.pet_id = pet["id"]
        self.online[pet["id"]] = session
        pet["seen"] = int(time.time())
        self.announce(session, pet)

    def drop(self, session):
        self.sessions.discard(session)
        pet_id = session.pet_id
        if pet_id is not None and self.online.get(pet_id) is session:
            del self.online[pet_id]
            self.pets[pet_id]["seen"] = int(time.time())
            self.save()
            self.broadcast(session, "OFF", pet_id)
            self.log(f"{self.pets[pet_id]['name']} (#{pet_id}) went offline")

    # ---- Commands ------------------------------------------------------

    def dispatch(self, session, line):
        fields = line.split("|")
        verb = fields[0].upper()
        handler = getattr(self, "cmd_" + verb.lower(), None)
        if handler is None:
            session.send("ERR", "verb", verb[:16])
            return
        try:
            handler(session, fields[1:])
        except (ValueError, IndexError):
            session.send("ERR", "bad", verb)

    def cmd_reg(self, session, args):
        if session.pet_id is not None:
            session.send("ERR", "already")
            return
        name = clean_name(args[0])
        if not name:
            session.send("ERR", "name")
            return
        pet = {
            "id": self.next_id,
            "key": secrets.token_hex(4),
            "name": name,
            "type": number(args[1], 0, len(TYPES) - 1),
            "stage": number(args[2], 0, len(STAGES) - 1),
            "age": number(args[3], 0, MAX_AGE),
            "registered": int(time.time()),
        }
        self.next_id += 1
        self.pets[pet["id"]] = pet
        self.save()
        session.send("REGOK", pet["id"], pet["key"])
        self.log(f"registered {name} the {TYPES[pet['type']]} as #{pet['id']} from {session.peer}")
        self.login(session, pet)

    def cmd_hello(self, session, args):
        pet = self.pets.get(int(args[0]))
        if pet is None:
            session.send("ERR", "unknown")
            return
        if args[1] != pet["key"]:
            session.send("ERR", "key")
            return
        pet["stage"] = number(args[2], 0, len(STAGES) - 1)
        pet["age"] = number(args[3], 0, MAX_AGE)
        session.send("HI", pet["id"], pet["name"])
        self.log(f"{pet['name']} (#{pet['id']}) is online from {session.peer}")
        self.login(session, pet)
        self.save()

    def cmd_upd(self, session, args):
        pet = self.pets.get(session.pet_id)
        if pet is None:
            session.send("ERR", "login")
            return
        name = clean_name(args[0])
        if name:
            pet["name"] = name
        pet["type"] = number(args[1], 0, len(TYPES) - 1)
        pet["stage"] = number(args[2], 0, len(STAGES) - 1)
        pet["age"] = number(args[3], 0, MAX_AGE)
        self.save()
        session.send("OK", "UPD")
        self.announce(session, pet)

    def cmd_list(self, session, args):
        pets = sorted(self.pets.values(),
                      key=lambda p: (p["id"] not in self.online, p["name"].lower()))
        for pet in pets:
            session.send("PET", pet["id"], pet["name"], pet["type"], pet["stage"], pet["age"],
                         1 if pet["id"] in self.online else 0)
        session.send("END", len(pets))

    def cmd_say(self, session, args):
        sender = self.pets.get(session.pet_id)
        if sender is None:
            session.send("ERR", "login")
            return
        to_id = int(args[0])
        code = number(args[1], 0, len(MESSAGES) - 1)
        target = self.online.get(to_id)
        if target is None or to_id == sender["id"]:
            session.send("ERR", "offline", to_id)
            return
        target.send("MSG", sender["id"], sender["name"], code)
        session.send("SENT", to_id, code)
        self.messages.append((int(time.time()), sender["name"], self.pets[to_id]["name"],
                              MESSAGES[code]))
        self.log(f"{sender['name']} -> {self.pets[to_id]['name']}: {MESSAGES[code]}")

    def cmd_ping(self, session, args):
        session.send("PONG")

    def cmd_bye(self, session, args):
        session.writer.close()

    # ---- Connections ---------------------------------------------------

    async def handle(self, reader, writer):
        session = Session(writer)
        self.sessions.add(session)
        try:
            while True:
                try:
                    raw = await asyncio.wait_for(reader.readline(), IDLE_TIMEOUT)
                except ValueError:  # longer than the stream's limit
                    break
                if not raw:
                    break
                if len(raw) > MAX_LINE:
                    session.send("ERR", "long")
                    continue
                line = raw.decode("latin-1").strip("\r\n")
                if line:
                    self.dispatch(session, line)
                await writer.drain()
        except (asyncio.TimeoutError, ConnectionError):
            pass
        finally:
            self.drop(session)
            writer.close()

    # ---- Status page ---------------------------------------------------

    def status(self):
        pets = []
        for pet in sorted(self.pets.values(), key=lambda p: p["id"]):
            pets.append({
                "id": pet["id"], "name": pet["name"], "type": TYPES[pet["type"]],
                "stage": STAGES[pet["stage"]], "age": pet["age"],
                "online": pet["id"] in self.online,
            })
        messages = [{"time": t, "from": a, "to": b, "text": m} for t, a, b, m in self.messages]
        return {"pets": pets, "messages": messages}

    def status_html(self):
        data = self.status()
        rows = "".join(
            f"<tr><td>#{p['id']}</td><td>{html.escape(p['name'])}</td><td>{p['type']}</td>"
            f"<td>{p['stage']}</td><td>{p['age']}</td>"
            f"<td>{'online' if p['online'] else ''}</td></tr>" for p in data["pets"])
        log = "".join(
            f"<li>{time.strftime('%H:%M:%S', time.localtime(m['time']))} "
            f"<b>{html.escape(m['from'])}</b> to <b>{html.escape(m['to'])}</b>: "
            f"{html.escape(m['text'])}</li>" for m in reversed(data["messages"]))
        return (
            "<!doctype html><meta charset=utf-8><meta http-equiv=refresh content=5>"
            "<title>FujiPet friends</title>"
            "<style>body{font-family:sans-serif;margin:2em}td,th{padding:2px 12px;text-align:left}"
            "</style><h1>FujiPet friends</h1>"
            f"<table><tr><th>ID</th><th>Name</th><th>Type</th><th>Stage</th><th>Age</th>"
            f"<th></th></tr>{rows}</table><h2>Messages</h2><ul>{log or '<li>none yet</li>'}</ul>")

    async def handle_http(self, reader, writer):
        try:
            request = await asyncio.wait_for(reader.readline(), 10)
            while (await asyncio.wait_for(reader.readline(), 10)) not in (b"\r\n", b"\n", b""):
                pass
            parts = request.decode("latin-1").split()
            path = parts[1] if len(parts) > 1 else "/"
            if path == "/pets.json":
                body, kind, code = json.dumps(self.status(), indent=1), "application/json", "200 OK"
            elif path == "/":
                body, kind, code = self.status_html(), "text/html; charset=utf-8", "200 OK"
            else:
                body, kind, code = "not found\n", "text/plain", "404 Not Found"
            data = body.encode()
            writer.write(f"HTTP/1.0 {code}\r\nContent-Type: {kind}\r\n"
                         f"Content-Length: {len(data)}\r\nConnection: close\r\n\r\n".encode() + data)
            await writer.drain()
        except (asyncio.TimeoutError, ConnectionError):
            pass
        finally:
            writer.close()


async def serve(args):
    server = FriendServer(args.data)
    pets = await asyncio.start_server(server.handle, args.host, args.port)
    web = await asyncio.start_server(server.handle_http, args.host, args.http_port)
    print(f"FujiPet friend server: pets on TCP {args.port}, status page on "
          f"http://{args.host}:{args.http_port}/, {len(server.pets)} pets in {args.data}",
          flush=True)
    async with pets, web:
        await asyncio.gather(pets.serve_forever(), web.serve_forever())


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=7878)
    parser.add_argument("--http-port", type=int, default=7879)
    parser.add_argument("--data", default="fujipet-pets.json",
                        help="where registrations are kept (default: %(default)s)")
    args = parser.parse_args()
    try:
        asyncio.run(serve(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
