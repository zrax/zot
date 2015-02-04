#!/usr/bin/env python

import os, sys
import socket
import re

IDENTIFIER = r'[A-Za-z_]\w*(?:(?:\.|->|::)[A-Za-z_]\w*)*'
WSPACE = r'(?:\s|/\*(?:[^*]|\*[^/])*\*+/)'
RE_PRE = re.compile(r'^%s*(\+\+|--)%s*(%s)(?:%s|;)*(?://.*)?$' % \
                    (WSPACE, WSPACE, IDENTIFIER, WSPACE))
RE_POST = re.compile(r'^%s*(%s)%s*(\+\+|--)(?:%s|;)*(?://.*)?$' % \
                     (WSPACE, IDENTIFIER, WSPACE, WSPACE))
RE_QUERY = re.compile(r'^%s*\?%s*(%s)(?:%s|;)*(?://.*)?$' % \
                      (WSPACE, WSPACE, IDENTIFIER, WSPACE))
RE_PART = re.compile(r'(\.|->|::)')

ZOT_VERSION = "ZOT 0.3"
DBASE_FILE = "zot.db"

class zot:
    def __init__(self, addr, port, nick, channels):
        self.server = (addr, port)
        self.nick = nick
        self.channels = channels

        self.dbase = { }
        self.load_dbase(DBASE_FILE)

        self.sock = None
        self.connect()

    def connect(self):
        print("Connecting...")
        if self.sock:
            self.sock.shutdown(socket.SHUT_RDWR)
            self.sock.close()
        self.sock = socket.create_connection(self.server, 600.0)
        self.sock.send('USER %s . . %s\r\n' % (self.nick, self.nick))
        self.sock.send('NICK %s\r\n' % self.nick)
        for chan in self.channels:
            self.sock.send('JOIN #%s\r\n' % chan)

    @staticmethod
    def normalize(ident):
        return RE_PART.sub('.', ident).lower()

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
        bad_time = 0
        while True:
            try:
                packet += self.sock.recv(4096)
            except socket.timeout:
                # Reconnect and try again
                bad_time += 1
                if bad_time > 3:
                    print >>sys.stderr, "Having trouble staying connected...  Bailing out!"
                    return
                self.connect()
                continue
            if len(packet) == 0:
                break

            bad_time = 0
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
                key = zot.normalize(match.group(2))
                self.sendMsg(dest, self.doMath(key, match.group(1), snick))
                return

            match = RE_POST.search(text)
            if match is not None:
                key = zot.normalize(match.group(1))
                self.sendMsg(dest, self.doMath(key, match.group(2), snick))
                return

            match = RE_QUERY.search(text)
            if match is not None:
                try:
                    key = zot.normalize(match.group(1))
                    value = self.dbase[key]
                except KeyError:
                    value = 0
                self.sendMsg(dest, '%s = %d' % (key, value))
                return

            if text.lower() == '\x01version\x01':
                self.sendNotice(snick, '\x01VERSION ' + ZOT_VERSION + '\x01')
                return

    def update(self, key, delta):
        if key not in self.dbase:
            self.dbase[key] = delta
        else:
            self.dbase[key] += delta

    def doMath(self, key, oper, snick):
        snick = zot.normalize(snick)
        if oper == '++':
            if key == snick:
                self.update(key, -1)
                return '%s = %d  // Thou shalt not increment thyself!' \
                       % (key, self.dbase[key])
            elif key == 'eap':
                self.update(snick, -1)
                self.update(key, -1)
                return '%s = %d, %s = %d  // Thou shalt not increment eap!' \
                       % (key, self.dbase[key], snick, self.dbase[snick])
            else:
                self.update(key, 1)
        else:
            self.update(key, -1)
        return '%s = %d' % (key, self.dbase[key])

    def sendMsg(self, dest, msg):
        self.sock.send('PRIVMSG ' + dest + ' :' + msg + '\r\n')

    def sendNotice(self, dest, msg):
        self.sock.send('NOTICE ' + dest + ' :' + msg + '\r\n')


if len(sys.argv) < 5:
    print "Usage:  %s hostname port nick channel [channel2 [...]]" % sys.argv[0]
    sys.exit(1)

host = sys.argv[1]
port = int(sys.argv[2])
nick = sys.argv[3]
channels = sys.argv[4:]
server = zot(host, port, nick, channels)
server.run()
