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

#pragma once

#include <unordered_map>
#include <string>

class ZotDB
{
public:
    ZotDB(std::string filename);

    ~ZotDB()
    {
        sync();
    }

    long value(const std::string &key) const
    {
        const auto iter = m_values.find(normalize(key));
        if (iter == m_values.end())
            return 0L;
        return iter->second;
    }

    long increment(const std::string &key)
    {
        return ++m_values[normalize(key)];
    }

    long decrement(const std::string &key)
    {
        return --m_values[normalize(key)];
    }

    // Write the database back to disk
    void sync() const;

private:
    static std::string normalize(const std::string &key);

    std::string m_filename;
    std::unordered_map<std::string, long> m_values;
};

struct Parsed
{
    enum { Invalid, Increment, Decrement, Query } m_op;
    std::string m_name;
};

Parsed parse_line(const std::string &line);
