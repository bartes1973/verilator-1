// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Replace return/continue with jumps
//
// Code available from: https://verilator.org
//
//*************************************************************************
//
// Copyright 2003-2020 by Wilson Snyder. This program is free software; you
// can redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************
// V3Increments's Transformations:
//
// XXX: TODO: Write some docs
//
//*************************************************************************

#include "config_build.h"
#include "verilatedos.h"

#include "V3Global.h"
#include "V3Increments.h"
#include "V3Ast.h"

#include <algorithm>
#include <cstdarg>
#include <vector>

#include <iostream>

//######################################################################

class IncrementsVisitor : public AstNVisitor {
private:

    // TYPES
    enum InsertMode {
        IM_BEFORE,  // Pointing at statement ref is in, insert before this
        IM_AFTER,  // Pointing at last inserted stmt, insert after
        IM_WHILE_PRECOND  // Pointing to for loop, add to body end
    };

    // STATE
    int m_modIncrementsNum;  // Var name counter
    InsertMode m_insMode;  // How to insert
    AstNode* m_insStmtp;  // Where to insert statement

    // METHODS
    void insertBefore(AstNode* placep, AstNode* newp) {
        AstNRelinker linker;
        placep->unlinkFrBack(&linker);
        newp->addNext(placep);
        linker.relink(newp);
    }

private:
    void insertBeforeStmt(AstNode* nodep, AstNode* newp) {
        // Return node that must be visited, if any
        // See also AstNode::addBeforeStmt; this predates that function
        std::cout << "[Increments] [" << __func__ << ":" << __LINE__ << "]" <<  std::endl;
        if (debug() >= 9) nodep->dumpTree(cout, "-newstmt:");
        UASSERT_OBJ(m_insStmtp, nodep, "Function not underneath a statement");
        if (m_insMode == IM_BEFORE) {
            // Add the whole thing before insertAt
            UINFO(5, "     IM_Before  " << m_insStmtp << endl);
            if (debug() >= 9) newp->dumpTree(cout, "-newfunc:");
            m_insStmtp->addHereThisAsNext(newp);
        } else if (m_insMode == IM_AFTER) {
            UINFO(5, "     IM_After   " << m_insStmtp << endl);
            m_insStmtp->addNextHere(newp);
        } else if (m_insMode == IM_WHILE_PRECOND) {
            UINFO(5, "     IM_While_Precond " << m_insStmtp << endl);
            AstWhile* whilep = VN_CAST(m_insStmtp, While);
            UASSERT_OBJ(whilep, nodep, "Insert should be under WHILE");
            whilep->addPrecondsp(newp);
        } else {
            nodep->v3fatalSrc("Unknown InsertMode");
        }
        m_insMode = IM_AFTER;
        m_insStmtp = newp;
    }

    virtual void visit(AstWhile* nodep) VL_OVERRIDE {
        // Special, as statements need to be put in different places
        // Preconditions insert first just before themselves (the normal
        // rule for other statement types)
        m_insStmtp = NULL;  // First thing should be new statement
        iterateAndNextNull(nodep->precondsp());
        // Conditions insert first at end of precondsp.
        m_insMode = IM_WHILE_PRECOND;
        m_insStmtp = nodep;
        iterateAndNextNull(nodep->condp());
        // Body insert just before themselves
        m_insStmtp = NULL;  // First thing should be new statement
        iterateAndNextNull(nodep->bodysp());
        iterateAndNextNull(nodep->incsp());
        // Done the loop
        m_insStmtp = NULL;  // Next thing should be new statement
    }
    virtual void visit(AstNodeFor* nodep) VL_OVERRIDE {
        nodep->v3fatalSrc(
            "For statements should have been converted to while statements in V3Begin.cpp");
    }
    virtual void visit(AstNodeStmt* nodep) VL_OVERRIDE {
        if (!nodep->isStatement()) {
            iterateChildren(nodep);
            return;
        }
        m_insMode = IM_BEFORE;
        m_insStmtp = nodep;
        iterateChildren(nodep);
        m_insStmtp = NULL;  // Next thing should be new statement
    }

    virtual void visit(AstPreAdd* nodep) VL_OVERRIDE {
        iterateChildren(nodep);
        AstNodeVarRef* vr = VN_CAST(nodep->op1p(), VarRef);
        AstConst* constp = VN_CAST(nodep->op2p(), Const);
        AstConst* newconstp = new AstConst(nodep->fileline(), constp->num());
        AstNode* backp = nodep->backp();

        nodep->replaceWith(new AstVarRef(backp->fileline(), vr->varp(), false));

        VL_DO_DANGLING(nodep->deleteTree(), nodep);

        AstNode* replacep = backp->op1p();
        FileLine* fl = replacep->fileline();

        insertBeforeStmt(nodep,
                     new AstAssign(fl, new AstVarRef(fl, vr->varp(), true),
                                   new AstAdd(fl, new AstVarRef(fl, vr->varp(), false),
                                              newconstp)));
    }

    virtual void visit(AstPostAdd* nodep) VL_OVERRIDE {
        iterateChildren(nodep);

        AstNodeVarRef* vr = VN_CAST(nodep->op1p(), VarRef);
        AstConst* constp = VN_CAST(nodep->op2p(), Const);
        AstConst* newconstp = new AstConst(nodep->fileline(), constp->num());
        AstNode* backp = nodep->backp();

        AstNode* replacep = backp->op1p();
        FileLine* fl = replacep->fileline();

        string name = string("__Vincrement") + cvtToStr(m_modIncrementsNum++);

        AstVar* varp
            = new AstVar(fl, AstVarType::BLOCKTEMP, name, vr->varp()->subDTypep());
        insertBeforeStmt(nodep, varp);

        insertBefore(nodep, new AstAssign(fl, new AstVarRef(fl, varp, true),
                                                  new AstVarRef(fl, vr->varp(), false)));

        insertBefore(nodep,
                     new AstAssign(fl, new AstVarRef(fl, vr->varp(), true),
                                   new AstAdd(fl, new AstVarRef(fl, vr->varp(), false),
                                              newconstp)));

        nodep->replaceWith(new AstVarRef(backp->fileline(), varp, false));

        VL_DO_DANGLING(nodep->deleteTree(), nodep);
    }

    virtual void visit(AstNode* nodep) VL_OVERRIDE { iterateChildren(nodep); }

public:
    // CONSTRUCTORS
    explicit IncrementsVisitor(AstNetlist* nodep) {
        m_modIncrementsNum = 0;
        m_insMode = IM_BEFORE;
        m_insStmtp = NULL;
        iterate(nodep);
    }

    virtual ~IncrementsVisitor() {}

};

void V3Increments::increments(AstNetlist* nodep) {
    UINFO(2, __FUNCTION__ << ": " << endl);
    { IncrementsVisitor bvisitor(nodep); }  // Destruct before checking
    V3Global::dumpCheckGlobalTree("link", 0, v3Global.opt.dumpTreeLevel(__FILE__) >= 3);
}
