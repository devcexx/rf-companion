#include "stored_codes.h"
#include "rf/clemsacode.h"
#include "rf/fixedcode.h"
#include "rf/rf.h"
#include "private.h"

#define CLEMSA_DEFAULT_REPETITION_COUNT 10
#define TESLA_CODE_REPETITION_COUNT 10

#define TESLA_CHARGER_BIT_RATE_SECOND 2479
#define TESLA_CHARGER_SIGNAL_PERIOD_US (1000000 / TESLA_CHARGER_BIT_RATE_SECOND)
#define TESLA_CHARGER_DISTANCE_BETWEEN_REPETITIONS_US 23000
#define TESLA_CHARGER_NUM_REPETITIONS 5

// FIXME: This code used to work with old STX882 transmitter, but it doesn't work with CC1101
// Ref: https://github.com/rgerganov/tesla-opener
static const uint8_t tesla_charger_door_payload[43] = {
    0x02, 0xAA, 0xAA, 0xAA, // Preamble of 26 bits by repeating 1010
    0x2B,                   // Sync byte
    0x2C, 0xCB, 0x33, 0x33, 0x2D, 0x34, 0xB5, 0x2B, 0x4D, 0x32,
    0xAD, 0x2C, 0x56, 0x59, 0x96, 0x66, 0x66, 0x5A, 0x69, 0x6A,
    0x56, 0x9A, 0x65, 0x5A, 0x58, 0xAC, 0xB3, 0x2C, 0xCC, 0xCC,
    0xB4, 0xD2, 0xD4, 0xAD, 0x34, 0xCA, 0xB4, 0xA0};

static const clemsa_codegen_tx_t clemsa_code_home_1_exit = {
    ._generator = &clemsa_generator,
    .tx_name = "Home 1 garage exit code",
    .code = HOME_1_GARAGE_EXIT_CODE,
    .code_len = CLEMSA_CODEGEN_DEFAULT_CODE_SIZE,
    .repetition_count = CLEMSA_DEFAULT_REPETITION_COUNT};

static const clemsa_codegen_tx_t clemsa_code_home_1_enter = {
    ._generator = &clemsa_generator,
    .tx_name = "Home 1 garage enter code",
    .code = HOME_1_GARAGE_ENTER_CODE,
    .code_len = CLEMSA_CODEGEN_DEFAULT_CODE_SIZE,
    .repetition_count = CLEMSA_DEFAULT_REPETITION_COUNT};

static const clemsa_codegen_tx_t clemsa_code_parents_a = {
    ._generator = &clemsa_generator,
    .tx_name = "Parents garage code A",
    .code = PARENTS_GARAGE_CODE_A,
    .code_len = CLEMSA_CODEGEN_DEFAULT_CODE_SIZE,
    .repetition_count = CLEMSA_DEFAULT_REPETITION_COUNT};

static const clemsa_codegen_tx_t clemsa_code_parents_b = {
    ._generator = &clemsa_generator,
    .tx_name = "Parents garage code B",
    .code = PARENTS_GARAGE_CODE_B,
    .code_len = CLEMSA_CODEGEN_DEFAULT_CODE_SIZE,
    .repetition_count = CLEMSA_DEFAULT_REPETITION_COUNT};

static const fixedcode_tx_t tesla_charger_opener = {
    ._generator = &fixedcode_generator,
    .tx_name = "Tesla Charger door opener code",
    .bit_rate = 2479,
    .data = tesla_charger_door_payload,
    .ticks_between_repetitions = 57,
    .code_len = 43,
    .repetitions = 5};

static const clemsa_codegen_tx_t clemsa_code_home_2_exit = {
    ._generator = &clemsa_generator,
    .tx_name = "Home 2 garage exit code",
    .code = HOME_2_GARAGE_EXIT_CODE,
    .code_len = CLEMSA_CODEGEN_DEFAULT_CODE_SIZE,
    .repetition_count = CLEMSA_DEFAULT_REPETITION_COUNT};

static const clemsa_codegen_tx_t clemsa_code_home_2_enter = {
    ._generator = &clemsa_generator,
    .tx_name = "Home 2 garage enter code",
    .code = HOME_2_GARAGE_ENTER_CODE,
    .code_len = CLEMSA_CODEGEN_DEFAULT_CODE_SIZE,
    .repetition_count = CLEMSA_DEFAULT_REPETITION_COUNT};

const rf_antenna_tx_t* stored_codes[STORED_CODES_COUNT] = {
  (rf_antenna_tx_t*) &clemsa_code_home_1_exit,
  (rf_antenna_tx_t*) &clemsa_code_home_1_enter,
  (rf_antenna_tx_t*) &clemsa_code_parents_a,
  (rf_antenna_tx_t*) &clemsa_code_parents_b,
  (rf_antenna_tx_t*) &tesla_charger_opener,
  (rf_antenna_tx_t*) &clemsa_code_home_2_exit,
  (rf_antenna_tx_t*) &clemsa_code_home_2_enter,
};
