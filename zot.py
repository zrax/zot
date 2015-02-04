#!/usr/bin/env python

import os, sys
import socket
import re

import irc.bot
import irc.client

IDENTIFIER = r'[A-Za-z_]\w*(?:(?:\.|->|::)[A-Za-z_]\w*)*'
WSPACE = r'(?:\s|/\*(?:[^*]|\*[^/])*\*+/)'
RE_PRE = re.compile(r'^%s*(\+\+|--)%s*(%s)(?:%s|;)*(?://.*)?$' % \
                    (WSPACE, WSPACE, IDENTIFIER, WSPACE))
RE_POST = re.compile(r'^%s*(%s)%s*(\+\+|--)(?:%s|;)*(?://.*)?$' % \
                     (WSPACE, IDENTIFIER, WSPACE, WSPACE))
RE_QUERY = re.compile(r'^%s*\?%s*(%s)(?:%s|;)*(?://.*)?$' % \
                      (WSPACE, WSPACE, IDENTIFIER, WSPACE))
RE_PART = re.compile(r'(\.|->|::)')

ZOT_VERSION = "ZOT 0.4"
DBASE_FILE = "zot.db"

# Fix UTF-8 decoding errors
irc.client.ServerConnection.buffer_class.errors = 'replace'

class zot(irc.bot.SingleServerIRCBot):
    def __init__(self, addr, port, nick, channels):
        irc.bot.SingleServerIRCBot.__init__(self, [(addr, port)], nick, nick)
        self.channels_to_join = channels

    @staticmethod
    def normalize(ident):
        return RE_PART.sub('.', ident).lower()

    def load_dbase(self):
        self.dbase = { }
        db = open(DBASE_FILE, 'r')
        for line in db.readlines():
            try:
                k, v = line.split(':', 1)
                self.dbase[k] = int(v)
            except ValueError:
                pass
        db.close()

    def save_dbase(self):
        db = open(DBASE_FILE, 'w')
        for k in self.dbase:
            db.write('%s:%s\n' % (k, str(self.dbase[k])))
        db.close()

    def update(self, key, delta):
        if key not in self.dbase:
            self.dbase[key] = delta
        else:
            self.dbase[key] += delta

    def do_math(self, key, oper, snick):
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

    def parse(self, conn, event, is_privmsg):
        snick = event.source.nick
        text = event.arguments[0]
        match = RE_PRE.search(text)
        if match is not None:
            key = zot.normalize(match.group(2))
            conn.privmsg(event.target, self.do_math(key, match.group(1), snick))
            return

        match = RE_POST.search(text)
        if match is not None:
            key = zot.normalize(match.group(1))
            conn.privmsg(event.target, self.do_math(key, match.group(2), snick))
            return

        match = RE_QUERY.search(text)
        if match is not None:
            try:
                key = zot.normalize(match.group(1))
                value = self.dbase[key]
            except KeyError:
                value = 0
            conn.privmsg(event.target, '%s = %d' % (key, value))
            return


    #
    # IRC Bot events
    #
    def on_nicknameinuse(self, conn, event):
        print >>sys.stderr, "Nick is already in use"
        sys.exit(1)

    def on_welcome(self, conn, event):
        print >>sys.stderr, "Got connection, joining channels"
        for chan in self.channels_to_join:
            conn.join('#' + chan)

    def on_privmsg(self, conn, event):
        self.parse(conn, event, False)

    def on_pubmsg(self, conn, event):
        self.parse(conn, event, True)

    def on_ctcp(self, conn, event):
        snick = event.source.nick
        if event.arguments[0] == 'VERSION':
            conn.ctcp_reply(snick, 'VERSION ' + ZOT_VERSION)
        else:
            irc.bot.SingleServerIRCBot.on_ctcp(self, conn, event)


if len(sys.argv) < 5:
    print "Usage:  %s hostname port nick channel [channel2 [...]]" % sys.argv[0]
    sys.exit(1)

host = sys.argv[1]
port = int(sys.argv[2])
nick = sys.argv[3]
channels = sys.argv[4:]

server = zot(host, port, nick, channels)
server.load_dbase()
try:
    server.start()
finally:
    server.save_dbase()
