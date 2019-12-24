/* This file is part of zot.
 *
 * zot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * zot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with zot.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "irccli.h"

#include <asio/connect.hpp>
#include <asio/read_until.hpp>
#include <asio/write.hpp>
#include <asio/signal_set.hpp>

//#define DEBUGIO

IrcClient::IrcClient(const char *dbfile, const char *hostname, const char *port,
                     const char *nick)
    : m_db(dbfile), m_state(SDisconnected),
      m_resolver(m_ioctx), m_sock(m_ioctx),
      m_ping(m_ioctx), m_timeout(m_ioctx), m_savetime(m_ioctx),
      m_signals(m_ioctx, SIGINT, SIGTERM),
      m_hostname(hostname), m_port(port), m_nick(nick)
{
    connect();

    m_signals.async_wait([this](const asio::error_code &err, int) {
        if (err) {
            if (err.value() != asio::error::operation_aborted)
                fprintf(stderr, "Signal wait failed: %s\n", err.message().c_str());
            return;
        }
        disconnect();
    });
}

void IrcClient::disconnect()
{
    if (m_state == SJoined)
        send("QUIT :--zot!\r\n", [this] { m_sock.close(); });
    else
        m_sock.close();

    m_state = SShutdown;
    m_resolver.cancel();
    m_ping.cancel();
    m_timeout.cancel();
    m_savetime.cancel();
    m_signals.cancel();
}

void IrcClient::connect()
{
    m_resolver.async_resolve(m_hostname, m_port, tcp::resolver::address_configured,
            [this](const asio::error_code &err, tcp::resolver::results_type results) {
        if (err) {
            if (err.value() != asio::error::operation_aborted) {
                fprintf(stderr, "Failed to resolve %s:%s: %s\n",
                        m_hostname.c_str(), m_port.c_str(), err.message().c_str());
            }
            // Don't attempt reconnect here...
            disconnect();
            return;
        }

        printf("Connecting to %s:%s\n", m_hostname.c_str(), m_port.c_str());
        m_timeout.expires_after(std::chrono::seconds(60));
        m_timeout.async_wait([this](const asio::error_code &err) {
            if (!err) {
                fprintf(stderr, "Connection timed out\n");
                reset_connection();
            }
        });
        asio::async_connect(m_sock, results,
                [this](const asio::error_code &err, const tcp::endpoint &endp) {
            if (err) {
                if (err.value() != asio::error::operation_aborted) {
                    fprintf(stderr, "Failed to connect to %s:%s: %s\n",
                            m_hostname.c_str(), m_port.c_str(), err.message().c_str());
                    reset_connection();
                }
                return;
            }

            printf("Connected to %s:%hd\n", endp.address().to_string().c_str(),
                   endp.port());
            m_timeout.cancel();
            m_state = SConnected;
            send_ident();
            start_read();
            start_ping_timer();
        });
    });
}

// Split a line up to the first component (excluding the beginning of line)
// that starts with a ':'
static std::vector<std::string> irc_split(const std::string &line)
{
    std::vector<std::string> parts;
    size_t start = 0, scan = 0;
    while (scan < line.size()) {
        if (isspace(line[scan])) {
            parts.emplace_back(line.substr(start, scan - start));
            while (scan < line.size() && isspace(line[scan]))
                ++scan;
            start = scan;
            if (line[start] == ':') {
                parts.emplace_back(line.substr(start));
                break;
            }
        } else {
            ++scan;
        }
    }
    if (start != scan)
        parts.emplace_back(line.substr(start));
    return parts;
}

void IrcClient::start_read()
{
    asio::async_read_until(m_sock, asio::dynamic_buffer(m_inbuf), '\n',
                           [this](const asio::error_code &err, size_t length) {
        if (err) {
            if (err.value() == asio::error::eof)
                printf("Server closed the connection.\n");
            else if (err.value() != asio::error::operation_aborted)
                fprintf(stderr, "Failed to read from server: %s\n", err.message().c_str());
            reset_connection();
            return;
        }

        std::string line = m_inbuf.substr(0, length);
        m_inbuf.erase(0, length);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();

#ifdef DEBUGIO
        printf("<== %s\n", line.c_str());
#endif

        if (m_state == SConnected)
            perform_join();

        // Process line.  We ignore most messages, and just process the
        // ones we care about.
        auto parts = irc_split(line);
        if (parts[0] == "PING" && parts.size() >= 2) {
            send("PONG " + parts[1] + "\r\n");
        } else if (parts.size() > 1 && parts[1] == "PONG") {
            m_timeout.cancel();
        } else if (parts.size() > 3 && parts[1] == "PRIVMSG") {
            auto bang = parts[0].find('!');
            auto sender = (bang != std::string::npos) ? parts[0].substr(0, bang) : parts[0];
            if (!sender.empty() && sender.front() == ':')
                sender = sender.substr(1);

            auto dest = parts[2];
            if (!dest.empty() && dest.front() == ':')
                dest = dest.substr(1);
            if (dest == m_nick)
                dest = sender;

            auto message = parts[3];
            if (!message.empty() && message.front() == ':')
                message = message.substr(1);

            auto p = parse_line(message);
            long value = 0;
            switch (p.m_op) {
            case Parsed::Increment:
                value = m_db.increment(p.m_name);
                break;
            case Parsed::Decrement:
                value = m_db.decrement(p.m_name);
                break;
            case Parsed::Query:
                value = m_db.value(p.m_name);
                break;
            default:
                break;
            }

            if (p.m_op != Parsed::Invalid)
                send("PRIVMSG " + dest + " :" + p.m_name + " = " + std::to_string(value) + "\r\n");
        }

        // Setup read for next line
        start_read();
        start_ping_timer();
    });
}

void IrcClient::send_ident()
{
    // Minimal identification necessary to satisfy the IRC server
    send("NICK " + m_nick + "\r\n"
         "USER " + m_nick + " . . :" + m_nick + "\r\n");
}

void IrcClient::perform_join()
{
    for (const auto &chan : m_channels)
        send("JOIN #" + chan + "\r\n");

    // We're not actually joined yet, but we've requested joins on all
    // specified channels...
    m_state = SJoined;
}

void IrcClient::start_ping_timer()
{
    m_ping.expires_after(std::chrono::minutes(5));
    m_ping.async_wait([this](const asio::error_code &err) {
        if (err)
            return;

        m_timeout.expires_after(std::chrono::seconds(60));
        m_timeout.async_wait([this](const asio::error_code &err) {
            if (err)
                return;

            fprintf(stderr, "No PING response from server\n");
            reset_connection();
        });
        send("PING :zot\r\n");
    });
}

void IrcClient::start_save_timer()
{
    m_savetime.expires_after(std::chrono::minutes(15));
    m_savetime.async_wait([this](const asio::error_code &err) {
        if (err)
            return;

        m_db.sync();
        start_save_timer();
    });
}

void IrcClient::reset_connection()
{
    m_sock.close();
    if (m_state == SShutdown)
        return;

    m_state = SDisconnected;
    fprintf(stderr, "Reconnecting in 60 sec...\n");
    m_timeout.expires_after(std::chrono::seconds(60));
    m_timeout.async_wait([this](const asio::error_code &err) {
        if (!err)
            connect();
    });
}

void IrcClient::send_op(IrcClient::WriteOperation *op)
{
#ifdef DEBUGIO
    printf("==> %s", op->m_text.c_str());
#endif

    m_writes.insert(op);
    asio::async_write(m_sock, asio::buffer(op->m_text),
                      [this, op](const asio::error_code &err, size_t) {
        if (err) {
            fprintf(stderr, "Failed to write to socket: %s\n", err.message().c_str());
            reset_connection();
        } else {
            if (op->m_next)
                op->m_next();
        }
        m_writes.erase(op);
        delete op;
    });
}
