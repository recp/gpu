#ifndef gpu_validation_f16_builtins_h
#define gpu_validation_f16_builtins_h

#include <stdint.h>

enum {
  F16_BUILTIN_CASES                 = 4u,
  F16_BUILTIN_INPUT_ROWS            = F16_BUILTIN_CASES * 2u,
  F16_BUILTIN_MATH_ROWS             = 32u,
  F16_BUILTIN_GEOMETRIC_ROWS        = 8u,
  F16_BUILTIN_TRIG_ROWS             = 16u,
  F16_BUILTIN_BASE_OUTPUTS_PER_CASE = F16_BUILTIN_MATH_ROWS +
                                      F16_BUILTIN_GEOMETRIC_ROWS +
                                      F16_BUILTIN_TRIG_ROWS,
  F16_BUILTIN_WIDTHS                = 3u,
  F16_BUILTIN_WIDTH_ROWS            = F16_BUILTIN_MATH_ROWS +
                                      F16_BUILTIN_TRIG_ROWS,
  F16_BUILTIN_WIDTH_ROWS_PER_CASE   = F16_BUILTIN_WIDTHS *
                                      F16_BUILTIN_WIDTH_ROWS,
  F16_BUILTIN_OUTPUTS_PER_CASE      = F16_BUILTIN_BASE_OUTPUTS_PER_CASE +
                                      F16_BUILTIN_WIDTH_ROWS_PER_CASE,
  F16_BUILTIN_OUTPUT_ROWS           = F16_BUILTIN_CASES *
                                      F16_BUILTIN_OUTPUTS_PER_CASE,
  F16_BUILTIN_BASE_CHECKS           = F16_BUILTIN_CASES *
                                      F16_BUILTIN_BASE_OUTPUTS_PER_CASE * 4u,
  F16_BUILTIN_WIDTH_CHECKS_PER_CASE = F16_BUILTIN_WIDTH_ROWS * 6u,
  F16_BUILTIN_CHECKS                = F16_BUILTIN_BASE_CHECKS +
                                      F16_BUILTIN_CASES *
                                      F16_BUILTIN_WIDTH_CHECKS_PER_CASE
};

extern const float gpu_f16_builtin_inputs[F16_BUILTIN_INPUT_ROWS][4];

int
gpu_f16_builtin_validate(
  const uint16_t output[F16_BUILTIN_OUTPUT_ROWS][4]
);

#endif /* gpu_validation_f16_builtins_h */
