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

#include "zotdb.h"

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <string>
#include <vector>
#include <set>
#include <functional>

using tcp = asio::ip::tcp;

class IrcClient
{
public:
    IrcClient(const char *dbfile, const char *hostname, const char *port,
              const char *nick);

    void join_channel(const char *channel)
    {
        m_channels.emplace_back(channel);
    }

    void run()
    {
        m_ioctx.run();
    }

    void disconnect();

private:
    void connect();
    void start_read();
    void send_ident();
    void perform_join();

    void start_ping_timer();
    void start_save_timer();
    void reset_connection();

    ZotDB m_db;

    enum { SDisconnected, SConnected, SJoined, SShutdown } m_state;

    asio::io_context m_ioctx;
    tcp::resolver m_resolver;
    tcp::socket m_sock;
    asio::steady_timer m_ping;
    asio::steady_timer m_timeout;
    asio::steady_timer m_savetime;
    asio::signal_set m_signals;

    std::string m_hostname;
    std::string m_port;
    std::string m_nick;
    std::vector<std::string> m_channels;
    std::string m_inbuf;

    struct WriteOperation
    {
        std::string m_text;
        std::function<void()> m_next;
    };
    std::set<WriteOperation *> m_writes;

    void send(std::string text)
    {
        send_op(new WriteOperation { std::move(text), nullptr });
    }

    template <typename Callable>
    void send(std::string text, Callable &&next)
    {
        send_op(new WriteOperation { std::move(text), std::forward<Callable>(next) });
    }

    void send_op(WriteOperation *op);
};
