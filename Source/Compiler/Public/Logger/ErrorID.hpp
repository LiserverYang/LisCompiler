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
// 2010 is retired: it was declared for "undefined type" but the parser has
// never emitted it (an unknown type name is not a parse error).
ERRORID E_ExpectedKeyword = 2011;
ERRORID E_ExpectASEMI = 2012;
ERRORID E_ExpectAnASSIGN = 2013;
ERRORID E_ExpectType = 2014;
ERRORID E_ExpectedExpression = 2015;
ERRORID E_InvalidLiteralType = 2016;
ERRORID E_MutidefinedTrait = 2017;

// Semantic (HIR) errors

ERRORID E_SemanticError = 3001; // general/default semantic error
ERRORID E_TypeMismatch = 3002;
ERRORID E_UndefinedIdentifier = 3003;
ERRORID E_AssignToImmutable = 3004;
ERRORID E_UseOfMovedValue = 3005;
// 3006-3010 are retired (argument mismatch / generic / trait / return type /
// cast): they were declared but never emitted. Every one of those failures is
// reported as E3001 or E3002 with a specific message, which is the single
// convention the analyzer follows.
ERRORID E_UseOfUninitializedValue = 3011;   // read/borrow/move of a `let x;` binding
ERRORID E_UninitializedNonCopyBinding = 3012; // `let x: <Move>;` without an initializer
ERRORID E_UnsafeBuiltinOutsideStdlib = 3013;  // heap primitive called outside the stdlib
ERRORID E_PointerOpOutsideStdlib = 3014;      // raw-pointer indexing/deref outside the stdlib
ERRORID E_PrivateFieldAccess = 3015;          // private field read/constructed outside its type
ERRORID E_MoveOutOfDropType = 3016;           // non-Copy field moved out of a type with impl Drop

// Borrow-checker errors (4000 series)

ERRORID E_CannotBorrowMutWhileBorrowed = 4001;
ERRORID E_CannotBorrowWhileMutBorrowed = 4002;
ERRORID E_CannotMutateWhileBorrowed = 4003;
ERRORID E_CannotMoveWhileBorrowed = 4004;
ERRORID E_CannotBorrowMovedValue = 4005;
ERRORID E_CannotBorrowAsMutable = 4006;
ERRORID E_BorrowDoesNotLiveLongEnough = 4007;

// Enum / match errors (5000 series)

ERRORID E_UnknownVariant = 5001;
ERRORID E_VariantArgMismatch = 5002;
ERRORID E_MatchOnNonEnum = 5003;
ERRORID E_NonExhaustiveMatch = 5004;
ERRORID E_VariantPatternMismatch = 5005;

// Error propagation (`expr?`) / Result errors (6000 series)

ERRORID E_TryNotResult = 6001;        // operand is not a Result value
ERRORID E_TryNotInResultFn = 6002;    // enclosing function does not return a Result
ERRORID E_TryErrorTypeMismatch = 6003; // operand error type != function error type
