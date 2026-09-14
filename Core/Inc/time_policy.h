#ifndef TIME_POLICY_H
#define TIME_POLICY_H

#define TIME_MIN_VALID_YEAR           2024U
#define TIME_MAX_VALID_YEAR           2099U
#define TIME_SYNC_INTERVAL_SEC        3600U
#define TIME_RESYNC_DRIFT_SEC         5U
#define TIME_LARGE_STEP_SEC           300U
#define TIME_MAX_HOLDOVER_SEC         (7U * 24U * 3600U)
#define TIME_CONFIRM_SAMPLE_COUNT     3U
#define TIME_CONFIRM_TOLERANCE_SEC    2U
#define TIME_CONFIRM_RETRY_SEC        16U
#define TIME_DNS_TIMEOUT_SEC          15U
#define TIME_SNTP_RESPONSE_TIMEOUT_SEC 75U
#define TIME_RETRY_MAX_SEC            60U
#define TIME_RETRY_JITTER_MAX_MS      250U
#define TIME_SAMPLE_QUEUE_LENGTH      4U
#define TIME_SYNC_TASK_STACK_WORDS    512U
#define TIME_SYNC_TASK_PRIORITY       2U
#define TIME_FAULT_INJECTION_ENABLE   0U

#define APP_NTP_SERVER_1              "pool.ntp.org"
#define APP_NTP_SERVER_2              "time.google.com"

#endif /* TIME_POLICY_H */
