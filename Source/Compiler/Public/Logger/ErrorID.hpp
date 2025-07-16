/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * This file storied ErrorIDs
 */

#pragma once

#include <cstddef>

#define ERRORID const size_t

ERRORID E_UnClosedStringLiteral = 1001;
ERRORID E_UnclosedCharLiteral = 1002;
ERRORID E_UnknownCharacter = 1003;

ERRORID E_UnexpectFinishing = 2001;
ERRORID E_ExpectAnIdentifier = 2002;
ERRORID E_MutidefinedStruct = 2003;
ERRORID E_ExpectALBRACE = 2004;
ERRORID E_UndefinedStruct = 2005;
ERRORID E_ExpectARBRACE = 2006;
ERRORID E_ExpectALPAREN = 2007;
ERRORID E_ExpectARPAREN = 2008;
ERRORID E_ExpectACOLON = 2009;
ERRORID E_UndefinedType = 2010;
ERRORID E_ExpectedKeyword = 2011;
ERRORID E_ExpectASEMI = 2012;
ERRORID E_ExpectAnASSIGN = 2013;
ERRORID E_ExpectType = 2014;
ERRORID E_ExpectedExpression = 2015;
ERRORID E_InvalidLiteralType = 2016;