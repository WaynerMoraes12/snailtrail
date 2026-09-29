#include "snailtrail/log/generator.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <sstream>
#include <vector>

#include "snailtrail/log/query_event.hpp"
#include "snailtrail/util/strings.hpp"

namespace snailtrail::log {

double Random::normal() {
    const double u1 = std::max(unit(), 1e-12);
    const double u2 = unit();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * std::numbers::pi * u2);
}

double Random::lognormal(double median, double sigma) {
    return median * std::exp(sigma * normal());
}

namespace {

constexpr std::uint8_t full_scan = static_cast<std::uint8_t>(ExecutionFlag::FullScan);
constexpr std::uint8_t filesort = static_cast<std::uint8_t>(ExecutionFlag::Filesort);
constexpr std::uint8_t tmp_table = static_cast<std::uint8_t>(ExecutionFlag::TmpTable);
constexpr std::uint8_t tmp_disk = static_cast<std::uint8_t>(ExecutionFlag::TmpTableOnDisk);

constexpr const char* statuses[] = {"pending", "paid", "shipped", "delivered", "cancelled"};
constexpr const char* terms[] = {"phone", "case", "usb", "cable", "shoe", "lamp",
                                 "book",  "chair", "mouse", "desk", "bag", "watch"};
constexpr const char* countries[] = {"BR", "US", "PT", "AR", "MX", "DE"};
constexpr const char* first_names[] = {"ana", "bruno", "carla", "diego", "elis", "fabio",
                                       "gabi", "heitor", "iris", "joao"};

template <std::size_t N>
const char* pick(Random& r, const char* const (&items)[N]) {
    return items[r.below(N)];
}

struct Rendered {
    std::string sql;
    double scale = 1.0;
};

struct Scenario {
    double weight;
    double median_ms;
    double sigma;
    std::uint64_t examined;
    std::uint64_t sent;
    std::uint8_t flags;
    Rendered (*render)(Random&);
};

std::string date(Random& r) { return std::format("2024-01-{:02}", r.between(1, 28)); }

std::string datetime(Random& r) {
    return std::format("{} {:02}:{:02}:{:02}", date(r), r.below(24), r.below(60), r.below(60));
}

std::string email(Random& r) {
    return std::format("{}.{}@example.com", pick(r, first_names), r.below(90000));
}

std::string phone(Random& r) { return std::format("55119{:08}", r.below(100000000)); }

const std::vector<Scenario>& scenarios() {
    static const std::vector<Scenario> all = {
        {30, 0.18, 0.4, 1, 1, 0,
         [](Random& r) {
             return r.below(2) == 0
                        ? Rendered{std::format("SELECT * FROM products WHERE id = {}", r.between(1, 5000))}
                        : Rendered{std::format("SELECT * FROM `products` WHERE `id` = {}", r.between(1, 5000))};
         }},
        {20, 0.12, 0.3, 1, 1, 0,
         [](Random& r) {
             std::string token(64, '0');
             for (char& c : token) c = "0123456789abcdef"[r.below(16)];
             return Rendered{std::format("SELECT customer_id FROM sessions WHERE token = '{}'", token)};
         }},
        {12, 42.0, 0.5, 3200, 20, filesort,
         [](Random& r) {
             return Rendered{std::format(
                 "SELECT id, total, created_at FROM orders WHERE customer_id = {} AND status = '{}' "
                 "ORDER BY created_at DESC LIMIT 20",
                 r.between(1, 50000), pick(r, statuses))};
         }},
        {10, 180.0, 0.35, 480000, 3, full_scan,
         [](Random& r) {
             return Rendered{std::format(
                 "SELECT oi.product_id, oi.quantity, p.name\nFROM order_items oi\n"
                 "JOIN products p ON p.id = oi.product_id\nWHERE oi.order_id = {}",
                 r.between(1, 200000))};
         }},
        {8, 0.9, 0.5, 0, 0, 0,
         [](Random& r) {
             return Rendered{std::format(
                 "INSERT INTO orders (customer_id, status, total, created_at, updated_at) "
                 "VALUES ({}, 'pending', {}.{:02}, '{}', '{}')",
                 r.between(1, 50000), r.between(5, 900), r.below(100), datetime(r), datetime(r))};
         }},
        {8, 1.3, 0.5, 0, 0, 0,
         [](Random& r) {
             std::string sql = "INSERT INTO order_items (order_id, product_id, quantity, unit_price) VALUES ";
             const auto rows = r.between(1, 5);
             for (std::uint64_t i = 0; i < rows; ++i) {
                 if (i > 0) sql += ", ";
                 sql += std::format("({}, {}, {}, {}.{:02})", r.between(1, 200000), r.between(1, 5000),
                                    r.between(1, 4), r.between(2, 300), r.below(100));
             }
             return Rendered{sql};
         }},
        {8, 115.0, 0.3, 200000, 1, full_scan,
         [](Random& r) {
             return Rendered{std::format(
                 "SELECT id, full_name, tier FROM customers WHERE LOWER(email) = '{}'", email(r))};
         }},
        {6, 0.7, 0.4, 1, 0, 0,
         [](Random& r) {
             return Rendered{std::format("UPDATE orders SET status = '{}', updated_at = '{}' WHERE id = {}",
                                         pick(r, statuses), datetime(r), r.between(1, 200000))};
         }},
        {6, 88.0, 0.4, 50000, 50, full_scan | filesort,
         [](Random& r) {
             return Rendered{std::format(
                 "SELECT id, name, price FROM products WHERE name LIKE '%{}%' ORDER BY price LIMIT 50",
                 pick(r, terms))};
         }},
        {5, 3.0, 0.4, 40, 20, filesort,
         [](Random& r) {
             const std::uint64_t offset = r.below(10) < 7 ? r.below(5) * 20 : r.between(50, 1000) * 20;
             return Rendered{std::format(
                                 "SELECT * FROM reviews WHERE product_id = {} ORDER BY created_at DESC LIMIT {}, 20",
                                 r.between(1, 5000), offset),
                             1.0 + static_cast<double>(offset) / 400.0};
         }},
        {4, 125.0, 0.3, 200000, 1, full_scan,
         [](Random& r) {
             return Rendered{std::format("SELECT id, full_name FROM customers WHERE phone = {}", phone(r))};
         }},
        {3, 62.0, 0.3, 100000, 8, full_scan | filesort | tmp_table,
         [](Random&) { return Rendered{"SELECT id, name, price FROM products ORDER BY RAND() LIMIT 8"}; }},
        {2, 140.0, 0.3, 200000, 1, full_scan,
         [](Random& r) {
             return Rendered{std::format("SELECT id FROM customers WHERE email = '{}' OR phone = '{}'",
                                         email(r), phone(r))};
         }},
        {1, 28.0, 0.3, 320, 320, 0,
         [](Random& r) {
             std::string sql = "SELECT id, stock FROM products WHERE id IN (";
             const auto count = r.between(250, 400);
             for (std::uint64_t i = 0; i < count; ++i) {
                 if (i > 0) sql += ", ";
                 sql += std::to_string(r.between(1, 5000));
             }
             return Rendered{sql + ")"};
         }},
        {1, 870.0, 0.25, 2000000, 30, full_scan | tmp_table,
         [](Random& r) {
             return Rendered{std::format(
                 "SELECT DATE(created_at) AS day, SUM(total) AS revenue FROM orders "
                 "WHERE DATE(created_at) >= '{}' GROUP BY DATE(created_at)",
                 date(r))};
         }},
        {0.5, 74.0, 0.2, 5000, 11, tmp_table,
         [](Random&) {
             return Rendered{"SELECT category, COUNT(*) FROM products GROUP BY category HAVING category <> 'books'"};
         }},
        {0.5, 790.0, 0.3, 300000, 0, full_scan,
         [](Random& r) {
             return Rendered{std::format("DELETE FROM sessions WHERE expires_at < '{}'", datetime(r))};
         }},
        {0.3, 2400.0, 0.2, 5000000, 1200, full_scan | tmp_table | tmp_disk,
         [](Random&) {
             return Rendered{"SELECT id, email FROM customers WHERE id NOT IN (SELECT customer_id FROM orders)"};
         }},
        {0.1, 7900.0, 0.15, 50000000, 1, full_scan,
         [](Random& r) {
             return Rendered{std::format(
                 "SELECT COUNT(*) FROM customers c, orders o WHERE c.country = '{}' AND o.status = 'paid'",
                 pick(r, countries))};
         }},
        {0.05, 3100.0, 0.1, 200000, 0, full_scan,
         [](Random&) { return Rendered{"UPDATE customers SET tier = 1"}; }},
    };
    return all;
}

std::string iso_time(std::uint64_t micros) {
    std::string t = util::format_timestamp(static_cast<std::int64_t>(micros / 1'000'000));
    t[10] = 'T';
    return std::format("{}.{:06}Z", t, micros % 1'000'000);
}

std::string seconds(std::uint64_t micros) {
    return std::format("{}.{:06}", micros / 1'000'000, micros % 1'000'000);
}

}

SlowLogGenerator::SlowLogGenerator(GeneratorOptions options) : options_(options) {}

void SlowLogGenerator::write(std::ostream& out) {
    const auto& all = scenarios();
    std::vector<double> cumulative;
    double total_weight = 0;
    for (const auto& s : all) {
        total_weight += s.weight;
        cumulative.push_back(total_weight);
    }

    Random rng(options_.seed);
    std::uint64_t now = static_cast<std::uint64_t>(options_.start_time) * 1'000'000;
    std::string buffer;
    buffer.reserve(1 << 20);
    buffer += "/usr/sbin/mysqld, Version: 8.4.2 (MySQL Community Server - GPL). started with:\n"
              "Tcp port: 3306  Unix socket: /var/run/mysqld/mysqld.sock\n"
              "Time                 Id Command    Argument\n";

    for (std::uint64_t i = 0; i < options_.events; ++i) {
        now += 1 + static_cast<std::uint64_t>(rng.unit() * 40'000.0);
        const double pick_at = rng.unit() * total_weight;
        const auto index = static_cast<std::size_t>(
            std::upper_bound(cumulative.begin(), cumulative.end(), pick_at) - cumulative.begin());
        const Scenario& s = all[std::min(index, all.size() - 1)];
        const Rendered rendered = s.render(rng);

        const double ms = rng.lognormal(s.median_ms * rendered.scale, s.sigma);
        const auto query_us = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(ms * 1000.0));
        const std::uint64_t lock_us = rng.between(1, 12);
        const auto jitter = 0.8 + 0.4 * rng.unit();
        const auto examined =
            static_cast<std::uint64_t>(static_cast<double>(s.examined) * rendered.scale * jitter);
        const std::uint64_t sent = s.sent;
        const std::uint64_t thread = 8 + rng.below(24);
        const std::uint64_t start = now;
        const std::uint64_t end = start + query_us;
        const bool quit = rng.below(250) == 0;

        buffer += std::format("# Time: {}\n# User@Host: app[app] @ app-{} [10.0.0.{}]  Id: {:5}\n",
                              iso_time(end), 1 + thread % 3, 11 + thread % 3, thread);
        buffer += std::format("# Query_time: {}  Lock_time: {} Rows_sent: {}  Rows_examined: {}",
                              seconds(quit ? 3 : query_us), seconds(quit ? 0 : lock_us),
                              quit ? 0 : sent, quit ? 0 : examined);
        if (options_.extra_fields) {
            const std::uint8_t flags = quit ? 0 : s.flags;
            buffer += std::format(
                " Thread_id: {} Errno: 0 Killed: 0 Bytes_received: {} Bytes_sent: {} Read_first: 0 "
                "Read_last: 0 Read_key: {} Read_next: 0 Read_prev: 0 Read_rnd: 0 Read_rnd_next: {} "
                "Sort_merge_passes: 0 Sort_range_count: 0 Sort_rows: {} Sort_scan_count: {} "
                "Created_tmp_disk_tables: {} Created_tmp_tables: {} Count_hit_tmp_table_size: 0 "
                "Start: {} End: {}",
                thread, rendered.sql.size(), 60 + sent * 48, (flags & full_scan) != 0 ? 0 : 1,
                (flags & full_scan) != 0 ? examined + 1 : 0, (flags & filesort) != 0 ? sent : 0,
                (flags & filesort) != 0 ? 1 : 0, (flags & tmp_disk) != 0 ? 1 : 0,
                (flags & tmp_table) != 0 ? 1 : 0, iso_time(start), iso_time(end));
        }
        buffer += '\n';
        if (i == 0) buffer += "use shop;\n";
        buffer += std::format("SET timestamp={};\n", start / 1'000'000);
        if (quit) {
            buffer += "# administrator command: Quit;\n";
        } else {
            buffer += rendered.sql;
            buffer += ";\n";
        }
        if (buffer.size() > (1U << 20)) {
            out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            buffer.clear();
        }
    }
    out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
}

std::string SlowLogGenerator::generate() {
    std::ostringstream out;
    write(out);
    return std::move(out).str();
}

}
