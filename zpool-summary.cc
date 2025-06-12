// Copyright (c) 2022 Mikael Simonsson <https://mikaelsimonsson.com>.
// SPDX-License-Identifier: BSL-1.0

#include "snn-core/main.hh"
#include "snn-core/algo/remove_if.hh"
#include "snn-core/chr/common.hh"
#include "snn-core/file/standard/out.hh"
#include "snn-core/fmt/byte_size.hh"
#include "snn-core/fn/common.hh"
#include "snn-core/map/sorted.hh"
#include "snn-core/map/unsorted.hh"
#include "snn-core/process/execute.hh"
#include "snn-core/range/view/reverse.hh"
#include "snn-core/string/range/split.hh"

namespace snn::app
{
    struct pool_meta final
    {
        usize avail{};
        usize used{};
    };

    namespace
    {
        constexpr bool is_valid_pool_name(const cstrview name) noexcept
        {
            // From `man zpool-create`: "The pool name must begin with a letter, and can only
            // contain alphanumeric characters as well as the underscore ("_"), dash ("-"), colon
            // (":"), space (" "), and period (".")."
            // Note: Some names are reserved but that is not relevant here.
            auto rng = name.range();
            if (rng.drop_front_if(chr::is_alpha))
            {
                rng.pop_front_while(fn::is_any_of{chr::is_alphanumeric,
                                                  fn::in_array{'_', '-', ':', ' ', '.'}});
                return rng.is_empty();
            }
            return false;
        }

        map::sorted<str, pool_meta> parse_list(strbuf output)
        {
            map::sorted<str, pool_meta> pools;

            for (const cstrview line : string::range::split{output, '\n'})
            {
                if (line.is_empty()) continue;

                str name;
                str property;
                usize value{};

                string::range::split columns{line, '\t'};

                if (columns)
                {
                    name = columns.pop_front().value();
                }

                if (columns)
                {
                    property = columns.pop_front().value();
                }

                if (columns)
                {
                    value = columns.pop_front().value().to<usize>().value_or_default();
                }
                else
                {
                    // Too few columns.
                    return {};
                }

                // Trailing columns?
                if (columns) return {};

                // Validate (don't propagate garbage data if the output format changed or if an
                // error occurred).

                // Empty/zero values?
                if (name.is_empty() || property.is_empty() || value == 0) return {};

                // Invalid name?
                if (!app::is_valid_pool_name(name)) return {};

                auto& meta = pools.insert(std::move(name), pool_meta{}).value();

                if (property == "available")
                {
                    meta.avail = value;
                }
                else if (property == "used")
                {
                    meta.used = value;
                }
                else
                {
                    // Unknown property.
                    return {};
                }
            }

            return pools;
        }

        map::unsorted<str, bool> parse_status(strbuf output)
        {
            map::unsorted<str, bool> statuses;

            output.replace('\t', ' ');

            // Remove indentation and replace consecutive spaces with a single space.
            output.remove_if([prev = '\n'](const char c) mutable {
                if (c == ' ' && fn::in_array{' ', '\n'}(prev))
                {
                    return true;
                }
                prev = c;
                return false;
            });

            str name;

            string::range::split lines{output, '\n'};
            while (lines)
            {
                cstrview line = lines.pop_front().value();

                if (line.has_front("pool: "))
                {
                    name = line.view(6);
                    continue;
                }

                if (!line.has_front("NAME STATE"))
                {
                    continue;
                }

                usize error_count    = 0;
                usize no_error_count = 0;

                while (lines)
                {
                    line = lines.pop_front().value();

                    if (line.is_empty())
                    {
                        break;
                    }

                    if (line.has_back("ONLINE 0 0 0"))
                    {
                        ++no_error_count;
                    }
                    else
                    {
                        ++error_count;
                    }
                }

                if (name)
                {
                    const bool has_errors = error_count > 0 || no_error_count == 0;
                    statuses.insert(name, has_errors);
                }

                name.clear();
            }

            return statuses;
        }

        strbuf command_output(process::command cmd)
        {
            strbuf buffer{init::reserve, 800};

            auto output = process::execute_and_consume_output(cmd);
            while (const auto res = output.read_line<cstrview>())
            {
                buffer.append(res.value());
            }

            return buffer;
        }

        auto list_pools()
        {
            process::command cmd;
            cmd << "zfs get -d 0 -Hp -o name,property,value available,used 2>/dev/null";
            return app::parse_list(app::command_output(cmd));
        }

        auto stat_pools()
        {
            process::command cmd;
            cmd << "zpool status 2>/dev/null";
            return app::parse_status(app::command_output(cmd));
        }
    }
}

namespace snn
{
    int main(array_view<const env::argument>)
    {
        // This app is primarily for status bars, it should always print something and never return
        // a non-successful exit status. `zpool list` can be used to detect if ZFS is in use.

        str output;

        const auto pools = app::list_pools();
        if (pools)
        {
            const auto statuses = app::stat_pools();

            // Reverse so "zroot" is more likely to be listed first.
            for (const auto& [name, meta] : pools.range() | range::v::reverse{})
            {
                const auto has_errors = statuses.get<bool>(name).value_or(true);

                const auto size = meta.avail + meta.used;

                // Is low? (Less than 5% available for 1 TB and up, or less than 10% otherwise).
                const usize divisor = (size >= constant::size::terabyte<usize>) ? 20 : 10;
                const bool is_low   = meta.avail < (size / divisor);

                // Smaller than 5 GB? Assume bootpool.
                const bool is_bootpool = size < (5 * constant::size::gigabyte<usize>);

                if (!is_bootpool || has_errors || is_low)
                {
                    if (output)
                    {
                        output << ' ';
                    }

                    const auto formatted_size =
                        fmt::byte_size<1024>(byte_size{meta.avail},
                                             fmt::table::byte_size::iec_short, ".", "");

                    output << name << ": " << formatted_size;

                    if (has_errors)
                    {
                        output << " (ERRORS)";
                    }
                    else if (is_low)
                    {
                        output << " (low)";
                    }
                }
            }

            output << '\n';
        }
        else
        {
            output << "Unknown\n";
        }

        file::standard::out{} << output;

        return constant::exit::success;
    }
}
