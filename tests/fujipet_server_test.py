#!/usr/bin/env python3
"""Drives tools/fujipet-server with two clients speaking the Palm's protocol."""
import asyncio
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools", "fujipet-server"))
import fujipet_server  # noqa: E402


class Client:
    def __init__(self, reader, writer):
        self.reader, self.writer = reader, writer

    @classmethod
    async def connect(cls, port):
        return cls(*await asyncio.open_connection("127.0.0.1", port))

    async def send(self, line):
        self.writer.write((line + "\n").encode())
        await self.writer.drain()

    async def line(self):
        raw = await asyncio.wait_for(self.reader.readline(), 2)
        return raw.decode().rstrip("\n").split("|")

    async def ask(self, line):
        await self.send(line)
        return await self.line()

    async def listing(self):
        await self.send("LIST")
        pets = []
        while True:
            fields = await self.line()
            if fields[0] == "END":
                return pets
            pets.append(fields)

    def close(self):
        self.writer.close()


class ServerTest(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.data = os.path.join(self.dir.name, "pets.json")
        await self.start()

    async def start(self):
        self.friends = fujipet_server.FriendServer(self.data, log=lambda text: None)
        self.server = await asyncio.start_server(self.friends.handle, "127.0.0.1", 0)
        self.port = self.server.sockets[0].getsockname()[1]

    async def asyncTearDown(self):
        self.server.close()
        await self.server.wait_closed()
        self.dir.cleanup()

    async def test_register_list_and_talk(self):
        a = await Client.connect(self.port)
        b = await Client.connect(self.port)

        self.assertEqual(await a.listing(), [])
        reply = await a.ask("REG|Pip|0|1|0")
        self.assertEqual(reply[:2], ["REGOK", "1"])
        self.assertEqual(len(reply[2]), 8)
        # b is connected but unregistered: it still hears who comes online.
        self.assertEqual(await b.line(), ["ON", "1", "Pip", "0", "1", "0"])

        self.assertEqual(await b.listing(), [["PET", "1", "Pip", "0", "1", "0", "1"]])
        reply = await b.ask("REG|Mochi|1|2|1")
        self.assertEqual(reply[:2], ["REGOK", "2"])
        self.assertEqual(await a.line(), ["ON", "2", "Mochi", "1", "2", "1"])

        self.assertEqual(await a.ask("SAY|2|1"), ["SENT", "2", "1"])
        self.assertEqual(await b.line(), ["MSG", "1", "Pip", "1"])
        self.assertEqual(await b.ask("SAY|1|5"), ["SENT", "1", "5"])
        self.assertEqual(await a.line(), ["MSG", "2", "Mochi", "5"])

        self.assertEqual(await a.ask("UPD|Pip|0|3|2"), ["OK", "UPD"])
        self.assertEqual(await b.line(), ["ON", "1", "Pip", "0", "3", "2"])
        self.assertEqual(await a.ask("PING"), ["PONG"])

        b.close()
        self.assertEqual(await a.line(), ["OFF", "2"])
        self.assertEqual(await a.ask("SAY|2|0"), ["ERR", "offline", "2"])
        self.assertEqual(await a.ask("SAY|1|0"), ["ERR", "offline", "1"])
        a.close()

    async def test_hello_needs_the_key_and_survives_a_restart(self):
        a = await Client.connect(self.port)
        _, pet_id, key = await a.ask("REG|Rex|3|4|5")
        a.close()
        await asyncio.sleep(0.1)

        self.server.close()
        await self.server.wait_closed()
        await self.start()
        with open(self.data) as f:
            self.assertEqual(json.load(f)["pets"][0]["name"], "Rex")

        c = await Client.connect(self.port)
        self.assertEqual(await c.ask(f"HELLO|{pet_id}|nope|4|6"), ["ERR", "key"])
        self.assertEqual(await c.ask("HELLO|99|x|0|0"), ["ERR", "unknown"])
        self.assertEqual(await c.ask(f"HELLO|{pet_id}|{key}|4|6"), ["HI", pet_id, "Rex"])
        self.assertEqual(await c.listing(), [["PET", pet_id, "Rex", "3", "4", "6", "1"]])
        c.close()

    async def test_rejects_bad_lines(self):
        a = await Client.connect(self.port)
        self.assertEqual(await a.ask("SAY|1|0"), ["ERR", "login"])
        self.assertEqual(await a.ask("REG|Pip|9|1|0"), ["ERR", "bad", "REG"])
        self.assertEqual(await a.ask("REG|||1|0"), ["ERR", "name"])
        self.assertEqual(await a.ask("DANCE"), ["ERR", "verb", "DANCE"])
        self.assertEqual((await a.ask("REG|A very long name indeed|2|1|0"))[0], "REGOK")
        self.assertEqual((await a.listing())[0][2], "A very lon")
        a.close()


if __name__ == "__main__":
    unittest.main()
