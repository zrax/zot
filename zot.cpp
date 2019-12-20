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

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Usage:  %s hostname port nick [channel [...]]\n", argv[0]);
        return 1;
    }

    IrcClient client("zot.db", argv[1], argv[2], argv[3]);
    for (int i = 4; i < argc; ++i)
        client.join_channel(argv[i]);

    client.run();

    return 0;
}
