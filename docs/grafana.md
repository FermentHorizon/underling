# Grafana — Underling Dashboard

## Data Source

- **Plugin**: [Altinity ClickHouse](https://grafana.com/grafana/plugins/vertamedia-clickhouse-datasource/) or official [ClickHouse plugin](https://grafana.com/grafana/plugins/grafana-clickhouse-datasource/)
- **Host**: `10.1.1.2:8123`
- **User**: `admin`
- **Password**: `ClickHouse2026!`
- **Database**: `sensor_data`

## Panel Queries

### Live Weight (time series)

```sql
SELECT
  toTimezone(timestamp, 'Australia/Perth') AS time,
  weight_g
FROM sensor_data.weight_readings
WHERE timestamp >= now() - INTERVAL $__interval_s SECOND
ORDER BY timestamp
```

### Temperature (time series)

```sql
SELECT
  toTimezone(timestamp, 'Australia/Perth') AS time,
  temperature_c
FROM sensor_data.weight_readings
WHERE timestamp >= now() - INTERVAL $__interval_s SECOND
ORDER BY timestamp
```

### Current Weight (stat panel)

```sql
SELECT weight_g
FROM sensor_data.weight_readings
ORDER BY timestamp DESC
LIMIT 1
```

### Transpiration Rate — g/hr (time series)

```sql
SELECT
  toTimezone(toStartOfHour(timestamp), 'Australia/Perth') AS time,
  -1 * (max(weight_g) - min(weight_g)) AS loss_g_per_hr
FROM sensor_data.weight_readings
WHERE timestamp >= now() - INTERVAL 24 HOUR
GROUP BY time
ORDER BY time
```

### Hourly Averages (from materialized view)

```sql
SELECT
  toTimezone(hour, 'Australia/Perth') AS time,
  avgMerge(avg_weight_g)   AS avg_weight,
  minMerge(min_weight_g)   AS min_weight,
  maxMerge(max_weight_g)   AS max_weight,
  avgMerge(avg_temperature_c) AS avg_temp
FROM sensor_data.weight_readings_hourly
WHERE hour >= now() - INTERVAL 7 DAY
GROUP BY time
ORDER BY time
```

### Daily Weight Loss (bar chart)

```sql
SELECT
  toTimezone(toStartOfDay(timestamp), 'Australia/Perth') AS time,
  max(weight_g) - min(weight_g) AS daily_loss_g
FROM sensor_data.weight_readings
WHERE timestamp >= now() - INTERVAL 30 DAY
GROUP BY time
ORDER BY time
```

## Notes

- All timestamps are stored as **UTC** in ClickHouse — use `toTimezone(ts, 'Australia/Perth')` for AWST display.
- Readings arrive every **30 seconds**.
- The `weight_readings_hourly` materialized view pre-aggregates using `AggregatingMergeTree` — use `avgMerge()`, `minMerge()`, `maxMerge()` to read it.
- Device identifier: `device_id = 'underling'`.
