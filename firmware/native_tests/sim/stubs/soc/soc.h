#pragma once
#include <cstdint>
uint32_t sim_reg_read(int reg);
#define REG_READ(reg) sim_reg_read(reg)
