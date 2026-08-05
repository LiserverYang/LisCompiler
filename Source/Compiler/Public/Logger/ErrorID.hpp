/**
 * Copyright 2025, LiserverYang. All rights reserved.
 * This file storied ErrorIDs
 */

#pragma once

#include <cstddef>

#define ERRORID const size_t

// Lexing errors

ERRORID E_UnClosedStringLiteral = 1001;
ERRORID E_UnclosedCharLiteral = 1002;
ERRORID E_UnknownCharacter = 1003;
ERRORID E_UnclosedBlockComment = 1004;

// Parsing errors

ERRORID E_UnexpectFinish = 2001;
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
ERRORID E_MutidefinedTrait = 2017;

// Semantic (HIR) errors

ERRORID E_SemanticError = 3001;          // general/default semantic error
ERRORID E_TypeMismatch = 3002;
ERRORID E_UndefinedIdentifier = 3003;
ERRORID E_AssignToImmutable = 3004;
ERRORID E_UseOfMovedValue = 3005;
ERRORID E_ArgMismatch = 3006;
ERRORID E_GenericError = 3007;
ERRORID E_TraitError = 3008;
ERRORID E_ReturnTypeMismatch = 3009;
ERRORID E_CastError = 3010;

// Borrow-checker errors (4000 series)

ERRORID E_CannotBorrowMutWhileBorrowed = 4001;
ERRORID E_CannotBorrowWhileMutBorrowed = 4002;
ERRORID E_CannotMutateWhileBorrowed = 4003;
ERRORID E_CannotMoveWhileBorrowed = 4004;
ERRORID E_CannotBorrowMovedValue = 4005;
ERRORID E_CannotBorrowAsMutable = 4006;
ERRORID E_BorrowDoesNotLiveLongEnough = 4007;
