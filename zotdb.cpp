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

#include <regex>
#include <stdio.h>

ZotDB::ZotDB(std::string filename)
    : m_filename(std::move(filename))
{
    FILE *db = fopen(m_filename.c_str(), "r");
    if (!db)
        return;

    char *line = nullptr;
    size_t count = 0;
    ssize_t size = 0;
    while ((size = getline(&line, &count, db)) >= 0) {
        while (size > 0 && (line[size - 1] == '\n' || line[size - 1] == '\r'))
            --size;
        std::string sline(line, size);
        if (sline.empty())
            continue;

        size_t split = sline.find(':');
        if (split == std::string::npos) {
            fprintf(stderr, "Warning: Invalid db line: \"%s\"\n", sline.c_str());
            continue;
        }

        std::string key(sline.substr(0, split));
        char *endp = nullptr;
        long value = strtol(sline.c_str() + split + 1, &endp, 0);
        if (*endp != 0) {
            fprintf(stderr, "Warning: Invalid db line: \"%s\"\n", sline.c_str());
            continue;
        }

        m_values.emplace(std::move(key), value);
    }

    free(line);
    fclose(db);
}

void ZotDB::sync() const
{
    FILE *db = fopen(m_filename.c_str(), "w");
    if (!db) {
        fprintf(stderr, "Failed to open %s for writing: %s\n", m_filename.c_str(),
                strerror(errno));
        return;
    }

    for (const auto &line : m_values)
        fprintf(db, "%s:%ld\n", line.first.c_str(), line.second);

    fclose(db);
}

std::string ZotDB::normalize(const std::string &key)
{
    const static std::regex re_seps("(::|->)");
    return std::regex_replace(key, re_seps, ".");
}
