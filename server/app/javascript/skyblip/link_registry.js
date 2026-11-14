export const LINK_REGISTRY = Object.freeze({
  "title": "skyBlip companion link registry (v1)",
  "frames": [
    {
      "command": "acknowledgement",
      "code": 0,
      "endpoint": "config",
      "sender": "unit",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "acknowledge",
          "number": 1,
          "type": "boolean",
          "required": true
        },
        {
          "name": "pending",
          "number": 2,
          "type": "boolean"
        },
        {
          "name": "reason",
          "number": 3,
          "type": "enum",
          "enum": "reason"
        },
        {
          "name": "by",
          "number": 4,
          "type": "unsigned"
        }
      ]
    },
    {
      "command": "get",
      "code": 1,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        }
      ]
    },
    {
      "command": "config",
      "code": 2,
      "endpoint": "config",
      "sender": "unit",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "version",
          "number": 1,
          "type": "unsigned"
        },
        {
          "name": "address",
          "number": 2,
          "type": "unsigned"
        },
        {
          "name": "address_mapping_table",
          "number": 3,
          "type": "unsigned"
        },
        {
          "name": "aircraft_type",
          "number": 4,
          "type": "unsigned"
        },
        {
          "name": "alarm_enabled",
          "number": 5,
          "type": "boolean"
        },
        {
          "name": "alarm_volume",
          "number": 6,
          "type": "enum",
          "enum": "alarm_volume"
        },
        {
          "name": "units",
          "number": 7,
          "type": "enum",
          "enum": "units"
        },
        {
          "name": "callsign",
          "number": 8,
          "type": "text"
        },
        {
          "name": "battery_offset_mv",
          "number": 9,
          "type": "integer"
        },
        {
          "name": "frequency_trim_e1_ppm",
          "number": 10,
          "type": "integer"
        },
        {
          "name": "own_icao",
          "number": 11,
          "type": "unsigned"
        }
      ]
    },
    {
      "command": "defaults",
      "code": 3,
      "endpoint": "config",
      "sender": "both",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "version",
          "number": 1,
          "type": "unsigned"
        },
        {
          "name": "address",
          "number": 2,
          "type": "unsigned"
        },
        {
          "name": "address_mapping_table",
          "number": 3,
          "type": "unsigned"
        },
        {
          "name": "aircraft_type",
          "number": 4,
          "type": "unsigned"
        },
        {
          "name": "alarm_enabled",
          "number": 5,
          "type": "boolean"
        },
        {
          "name": "alarm_volume",
          "number": 6,
          "type": "enum",
          "enum": "alarm_volume"
        },
        {
          "name": "units",
          "number": 7,
          "type": "enum",
          "enum": "units"
        },
        {
          "name": "callsign",
          "number": 8,
          "type": "text"
        },
        {
          "name": "battery_offset_mv",
          "number": 9,
          "type": "integer"
        },
        {
          "name": "frequency_trim_e1_ppm",
          "number": 10,
          "type": "integer"
        },
        {
          "name": "own_icao",
          "number": 11,
          "type": "unsigned"
        }
      ],
      "keys_of": "config"
    },
    {
      "command": "set",
      "code": 4,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "version",
          "number": 1,
          "type": "unsigned"
        },
        {
          "name": "address",
          "number": 2,
          "type": "unsigned"
        },
        {
          "name": "address_mapping_table",
          "number": 3,
          "type": "unsigned"
        },
        {
          "name": "aircraft_type",
          "number": 4,
          "type": "unsigned"
        },
        {
          "name": "alarm_enabled",
          "number": 5,
          "type": "boolean"
        },
        {
          "name": "alarm_volume",
          "number": 6,
          "type": "enum",
          "enum": "alarm_volume"
        },
        {
          "name": "units",
          "number": 7,
          "type": "enum",
          "enum": "units"
        },
        {
          "name": "callsign",
          "number": 8,
          "type": "text"
        },
        {
          "name": "battery_offset_mv",
          "number": 9,
          "type": "integer"
        },
        {
          "name": "frequency_trim_e1_ppm",
          "number": 10,
          "type": "integer"
        },
        {
          "name": "own_icao",
          "number": 11,
          "type": "unsigned"
        }
      ],
      "keys_of": "config"
    },
    {
      "command": "status",
      "code": 5,
      "endpoint": "config",
      "sender": "both",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "flight",
          "number": 1,
          "type": "enum",
          "enum": "flight",
          "required": true
        },
        {
          "name": "upload",
          "number": 2,
          "type": "boolean",
          "required": true
        },
        {
          "name": "battery_percent",
          "number": 3,
          "type": "unsigned"
        },
        {
          "name": "charging",
          "number": 4,
          "type": "boolean",
          "required": true
        },
        {
          "name": "power_level",
          "number": 5,
          "type": "enum",
          "enum": "power_level",
          "required": true
        },
        {
          "name": "went_dark_flat",
          "number": 6,
          "type": "boolean",
          "required": true
        },
        {
          "name": "die_temperature_c",
          "number": 7,
          "type": "integer"
        },
        {
          "name": "bravo",
          "number": 8,
          "type": "enum",
          "enum": "bravo"
        }
      ]
    },
    {
      "command": "timing",
      "code": 6,
      "endpoint": "config",
      "sender": "both",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "part",
          "number": 1,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "more",
          "number": 2,
          "type": "boolean",
          "required": true
        },
        {
          "name": "pps_us",
          "number": 3,
          "type": "unsigned_array"
        },
        {
          "name": "pps_worst_us",
          "number": 4,
          "type": "integer"
        },
        {
          "name": "pps_samples",
          "number": 5,
          "type": "unsigned"
        },
        {
          "name": "dwell_us",
          "number": 6,
          "type": "unsigned_array"
        },
        {
          "name": "dwell_worst_us",
          "number": 7,
          "type": "integer"
        },
        {
          "name": "dwell_samples",
          "number": 8,
          "type": "unsigned"
        },
        {
          "name": "holdover_events",
          "number": 9,
          "type": "unsigned"
        },
        {
          "name": "missed",
          "number": 10,
          "type": "unsigned"
        },
        {
          "name": "refused",
          "number": 11,
          "type": "unsigned"
        },
        {
          "name": "switch_hop_us",
          "number": 12,
          "type": "unsigned"
        },
        {
          "name": "switch_to_o_band_us",
          "number": 13,
          "type": "unsigned"
        },
        {
          "name": "switch_to_m_band_us",
          "number": 14,
          "type": "unsigned"
        },
        {
          "name": "late_dwells",
          "number": 15,
          "type": "unsigned"
        }
      ]
    },
    {
      "command": "flash",
      "code": 7,
      "endpoint": "config",
      "sender": "both",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "writable",
          "number": 1,
          "type": "boolean",
          "required": true
        },
        {
          "name": "requests",
          "number": 2,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "writes",
          "number": 3,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "forced",
          "number": 4,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "worst_wait_ms",
          "number": 5,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "pending",
          "number": 6,
          "type": "boolean",
          "required": true
        },
        {
          "name": "budget_ms",
          "number": 7,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "bound_ms",
          "number": 8,
          "type": "unsigned",
          "required": true
        }
      ]
    },
    {
      "command": "radio",
      "code": 8,
      "endpoint": "config",
      "sender": "both",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "group",
          "number": 1,
          "type": "enum",
          "enum": "group",
          "required": true
        },
        {
          "name": "part",
          "number": 2,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "more",
          "number": 3,
          "type": "boolean",
          "required": true
        },
        {
          "name": "uptime_s",
          "number": 4,
          "type": "unsigned"
        },
        {
          "name": "reset",
          "number": 5,
          "type": "enum",
          "enum": "reset"
        },
        {
          "name": "link_drops",
          "number": 6,
          "type": "unsigned"
        },
        {
          "name": "noise_dbm",
          "number": 7,
          "type": "integer"
        },
        {
          "name": "duty_permille",
          "number": 8,
          "type": "unsigned"
        },
        {
          "name": "rx_ok",
          "number": 9,
          "type": "unsigned"
        },
        {
          "name": "rx_bad",
          "number": 10,
          "type": "unsigned"
        },
        {
          "name": "rx_wait",
          "number": 11,
          "type": "unsigned"
        },
        {
          "name": "rx_type",
          "number": 12,
          "type": "unsigned"
        },
        {
          "name": "rx_unframed",
          "number": 13,
          "type": "unsigned"
        },
        {
          "name": "rx_miskeyed",
          "number": 14,
          "type": "unsigned"
        },
        {
          "name": "rx_noise",
          "number": 15,
          "type": "unsigned"
        },
        {
          "name": "rx_named",
          "number": 16,
          "type": "unsigned"
        },
        {
          "name": "tx_ok",
          "number": 17,
          "type": "unsigned"
        },
        {
          "name": "tx_lost",
          "number": 18,
          "type": "unsigned"
        },
        {
          "name": "tx_named",
          "number": 19,
          "type": "unsigned"
        },
        {
          "name": "tx_keyed_us",
          "number": 20,
          "type": "unsigned"
        },
        {
          "name": "tx_span_us",
          "number": 21,
          "type": "unsigned"
        },
        {
          "name": "range_refused",
          "number": 22,
          "type": "unsigned"
        },
        {
          "name": "dio1_armed",
          "number": 23,
          "type": "boolean"
        },
        {
          "name": "dio1_edges",
          "number": 24,
          "type": "unsigned"
        },
        {
          "name": "dio1_missed",
          "number": 25,
          "type": "unsigned"
        },
        {
          "name": "tracked",
          "number": 26,
          "type": "unsigned"
        },
        {
          "name": "alarm",
          "number": 27,
          "type": "unsigned"
        },
        {
          "name": "fixes",
          "number": 28,
          "type": "unsigned"
        },
        {
          "name": "valid",
          "number": 29,
          "type": "boolean"
        },
        {
          "name": "baud",
          "number": 30,
          "type": "unsigned"
        },
        {
          "name": "config",
          "number": 31,
          "type": "enum",
          "enum": "gnss_config"
        },
        {
          "name": "raises",
          "number": 32,
          "type": "unsigned"
        },
        {
          "name": "raise_fallbacks",
          "number": 33,
          "type": "unsigned"
        },
        {
          "name": "solution_phase_ms",
          "number": 34,
          "type": "unsigned"
        },
        {
          "name": "overruns",
          "number": 35,
          "type": "unsigned"
        },
        {
          "name": "identified",
          "number": 36,
          "type": "boolean"
        },
        {
          "name": "firmware",
          "number": 37,
          "type": "text"
        },
        {
          "name": "reject",
          "number": 38,
          "type": "enum",
          "enum": "reject"
        },
        {
          "name": "rejected",
          "number": 39,
          "type": "unsigned"
        },
        {
          "name": "residual_m",
          "number": 40,
          "type": "unsigned"
        },
        {
          "name": "cell_mv",
          "number": 41,
          "type": "unsigned"
        },
        {
          "name": "percent",
          "number": 42,
          "type": "unsigned"
        },
        {
          "name": "battery_valid",
          "number": 43,
          "type": "boolean"
        },
        {
          "name": "charging",
          "number": 44,
          "type": "boolean"
        },
        {
          "name": "power_level",
          "number": 45,
          "type": "enum",
          "enum": "power_level"
        },
        {
          "name": "supply_warnings",
          "number": 46,
          "type": "unsigned"
        },
        {
          "name": "implausible",
          "number": 47,
          "type": "unsigned"
        },
        {
          "name": "die_temperature_c",
          "number": 48,
          "type": "integer"
        },
        {
          "name": "charge",
          "number": 49,
          "type": "enum",
          "enum": "charge"
        },
        {
          "name": "charge_warnings",
          "number": 50,
          "type": "unsigned"
        },
        {
          "name": "frames",
          "number": 51,
          "type": "unsigned"
        },
        {
          "name": "frame_rate_hz",
          "number": 52,
          "type": "unsigned"
        },
        {
          "name": "dropped",
          "number": 54,
          "type": "unsigned"
        },
        {
          "name": "rearms",
          "number": 55,
          "type": "unsigned"
        },
        {
          "name": "resets",
          "number": 56,
          "type": "unsigned"
        },
        {
          "name": "command_errors",
          "number": 57,
          "type": "unsigned"
        },
        {
          "name": "errors",
          "number": 58,
          "type": "unsigned"
        },
        {
          "name": "rssi_dbm",
          "number": 59,
          "type": "unsigned_array"
        },
        {
          "name": "crc",
          "number": 60,
          "type": "unsigned"
        },
        {
          "name": "unsupported",
          "number": 61,
          "type": "unsigned"
        },
        {
          "name": "folded",
          "number": 62,
          "type": "unsigned"
        },
        {
          "name": "aircraft",
          "number": 63,
          "type": "unsigned"
        },
        {
          "name": "tx_held_for_b5",
          "number": 64,
          "type": "unsigned"
        },
        {
          "name": "rx_status",
          "number": 65,
          "type": "unsigned"
        },
        {
          "name": "uplink_stale",
          "number": 66,
          "type": "unsigned"
        },
        {
          "name": "tx_given_up",
          "number": 67,
          "type": "unsigned"
        },
        {
          "name": "tx_deferred",
          "number": 68,
          "type": "unsigned"
        },
        {
          "name": "tx_forced",
          "number": 69,
          "type": "unsigned"
        },
        {
          "name": "tx_busy_missed",
          "number": 70,
          "type": "unsigned"
        }
      ],
      "keys_of": "diagnostics"
    },
    {
      "command": "diagnostics",
      "code": 9,
      "endpoint": "config",
      "sender": "both",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "group",
          "number": 1,
          "type": "enum",
          "enum": "group",
          "required": true
        },
        {
          "name": "part",
          "number": 2,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "more",
          "number": 3,
          "type": "boolean",
          "required": true
        },
        {
          "name": "uptime_s",
          "number": 4,
          "type": "unsigned"
        },
        {
          "name": "reset",
          "number": 5,
          "type": "enum",
          "enum": "reset"
        },
        {
          "name": "link_drops",
          "number": 6,
          "type": "unsigned"
        },
        {
          "name": "noise_dbm",
          "number": 7,
          "type": "integer"
        },
        {
          "name": "duty_permille",
          "number": 8,
          "type": "unsigned"
        },
        {
          "name": "rx_ok",
          "number": 9,
          "type": "unsigned"
        },
        {
          "name": "rx_bad",
          "number": 10,
          "type": "unsigned"
        },
        {
          "name": "rx_wait",
          "number": 11,
          "type": "unsigned"
        },
        {
          "name": "rx_type",
          "number": 12,
          "type": "unsigned"
        },
        {
          "name": "rx_unframed",
          "number": 13,
          "type": "unsigned"
        },
        {
          "name": "rx_miskeyed",
          "number": 14,
          "type": "unsigned"
        },
        {
          "name": "rx_noise",
          "number": 15,
          "type": "unsigned"
        },
        {
          "name": "rx_named",
          "number": 16,
          "type": "unsigned"
        },
        {
          "name": "tx_ok",
          "number": 17,
          "type": "unsigned"
        },
        {
          "name": "tx_lost",
          "number": 18,
          "type": "unsigned"
        },
        {
          "name": "tx_named",
          "number": 19,
          "type": "unsigned"
        },
        {
          "name": "tx_keyed_us",
          "number": 20,
          "type": "unsigned"
        },
        {
          "name": "tx_span_us",
          "number": 21,
          "type": "unsigned"
        },
        {
          "name": "range_refused",
          "number": 22,
          "type": "unsigned"
        },
        {
          "name": "dio1_armed",
          "number": 23,
          "type": "boolean"
        },
        {
          "name": "dio1_edges",
          "number": 24,
          "type": "unsigned"
        },
        {
          "name": "dio1_missed",
          "number": 25,
          "type": "unsigned"
        },
        {
          "name": "tracked",
          "number": 26,
          "type": "unsigned"
        },
        {
          "name": "alarm",
          "number": 27,
          "type": "unsigned"
        },
        {
          "name": "fixes",
          "number": 28,
          "type": "unsigned"
        },
        {
          "name": "valid",
          "number": 29,
          "type": "boolean"
        },
        {
          "name": "baud",
          "number": 30,
          "type": "unsigned"
        },
        {
          "name": "config",
          "number": 31,
          "type": "enum",
          "enum": "gnss_config"
        },
        {
          "name": "raises",
          "number": 32,
          "type": "unsigned"
        },
        {
          "name": "raise_fallbacks",
          "number": 33,
          "type": "unsigned"
        },
        {
          "name": "solution_phase_ms",
          "number": 34,
          "type": "unsigned"
        },
        {
          "name": "overruns",
          "number": 35,
          "type": "unsigned"
        },
        {
          "name": "identified",
          "number": 36,
          "type": "boolean"
        },
        {
          "name": "firmware",
          "number": 37,
          "type": "text"
        },
        {
          "name": "reject",
          "number": 38,
          "type": "enum",
          "enum": "reject"
        },
        {
          "name": "rejected",
          "number": 39,
          "type": "unsigned"
        },
        {
          "name": "residual_m",
          "number": 40,
          "type": "unsigned"
        },
        {
          "name": "cell_mv",
          "number": 41,
          "type": "unsigned"
        },
        {
          "name": "percent",
          "number": 42,
          "type": "unsigned"
        },
        {
          "name": "battery_valid",
          "number": 43,
          "type": "boolean"
        },
        {
          "name": "charging",
          "number": 44,
          "type": "boolean"
        },
        {
          "name": "power_level",
          "number": 45,
          "type": "enum",
          "enum": "power_level"
        },
        {
          "name": "supply_warnings",
          "number": 46,
          "type": "unsigned"
        },
        {
          "name": "implausible",
          "number": 47,
          "type": "unsigned"
        },
        {
          "name": "die_temperature_c",
          "number": 48,
          "type": "integer"
        },
        {
          "name": "charge",
          "number": 49,
          "type": "enum",
          "enum": "charge"
        },
        {
          "name": "charge_warnings",
          "number": 50,
          "type": "unsigned"
        },
        {
          "name": "frames",
          "number": 51,
          "type": "unsigned"
        },
        {
          "name": "frame_rate_hz",
          "number": 52,
          "type": "unsigned"
        },
        {
          "name": "dropped",
          "number": 54,
          "type": "unsigned"
        },
        {
          "name": "rearms",
          "number": 55,
          "type": "unsigned"
        },
        {
          "name": "resets",
          "number": 56,
          "type": "unsigned"
        },
        {
          "name": "command_errors",
          "number": 57,
          "type": "unsigned"
        },
        {
          "name": "errors",
          "number": 58,
          "type": "unsigned"
        },
        {
          "name": "rssi_dbm",
          "number": 59,
          "type": "unsigned_array"
        },
        {
          "name": "crc",
          "number": 60,
          "type": "unsigned"
        },
        {
          "name": "unsupported",
          "number": 61,
          "type": "unsigned"
        },
        {
          "name": "folded",
          "number": 62,
          "type": "unsigned"
        },
        {
          "name": "aircraft",
          "number": 63,
          "type": "unsigned"
        },
        {
          "name": "tx_held_for_b5",
          "number": 64,
          "type": "unsigned"
        },
        {
          "name": "rx_status",
          "number": 65,
          "type": "unsigned"
        },
        {
          "name": "uplink_stale",
          "number": 66,
          "type": "unsigned"
        },
        {
          "name": "tx_given_up",
          "number": 67,
          "type": "unsigned"
        },
        {
          "name": "tx_deferred",
          "number": 68,
          "type": "unsigned"
        },
        {
          "name": "tx_forced",
          "number": 69,
          "type": "unsigned"
        },
        {
          "name": "tx_busy_missed",
          "number": 70,
          "type": "unsigned"
        }
      ]
    },
    {
      "command": "update",
      "code": 10,
      "endpoint": "config",
      "sender": "both",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "image",
          "number": 1,
          "type": "enum",
          "enum": "image",
          "required": true
        },
        {
          "name": "from",
          "number": 2,
          "type": "text"
        },
        {
          "name": "to",
          "number": 3,
          "type": "text"
        },
        {
          "name": "settings",
          "number": 4,
          "type": "enum",
          "enum": "settings_fallback"
        },
        {
          "name": "swap_powered",
          "number": 5,
          "type": "boolean",
          "required": true
        },
        {
          "name": "imu",
          "number": 6,
          "type": "enum",
          "enum": "hub_image",
          "required": true
        },
        {
          "name": "imu_digest",
          "number": 7,
          "type": "bytes",
          "bytes": 8
        },
        {
          "name": "downgrade",
          "number": 8,
          "type": "boolean"
        },
        {
          "name": "key",
          "number": 9,
          "type": "bytes",
          "bytes": 4
        },
        {
          "name": "product",
          "number": 10,
          "type": "enum",
          "enum": "product"
        }
      ]
    },
    {
      "command": "dfu",
      "code": 11,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "version",
          "number": 1,
          "type": "text",
          "required": true
        }
      ]
    },
    {
      "command": "recovery",
      "code": 12,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        }
      ]
    },
    {
      "command": "power_off",
      "code": 13,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        }
      ]
    },
    {
      "command": "challenge",
      "code": 14,
      "endpoint": "config",
      "sender": "both",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "nonce",
          "number": 1,
          "type": "bytes",
          "bytes": 16,
          "required": true
        },
        {
          "name": "server_key_id",
          "number": 2,
          "type": "bytes",
          "bytes": 4,
          "required": true
        }
      ]
    },
    {
      "command": "register",
      "code": 15,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "server_nonce",
          "number": 1,
          "type": "bytes",
          "bytes": 16,
          "required": true
        },
        {
          "name": "signature",
          "number": 2,
          "type": "bytes",
          "bytes": 64,
          "required": true
        }
      ]
    },
    {
      "command": "registration",
      "code": 16,
      "endpoint": "config",
      "sender": "unit",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "device_id",
          "number": 1,
          "type": "bytes",
          "bytes": 8,
          "required": true
        },
        {
          "name": "public_key",
          "number": 2,
          "type": "bytes",
          "bytes": 32,
          "required": true
        },
        {
          "name": "version",
          "number": 3,
          "type": "text",
          "required": true
        },
        {
          "name": "server_key_id",
          "number": 4,
          "type": "bytes",
          "bytes": 4,
          "required": true
        },
        {
          "name": "release",
          "number": 5,
          "type": "boolean",
          "required": true
        },
        {
          "name": "signature",
          "number": 6,
          "type": "bytes",
          "bytes": 64,
          "required": true
        }
      ]
    },
    {
      "command": "unlock",
      "code": 17,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "serial",
          "number": 1,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "signature",
          "number": 2,
          "type": "bytes",
          "bytes": 64,
          "required": true
        }
      ]
    },
    {
      "command": "list",
      "code": 18,
      "endpoint": "log",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "log",
          "number": 1,
          "type": "enum",
          "enum": "log"
        },
        {
          "name": "index",
          "number": 2,
          "type": "unsigned"
        }
      ]
    },
    {
      "command": "read",
      "code": 19,
      "endpoint": "log",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "log",
          "number": 1,
          "type": "enum",
          "enum": "log"
        },
        {
          "name": "session",
          "number": 2,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "from",
          "number": 3,
          "type": "unsigned"
        },
        {
          "name": "count",
          "number": 4,
          "type": "unsigned"
        }
      ]
    },
    {
      "command": "erase",
      "code": 20,
      "endpoint": "log",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "log",
          "number": 1,
          "type": "enum",
          "enum": "log"
        }
      ]
    },
    {
      "command": "log",
      "code": 21,
      "endpoint": "log",
      "sender": "unit",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "log",
          "number": 1,
          "type": "enum",
          "enum": "log"
        },
        {
          "name": "acknowledge",
          "number": 2,
          "type": "boolean",
          "required": true
        },
        {
          "name": "reason",
          "number": 3,
          "type": "enum",
          "enum": "reason"
        },
        {
          "name": "sessions",
          "number": 4,
          "type": "unsigned"
        },
        {
          "name": "truncated",
          "number": 5,
          "type": "boolean"
        }
      ]
    },
    {
      "command": "session",
      "code": 22,
      "endpoint": "log",
      "sender": "unit",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "log",
          "number": 1,
          "type": "enum",
          "enum": "log"
        },
        {
          "name": "index",
          "number": 2,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "of",
          "number": 3,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "session",
          "number": 4,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "records",
          "number": 5,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "closed",
          "number": 6,
          "type": "boolean",
          "required": true
        },
        {
          "name": "truncated",
          "number": 7,
          "type": "boolean",
          "required": true
        }
      ]
    },
    {
      "command": "chunk",
      "code": 23,
      "endpoint": "log",
      "sender": "unit",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "log",
          "number": 1,
          "type": "enum",
          "enum": "log"
        },
        {
          "name": "session",
          "number": 2,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "from",
          "number": 3,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "ends_session",
          "number": 4,
          "type": "boolean",
          "required": true
        },
        {
          "name": "records",
          "number": 5,
          "type": "bytes",
          "required": true
        }
      ]
    },
    {
      "command": "gnss_cold_start",
      "code": 24,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        }
      ]
    },
    {
      "command": "ownship",
      "code": 25,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "aircraft_type",
          "number": 1,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "width",
          "number": 2,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "hot_spot",
          "number": 3,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "rows",
          "number": 4,
          "type": "bytes",
          "required": true
        }
      ]
    },
    {
      "command": "observation",
      "code": 26,
      "endpoint": "peer",
      "sender": "unit",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "address",
          "number": 1,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "boot_epoch",
          "number": 2,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "sequence",
          "number": 3,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "at_s",
          "number": 4,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "into_ms",
          "number": 5,
          "type": "unsigned"
        },
        {
          "name": "latitude_1e7",
          "number": 6,
          "type": "integer"
        },
        {
          "name": "longitude_1e7",
          "number": 7,
          "type": "integer"
        },
        {
          "name": "altitude_m",
          "number": 8,
          "type": "integer"
        },
        {
          "name": "altitude_age_ms",
          "number": 9,
          "type": "integer"
        },
        {
          "name": "pressure_altitude_ft",
          "number": 10,
          "type": "integer"
        },
        {
          "name": "pressure_altitude_age_ms",
          "number": 11,
          "type": "integer"
        },
        {
          "name": "speed_q",
          "number": 12,
          "type": "unsigned"
        },
        {
          "name": "track_c9",
          "number": 13,
          "type": "unsigned"
        },
        {
          "name": "speed_age_ms",
          "number": 14,
          "type": "integer"
        },
        {
          "name": "climb_e8",
          "number": 15,
          "type": "integer"
        },
        {
          "name": "climb_source",
          "number": 16,
          "type": "enum",
          "enum": "climb_source"
        },
        {
          "name": "climb_age_ms",
          "number": 17,
          "type": "integer"
        },
        {
          "name": "flight_state",
          "number": 18,
          "type": "enum",
          "enum": "flight"
        },
        {
          "name": "flight_state_age_ms",
          "number": 19,
          "type": "integer"
        },
        {
          "name": "aircraft_category",
          "number": 20,
          "type": "unsigned"
        },
        {
          "name": "adsb_version",
          "number": 21,
          "type": "unsigned"
        },
        {
          "name": "gva",
          "number": 22,
          "type": "unsigned"
        },
        {
          "name": "callsign",
          "number": 23,
          "type": "text"
        },
        {
          "name": "callsign_heard_s",
          "number": 24,
          "type": "unsigned"
        }
      ]
    },
    {
      "command": "health",
      "code": 27,
      "endpoint": "peer",
      "sender": "unit",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "protocol_version",
          "number": 1,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "boot_epoch",
          "number": 2,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "listening",
          "number": 3,
          "type": "boolean",
          "required": true
        },
        {
          "name": "utc_valid",
          "number": 4,
          "type": "boolean",
          "required": true
        },
        {
          "name": "last_sequence",
          "number": 5,
          "type": "unsigned"
        },
        {
          "name": "own_icao_set",
          "number": 6,
          "type": "boolean",
          "required": true
        },
        {
          "name": "alarm_granted",
          "number": 7,
          "type": "boolean",
          "required": true
        },
        {
          "name": "export_reference",
          "number": 11,
          "type": "enum",
          "enum": "export_reference",
          "required": true
        },
        {
          "name": "paired",
          "number": 12,
          "type": "boolean",
          "required": true
        },
        {
          "name": "own_transponder",
          "number": 13,
          "type": "unsigned"
        },
        {
          "name": "own_transponder_callsign",
          "number": 14,
          "type": "text"
        },
        {
          "name": "own_transponder_category",
          "number": 15,
          "type": "unsigned"
        }
      ]
    },
    {
      "command": "peer_claim",
      "code": 28,
      "endpoint": "peer",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "boot_epoch",
          "number": 1,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "at_s",
          "number": 2,
          "type": "unsigned"
        },
        {
          "name": "into_ms",
          "number": 3,
          "type": "unsigned"
        },
        {
          "name": "latitude_1e7",
          "number": 4,
          "type": "integer"
        },
        {
          "name": "longitude_1e7",
          "number": 5,
          "type": "integer"
        },
        {
          "name": "position_age_ms",
          "number": 6,
          "type": "unsigned"
        },
        {
          "name": "altitude_hae_mm",
          "number": 7,
          "type": "integer"
        },
        {
          "name": "altitude_hae_age_ms",
          "number": 8,
          "type": "unsigned"
        },
        {
          "name": "pressure_altitude_mm",
          "number": 9,
          "type": "integer"
        },
        {
          "name": "pressure_altitude_age_ms",
          "number": 10,
          "type": "unsigned"
        },
        {
          "name": "own_icao",
          "number": 11,
          "type": "unsigned"
        },
        {
          "name": "alarm_requested",
          "number": 12,
          "type": "boolean",
          "required": true
        },
        {
          "name": "paired",
          "number": 18,
          "type": "boolean",
          "required": true
        }
      ]
    },
    {
      "command": "capture",
      "code": 29,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "profile",
          "number": 1,
          "type": "enum",
          "enum": "capture_profile",
          "required": true
        }
      ]
    },
    {
      "command": "browser_challenge",
      "code": 30,
      "endpoint": "config",
      "sender": "both",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "nonce",
          "number": 1,
          "type": "bytes",
          "bytes": 16,
          "required": true
        }
      ]
    },
    {
      "command": "trust_browser",
      "code": 31,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "key",
          "number": 1,
          "type": "bytes",
          "bytes": 32,
          "required": true
        },
        {
          "name": "signature",
          "number": 2,
          "type": "bytes",
          "bytes": 64,
          "required": true
        },
        {
          "name": "label",
          "number": 3,
          "type": "text"
        }
      ]
    },
    {
      "command": "browsers",
      "code": 32,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "index",
          "number": 1,
          "type": "unsigned",
          "required": true
        }
      ]
    },
    {
      "command": "browser",
      "code": 33,
      "endpoint": "config",
      "sender": "unit",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "index",
          "number": 1,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "count",
          "number": 2,
          "type": "unsigned",
          "required": true
        },
        {
          "name": "id",
          "number": 3,
          "type": "bytes",
          "bytes": 8
        },
        {
          "name": "label",
          "number": 4,
          "type": "text"
        }
      ]
    },
    {
      "command": "forget_browser",
      "code": 34,
      "endpoint": "config",
      "sender": "host",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "id",
          "number": 1,
          "type": "bytes",
          "bytes": 8,
          "required": true
        }
      ]
    },
    {
      "command": "install_report",
      "code": 35,
      "endpoint": "config",
      "sender": "both",
      "keys": [
        {
          "name": "command",
          "number": 0,
          "type": "enum",
          "enum": "command"
        },
        {
          "name": "check",
          "number": 1,
          "type": "enum",
          "enum": "staged_check"
        },
        {
          "name": "swap",
          "number": 2,
          "type": "enum",
          "enum": "swap_type"
        },
        {
          "name": "request_error",
          "number": 3,
          "type": "integer"
        },
        {
          "name": "boots",
          "number": 4,
          "type": "unsigned"
        },
        {
          "name": "probation_boots",
          "number": 5,
          "type": "unsigned"
        },
        {
          "name": "proof",
          "number": 6,
          "type": "enum",
          "enum": "proof"
        },
        {
          "name": "reset",
          "number": 7,
          "type": "enum",
          "enum": "reset"
        }
      ]
    }
  ],
  "enums": {
    "reason": {
      "values": [
        {
          "name": "confirm",
          "code": 0
        },
        {
          "name": "confirm_dfu",
          "code": 1
        },
        {
          "name": "confirm_recovery",
          "code": 2
        },
        {
          "name": "confirm_power_off",
          "code": 3
        },
        {
          "name": "confirm_gnss_cold_start",
          "code": 4
        },
        {
          "name": "confirm_erase_log",
          "code": 5
        },
        {
          "name": "confirm_register",
          "code": 6
        },
        {
          "name": "cancelled",
          "code": 7
        },
        {
          "name": "expired",
          "code": 8
        },
        {
          "name": "in_flight",
          "code": 9
        },
        {
          "name": "low_power",
          "code": 10
        },
        {
          "name": "claimed",
          "code": 11
        },
        {
          "name": "no_command",
          "code": 12
        },
        {
          "name": "unknown_command",
          "code": 13
        },
        {
          "name": "no_version",
          "code": 14
        },
        {
          "name": "older",
          "code": 15
        },
        {
          "name": "nothing_staged",
          "code": 16
        },
        {
          "name": "not_approved",
          "code": 17
        },
        {
          "name": "wrong_key",
          "code": 18
        },
        {
          "name": "no_statistics",
          "code": 19
        },
        {
          "name": "install",
          "code": 20
        },
        {
          "name": "dfu",
          "code": 21
        },
        {
          "name": "recovery",
          "code": 22
        },
        {
          "name": "power_off",
          "code": 23
        },
        {
          "name": "erase_log",
          "code": 24
        },
        {
          "name": "gnss_cold_start",
          "code": 25
        },
        {
          "name": "unlock",
          "code": 26
        },
        {
          "name": "keyless",
          "code": 27
        },
        {
          "name": "no_session",
          "code": 28
        },
        {
          "name": "bad_signature",
          "code": 29
        },
        {
          "name": "small_mtu",
          "code": 30
        },
        {
          "name": "flash",
          "code": 31
        },
        {
          "name": "invalid",
          "code": 32
        },
        {
          "name": "out_of_range",
          "code": 33
        },
        {
          "name": "unknown_log",
          "code": 34
        },
        {
          "name": "no_diagnostics",
          "code": 35
        },
        {
          "name": "no_storage",
          "code": 36
        },
        {
          "name": "busy",
          "code": 37
        },
        {
          "name": "flights_only",
          "code": 38
        },
        {
          "name": "read_failed",
          "code": 40
        },
        {
          "name": "erased",
          "code": 41
        },
        {
          "name": "prompt_pending",
          "code": 42
        },
        {
          "name": "refused_on_device",
          "code": 43
        },
        {
          "name": "confirm_capture",
          "code": 44
        },
        {
          "name": "capture",
          "code": 45
        },
        {
          "name": "capturing",
          "code": 46
        },
        {
          "name": "not_capturing",
          "code": 47
        },
        {
          "name": "trusted",
          "code": 48
        },
        {
          "name": "untrusted",
          "code": 49
        },
        {
          "name": "forgotten",
          "code": 50
        },
        {
          "name": "unknown_browser",
          "code": 51
        }
      ]
    },
    "alarm_volume": {
      "mirrors": {
        "type": "go::AlarmVolume",
        "header": "products/skyblip_go/settings.h"
      },
      "values": [
        {
          "name": "off",
          "code": 0,
          "mirrors": "Off"
        },
        {
          "name": "soft",
          "code": 1,
          "mirrors": "Soft"
        },
        {
          "name": "loud",
          "code": 2,
          "mirrors": "Loud"
        }
      ]
    },
    "units": {
      "mirrors": {
        "type": "go::Units",
        "header": "products/skyblip_go/settings.h"
      },
      "values": [
        {
          "name": "nautical",
          "code": 0,
          "mirrors": "Nautical"
        },
        {
          "name": "metric",
          "code": 1,
          "mirrors": "Metric"
        }
      ]
    },
    "flight": {
      "mirrors": {
        "type": "flight::FlightState",
        "header": "core/flight/state.h"
      },
      "values": [
        {
          "name": "unknown",
          "code": 0,
          "mirrors": "Unknown"
        },
        {
          "name": "ground",
          "code": 1,
          "mirrors": "Ground"
        },
        {
          "name": "airborne",
          "code": 2,
          "mirrors": "Airborne"
        }
      ]
    },
    "power_level": {
      "mirrors": {
        "type": "power::PowerLevel",
        "header": "core/power/cutoff.h"
      },
      "values": [
        {
          "name": "unknown",
          "code": 0,
          "mirrors": "Unknown"
        },
        {
          "name": "normal",
          "code": 1,
          "mirrors": "Normal"
        },
        {
          "name": "critical",
          "code": 2,
          "mirrors": "Critical"
        },
        {
          "name": "flat",
          "code": 3,
          "mirrors": "Flat"
        },
        {
          "name": "low",
          "code": 4,
          "mirrors": "Low"
        }
      ]
    },
    "group": {
      "mirrors": {
        "type": "comms::DiagnosticsReport::Group",
        "header": "core/comms/diagnostics.h"
      },
      "values": [
        {
          "name": "system",
          "code": 0,
          "mirrors": "System"
        },
        {
          "name": "radio",
          "code": 1,
          "mirrors": "Radio"
        },
        {
          "name": "traffic",
          "code": 2,
          "mirrors": "Traffic"
        },
        {
          "name": "gnss",
          "code": 3,
          "mirrors": "Gnss"
        },
        {
          "name": "power",
          "code": 4,
          "mirrors": "Power"
        },
        {
          "name": "adsb",
          "code": 5,
          "mirrors": "Adsb"
        }
      ]
    },
    "reset": {
      "mirrors": {
        "type": "power::ResetReason",
        "header": "core/power/reset_reason.h"
      },
      "values": [
        {
          "name": "unknown",
          "code": 0,
          "mirrors": "Unknown"
        },
        {
          "name": "power_on",
          "code": 1,
          "mirrors": "PowerOn"
        },
        {
          "name": "pin",
          "code": 2,
          "mirrors": "Pin"
        },
        {
          "name": "brownout",
          "code": 3,
          "mirrors": "Brownout"
        },
        {
          "name": "software",
          "code": 4,
          "mirrors": "Software"
        },
        {
          "name": "watchdog",
          "code": 5,
          "mirrors": "Watchdog"
        },
        {
          "name": "lockup",
          "code": 6,
          "mirrors": "Lockup"
        },
        {
          "name": "charger_wake",
          "code": 7,
          "mirrors": "ChargerWake"
        },
        {
          "name": "low_power_wake",
          "code": 8,
          "mirrors": "LowPowerWake"
        },
        {
          "name": "debug",
          "code": 9,
          "mirrors": "Debug"
        }
      ]
    },
    "gnss_config": {
      "mirrors": {
        "type": "ports::GnssConfigStage",
        "header": "ports/gnss.h"
      },
      "values": [
        {
          "name": "idle",
          "code": 0,
          "mirrors": "Idle"
        },
        {
          "name": "restarting",
          "code": 1,
          "mirrors": "Restarting"
        },
        {
          "name": "waking",
          "code": 2,
          "mirrors": "Waking"
        },
        {
          "name": "identifying",
          "code": 3,
          "mirrors": "Identifying"
        },
        {
          "name": "sending",
          "code": 4,
          "mirrors": "Sending"
        },
        {
          "name": "verifying",
          "code": 5,
          "mirrors": "Verifying"
        },
        {
          "name": "confirming",
          "code": 6,
          "mirrors": "Confirming"
        },
        {
          "name": "ready",
          "code": 7,
          "mirrors": "Ready"
        },
        {
          "name": "degraded",
          "code": 8,
          "mirrors": "Degraded"
        }
      ]
    },
    "reject": {
      "mirrors": {
        "type": "gnss::FixReject",
        "header": "core/gnss/validity.h"
      },
      "values": [
        {
          "name": "none",
          "code": 0,
          "mirrors": "None"
        },
        {
          "name": "no_solution",
          "code": 1,
          "mirrors": "NoSolution"
        },
        {
          "name": "missing_rmc",
          "code": 2,
          "mirrors": "MissingRmc"
        },
        {
          "name": "missing_gga",
          "code": 3,
          "mirrors": "MissingGga"
        },
        {
          "name": "stale",
          "code": 4,
          "mirrors": "Stale"
        },
        {
          "name": "no_date",
          "code": 5,
          "mirrors": "NoDate"
        },
        {
          "name": "jump",
          "code": 6,
          "mirrors": "Jump"
        }
      ]
    },
    "charge": {
      "mirrors": {
        "type": "power::ChargeCondition",
        "header": "core/power/charging.h"
      },
      "values": [
        {
          "name": "unknown",
          "code": 0,
          "mirrors": "Unknown"
        },
        {
          "name": "ok",
          "code": 1,
          "mirrors": "Ok"
        },
        {
          "name": "too_cold",
          "code": 2,
          "mirrors": "TooCold"
        },
        {
          "name": "too_hot",
          "code": 3,
          "mirrors": "TooHot"
        }
      ]
    },
    "image": {
      "mirrors": {
        "type": "dfu::ImageState",
        "header": "core/dfu/update.h"
      },
      "values": [
        {
          "name": "confirmed",
          "code": 0,
          "mirrors": "Confirmed"
        },
        {
          "name": "probation",
          "code": 1,
          "mirrors": "Probation"
        },
        {
          "name": "reverted",
          "code": 2,
          "mirrors": "Reverted"
        },
        {
          "name": "refused",
          "code": 3,
          "mirrors": "Refused"
        }
      ]
    },
    "settings_fallback": {
      "mirrors": {
        "type": "settings::Fallback",
        "header": "core/settings/blob.h"
      },
      "values": [
        {
          "name": "none",
          "code": 0,
          "mirrors": "None"
        },
        {
          "name": "prior",
          "code": 1,
          "mirrors": "Prior"
        },
        {
          "name": "defaults",
          "code": 2,
          "mirrors": "Defaults"
        }
      ]
    },
    "hub_image": {
      "mirrors": {
        "type": "dfu::HubImage",
        "header": "core/dfu/update.h"
      },
      "values": [
        {
          "name": "none",
          "code": 0,
          "mirrors": "None"
        },
        {
          "name": "missing",
          "code": 1,
          "mirrors": "Missing"
        },
        {
          "name": "corrupt",
          "code": 2,
          "mirrors": "Corrupt"
        },
        {
          "name": "unreadable",
          "code": 3,
          "mirrors": "Unreadable"
        },
        {
          "name": "writing",
          "code": 4,
          "mirrors": "Writing"
        },
        {
          "name": "held",
          "code": 5,
          "mirrors": "Held"
        }
      ]
    },
    "export_reference": {
      "mirrors": {
        "type": "comms::ExportReference",
        "header": "core/comms/peer.h"
      },
      "values": [
        {
          "name": "none",
          "code": 0,
          "mirrors": "None"
        },
        {
          "name": "go",
          "code": 1,
          "mirrors": "Go"
        },
        {
          "name": "own",
          "code": 2,
          "mirrors": "Own"
        }
      ]
    },
    "bravo": {
      "mirrors": {
        "type": "comms::BravoState",
        "header": "core/comms/peer.h",
        "off_the_wire": [
          "Unpaired"
        ]
      },
      "values": [
        {
          "name": "waiting",
          "code": 1,
          "mirrors": "Waiting"
        },
        {
          "name": "live",
          "code": 2,
          "mirrors": "Live"
        },
        {
          "name": "link_down",
          "code": 3,
          "mirrors": "LinkDown"
        },
        {
          "name": "receiver_down",
          "code": 4,
          "mirrors": "ReceiverDown"
        },
        {
          "name": "no_utc",
          "code": 5,
          "mirrors": "NoUtc"
        }
      ]
    },
    "climb_source": {
      "mirrors": {
        "type": "model::ClimbSource",
        "header": "core/model/aircraft.h"
      },
      "values": [
        {
          "name": "unknown",
          "code": 0,
          "mirrors": "Unknown"
        },
        {
          "name": "gnss",
          "code": 1,
          "mirrors": "Gnss"
        },
        {
          "name": "barometric",
          "code": 2,
          "mirrors": "Barometric"
        }
      ]
    },
    "product": {
      "mirrors": {
        "type": "dfu::Product",
        "header": "core/dfu/update.h"
      },
      "values": [
        {
          "name": "skyblip_go",
          "code": 0,
          "mirrors": "SkyblipGo"
        },
        {
          "name": "skyblip_bravo",
          "code": 1,
          "mirrors": "SkyblipBravo"
        }
      ]
    },
    "log": {
      "mirrors": {
        "type": "store::SectorOwner",
        "header": "core/store/sector.h",
        "off_the_wire": [
          "None"
        ]
      },
      "values": [
        {
          "name": "flights",
          "code": 1,
          "mirrors": "Flights"
        },
        {
          "name": "diagnostics",
          "code": 2,
          "mirrors": "Diagnostics"
        }
      ]
    },
    "capture_profile": {
      "mirrors": {
        "type": "comms::CaptureProfile",
        "header": "core/comms/config.h"
      },
      "values": [
        {
          "name": "full",
          "code": 0,
          "mirrors": "Full"
        },
        {
          "name": "power_run",
          "code": 1,
          "mirrors": "PowerRun"
        },
        {
          "name": "flight_run",
          "code": 2,
          "mirrors": "FlightRun"
        },
        {
          "name": "stop",
          "code": 3,
          "mirrors": "Stop"
        }
      ]
    },
    "staged_check": {
      "mirrors": {
        "type": "ports::StagedCheck",
        "header": "ports/dfu.h"
      },
      "values": [
        {
          "name": "unchecked",
          "code": 0,
          "mirrors": "Unchecked"
        },
        {
          "name": "matched",
          "code": 1,
          "mirrors": "Matched"
        },
        {
          "name": "damaged",
          "code": 2,
          "mirrors": "Damaged"
        },
        {
          "name": "unreadable",
          "code": 3,
          "mirrors": "Unreadable"
        },
        {
          "name": "undigested",
          "code": 4,
          "mirrors": "Undigested"
        }
      ]
    },
    "swap_type": {
      "mirrors": {
        "type": "ports::SwapType",
        "header": "ports/dfu.h"
      },
      "values": [
        {
          "name": "unknown",
          "code": 0,
          "mirrors": "Unknown"
        },
        {
          "name": "none",
          "code": 1,
          "mirrors": "None"
        },
        {
          "name": "test",
          "code": 2,
          "mirrors": "Test"
        },
        {
          "name": "permanent",
          "code": 3,
          "mirrors": "Permanent"
        },
        {
          "name": "revert",
          "code": 4,
          "mirrors": "Revert"
        },
        {
          "name": "fail",
          "code": 5,
          "mirrors": "Fail"
        },
        {
          "name": "panic",
          "code": 6,
          "mirrors": "Panic"
        }
      ]
    },
    "proof": {
      "mirrors": {
        "type": "dfu::Proof",
        "header": "core/dfu/update.h"
      },
      "values": [
        {
          "name": "none",
          "code": 0,
          "mirrors": "None"
        },
        {
          "name": "booted",
          "code": 1,
          "mirrors": "Booted"
        },
        {
          "name": "started",
          "code": 2,
          "mirrors": "Started"
        },
        {
          "name": "gnss_heard",
          "code": 3,
          "mirrors": "GnssHeard"
        },
        {
          "name": "hub_bootable",
          "code": 4,
          "mirrors": "HubBootable"
        },
        {
          "name": "proven",
          "code": 5,
          "mirrors": "Proven"
        },
        {
          "name": "confirm_failed",
          "code": 6,
          "mirrors": "ConfirmFailed"
        }
      ]
    },
    "command": {
      "values": [
        {
          "name": "acknowledgement",
          "code": 0
        },
        {
          "name": "get",
          "code": 1
        },
        {
          "name": "config",
          "code": 2
        },
        {
          "name": "defaults",
          "code": 3
        },
        {
          "name": "set",
          "code": 4
        },
        {
          "name": "status",
          "code": 5
        },
        {
          "name": "timing",
          "code": 6
        },
        {
          "name": "flash",
          "code": 7
        },
        {
          "name": "radio",
          "code": 8
        },
        {
          "name": "diagnostics",
          "code": 9
        },
        {
          "name": "update",
          "code": 10
        },
        {
          "name": "dfu",
          "code": 11
        },
        {
          "name": "recovery",
          "code": 12
        },
        {
          "name": "power_off",
          "code": 13
        },
        {
          "name": "challenge",
          "code": 14
        },
        {
          "name": "register",
          "code": 15
        },
        {
          "name": "registration",
          "code": 16
        },
        {
          "name": "unlock",
          "code": 17
        },
        {
          "name": "list",
          "code": 18
        },
        {
          "name": "read",
          "code": 19
        },
        {
          "name": "erase",
          "code": 20
        },
        {
          "name": "log",
          "code": 21
        },
        {
          "name": "session",
          "code": 22
        },
        {
          "name": "chunk",
          "code": 23
        },
        {
          "name": "gnss_cold_start",
          "code": 24
        },
        {
          "name": "ownship",
          "code": 25
        },
        {
          "name": "observation",
          "code": 26
        },
        {
          "name": "health",
          "code": 27
        },
        {
          "name": "peer_claim",
          "code": 28
        },
        {
          "name": "capture",
          "code": 29
        },
        {
          "name": "browser_challenge",
          "code": 30
        },
        {
          "name": "trust_browser",
          "code": 31
        },
        {
          "name": "browsers",
          "code": 32
        },
        {
          "name": "browser",
          "code": 33
        },
        {
          "name": "forget_browser",
          "code": 34
        },
        {
          "name": "install_report",
          "code": 35
        }
      ]
    }
  }
});
