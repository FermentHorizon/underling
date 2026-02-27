#!/usr/bin/env python3
"""
ClickHouse table initialization for Underling weight sensor.

Creates the weight_readings table in the sensor_data database.
Follows the same pattern as the Budde growbot sensor tables.

Usage:
    python3 init_clickhouse.py          # Create table
    python3 init_clickhouse.py --drop   # Drop and recreate
"""

import sys
import argparse

try:
    import clickhouse_connect
except ImportError:
    print("Install clickhouse-connect: pip3 install clickhouse-connect")
    sys.exit(1)

# ClickHouse connection — same as Budde config/sensors.yaml
CH_HOST     = "10.1.1.2"
CH_PORT     = 8123
CH_USER     = "admin"
CH_PASS     = "ClickHouse2026!"
CH_DATABASE = "sensor_data"
CH_TABLE    = "weight_readings"


def create_table(client, database: str, table: str, drop: bool = False):
    """Create weight_readings table for Underling transpiration data."""

    if drop:
        print(f"Dropping existing table: {table}")
        client.command(f"DROP TABLE IF EXISTS {database}.{table}")

    print(f"Creating table: {table}")

    create_sql = f"""
    CREATE TABLE IF NOT EXISTS {database}.{table} (
        -- Timestamps
        timestamp DateTime64(3, 'UTC'),
        inserted_at DateTime64(3, 'UTC') DEFAULT now64(3, 'UTC'),

        -- Weight data
        weight_g Float32,

        -- Temperature (nullable — sensor may not be connected)
        temperature_c Nullable(Float32),

        -- Device metadata
        uptime_s UInt32,
        device_id String DEFAULT 'underling'
    )
    ENGINE = MergeTree()
    PARTITION BY toYYYYMM(timestamp)
    ORDER BY (device_id, timestamp, inserted_at)
    TTL timestamp + INTERVAL 1 YEAR
    SETTINGS index_granularity = 8192
    """

    client.command(create_sql)
    print(f"  Table '{table}' created")

    # Hourly aggregate materialized view
    mv_name = f"{table}_hourly"
    print(f"Creating materialized view: {mv_name}")

    mv_sql = f"""
    CREATE MATERIALIZED VIEW IF NOT EXISTS {database}.{mv_name}
    ENGINE = AggregatingMergeTree()
    PARTITION BY toYYYYMM(hour)
    ORDER BY (device_id, hour)
    AS SELECT
        toStartOfHour(timestamp) AS hour,
        device_id,
        avg(weight_g) AS avg_weight_g,
        min(weight_g) AS min_weight_g,
        max(weight_g) AS max_weight_g,
        avg(temperature_c) AS avg_temperature_c,
        count() AS reading_count
    FROM {database}.{table}
    GROUP BY device_id, hour
    """

    client.command(mv_sql)
    print(f"  Materialized view '{mv_name}' created")


def print_sample_queries(database: str, table: str):
    """Print useful queries."""
    print("\n" + "=" * 60)
    print("SAMPLE QUERIES")
    print("=" * 60)

    queries = [
        ("Latest weight readings (last 10)", f"""
SELECT timestamp, weight_g, temperature_c, uptime_s
FROM {database}.{table}
ORDER BY timestamp DESC
LIMIT 10"""),

        ("Hourly averages (last 24 hours)", f"""
SELECT hour, avg_weight_g, min_weight_g, max_weight_g,
       avg_temperature_c, reading_count
FROM {database}.{table}_hourly
WHERE hour >= now() - INTERVAL 24 HOUR
ORDER BY hour DESC"""),

        ("Weight change over last hour", f"""
SELECT
    max(weight_g) - min(weight_g) AS range_g,
    count() AS readings
FROM {database}.{table}
WHERE timestamp >= now() - INTERVAL 1 HOUR"""),

        ("Transpiration rate (g/hr, last 6 hours)", f"""
SELECT
    toStartOfHour(timestamp) AS hour,
    -1 * (max(weight_g) - min(weight_g)) AS loss_g,
    count() AS n
FROM {database}.{table}
WHERE timestamp >= now() - INTERVAL 6 HOUR
GROUP BY hour
ORDER BY hour"""),
    ]

    for title, query in queries:
        print(f"\n-- {title}")
        print(query.strip())
    print()


def main():
    parser = argparse.ArgumentParser(
        description="Initialize ClickHouse table for Underling weight sensor"
    )
    parser.add_argument(
        "--drop", action="store_true",
        help="Drop existing table before creating (WARNING: data loss!)"
    )
    args = parser.parse_args()

    print(f"Connecting to ClickHouse at {CH_HOST}:{CH_PORT}")

    try:
        client = clickhouse_connect.get_client(
            host=CH_HOST, port=CH_PORT,
            username=CH_USER, password=CH_PASS
        )

        version = client.command("SELECT version()")
        print(f"  Connected (version {version})")

        # Ensure database exists
        client.command(f"CREATE DATABASE IF NOT EXISTS {CH_DATABASE}")
        print(f"  Database '{CH_DATABASE}' ready")
        print()

        create_table(client, CH_DATABASE, CH_TABLE, drop=args.drop)

        print_sample_queries(CH_DATABASE, CH_TABLE)

        print("=" * 60)
        print("DONE")
        print("=" * 60)
        print(f"\nWeb UI: http://{CH_HOST}:{CH_PORT}/play")
        print(f"Table:  {CH_DATABASE}.{CH_TABLE}")

        client.close()
        return 0

    except Exception as e:
        print(f"\nERROR: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
