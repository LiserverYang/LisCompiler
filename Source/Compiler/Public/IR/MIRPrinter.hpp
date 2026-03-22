/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once
#include "MIR.hpp"
#include <ostream>

void printMIRProgram(const MIRProgram &prog, std::ostream &out);
void printMIRFunction(const MIRFunction &fn, std::ostream &out);
void printMIRBody(const MIRBody &body, std::ostream &out);