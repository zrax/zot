#!/usr/bin/env python

import os, sys
import socket
import re

IDENTIFIER = r'[A-Za-z_]\w*(?:(?:\.|->|::)[A-Za-z_]\w*)*'
RE_PRE = re.compile(r'(?:^|[^\w+-])(\+\+|--)(%s)\b' % IDENTIFIER)
RE_POST = re.compile(r'\b(%s)(\+\+|--)(?:$|[^\w+-])' % IDENTIFIER)
RE_QUERY = re.compile(r'(?:^|[^\w+-])\?(%s)\b' % IDENTIFIER)
RE_PART = re.compile(r'(\.|->|::)')

ZOT_VERSION = "ZOT 0.2"
DBASE_FILE = "zot.db"

class zot:
    def __init__(self, addr, port, nick, channels):
        self.nick = nick
        self.dbase = { }
        self.load_dbase(DBASE_FILE)

        self.sock = socket.create_connection((addr, port))
        self.sock.send('USER %s . . %s\r\n' % (nick, nick))
        self.sock.send('NICK %s\r\n' % nick)
        for chan in channels:
            self.sock.send('JOIN #%s\r\n' % chan)

    def load_dbase(self, filename):
        db = open(filename, 'r')
        for line in db.readlines():
            try:
                k, v = line.split(':', 1)
                self.dbase[k] = int(v)
            except ValueError:
                pass
        db.close()

    def save_dbase(self, filename):
        db = open(filename, 'w')
        for k in self.dbase:
            db.write('%s:%s\n' % (k, str(self.dbase[k])))
        db.close()

    def run(self):
        packet = ''
        while True:
            packet += self.sock.recv(4096)
            if len(packet) == 0:
                break

            lines = packet.split('\r\n')
            for ln in lines[:-1]:
                self.parse(ln)

            packet = lines[-1] if len(lines[-1]) else ''

    def parse(self, line):
        if len(line) == 0:
            return

        sender = ''
        msg = line.split(None, 3)
        if line[0] == ':':
            sender = msg[0][1:]
            msg = msg[1:]
        if len(msg) < 1:
            return

        if msg[0] == 'PING':
            # Lazy: Save the dbase on a timer!
            self.sock.send('PONG ' + msg[1] + '\r\n')
            self.save_dbase(DBASE_FILE)

        elif msg[0] == 'PRIVMSG':
            try:
                recp = msg[1]
                text = msg[2]
            except IndexError:
                print "Got bad message!"
                return

            snick = sender.split('!')[0]
            if recp == self.nick:
                dest = snick
            else:
                dest = recp

            if text.startswith(':'):
                text = text[1:]

            match = RE_PRE.search(text)
            if match is not None:
                key = RE_PART.sub('.', match.group(2))
                value = 1 if match.group(1) == '++' else -1
                if key not in self.dbase:
                    self.dbase[key] = value
                else:
                    self.dbase[key] += value
                self.sendMsg(dest, '%s = %d' % (key, self.dbase[key]))
                return

            match = RE_POST.search(text)
            if match is not None:
                key = RE_PART.sub('.', match.group(1))
                value = 1 if match.group(2) == '++' else -1
                if key not in self.dbase:
                    self.dbase[key] = value
                else:
                    self.dbase[key] += value
                self.sendMsg(dest, '%s = %d' % (key, self.dbase[key]))
                return

            match = RE_QUERY.search(text)
            if match is not None:
                try:
                    key = RE_PART.sub('.', match.group(1))
                    value = self.dbase[key]
                except KeyError:
                    value = 0
                self.sendMsg(dest, '%s = %d' % (key, value))
                return

            if text.lower() == '\x01version\x01':
                self.sendNotice(snick, '\x01VERSION ' + ZOT_VERSION + '\x01')
                return

    def sendMsg(self, dest, msg):
        self.sock.send('PRIVMSG ' + dest + ' :' + msg + '\r\n')

    def sendNotice(self, dest, msg):
        self.sock.send('NOTICE ' + dest + ' :' + msg + '\r\n')


if len(sys.argv) < 4:
    print "Usage:  %s hostname port channel [channel2 [...]]" % sys.argv[0]
    sys.exit(1)

host = sys.argv[1]
port = int(sys.argv[2])
nick = sys.argv[3]
channels = sys.argv[4:]
server = zot(host, port, nick, channels)
server.run()
