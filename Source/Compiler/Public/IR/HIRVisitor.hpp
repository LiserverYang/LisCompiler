/**
 * Copyright 2026, LiserverYang. All rights reserved.
 * MIT License.
 */

#pragma once

class HIRProgram;
class HIRNameRef;
class HIRLiteral;
class HIRBinaryOp;
class HIRCast;
class HIRCall;
class HIRMemberAccess;
class HIRIndexAccess;
class HIRArrayLiteral;
class HIRStructInit;
class HIRVariantInit;
class HIRRef;
class HIRBlock;
class HIRVarDecl;
class HIRAssign;
class HIRIf;
class HIRMatch;
class HIRLoop;
class HIRReturn;
class HIRBreak;
class HIRContinue;
class HIRExprStmt;
class HIRFunction;
class HIRStruct;
class HIREnum;
class HIRTrait;
class HIRImpl;
class HIRImport;

class HIRVisitor
{
public:
    virtual ~HIRVisitor() = default;
    virtual void visit(HIRProgram *node) = 0;
    virtual void visit(HIRNameRef *node) = 0;
    virtual void visit(HIRLiteral *node) = 0;
    virtual void visit(HIRBinaryOp *node) = 0;
    virtual void visit(HIRCast *node) = 0;
    virtual void visit(HIRCall *node) = 0;
    virtual void visit(HIRMemberAccess *node) = 0;
    virtual void visit(HIRIndexAccess *node) = 0;
    virtual void visit(HIRArrayLiteral *node) = 0;
    virtual void visit(HIRStructInit *node) = 0;
    virtual void visit(HIRVariantInit *node) = 0;
    virtual void visit(HIRRef *node) = 0;
    virtual void visit(HIRBlock *node) = 0;
    virtual void visit(HIRVarDecl *node) = 0;
    virtual void visit(HIRAssign *node) = 0;
    virtual void visit(HIRIf *node) = 0;
    virtual void visit(HIRMatch *node) = 0;
    virtual void visit(HIRLoop *node) = 0;
    virtual void visit(HIRReturn *node) = 0;
    virtual void visit(HIRBreak *node) = 0;
    virtual void visit(HIRContinue *node) = 0;
    virtual void visit(HIRExprStmt *node) = 0;
    virtual void visit(HIRFunction *node) = 0;
    virtual void visit(HIRStruct *node) = 0;
    virtual void visit(HIREnum *node) = 0;
    virtual void visit(HIRTrait *node) = 0;
    virtual void visit(HIRImpl *node) = 0;
    virtual void visit(HIRImport *node) = 0;
};