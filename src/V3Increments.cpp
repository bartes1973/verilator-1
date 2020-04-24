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

class IncrementsAssignVisitor : public AstNVisitor {
private:
    // NODE STATE
    //  AstVar::user4()         // bool; occurs on LHS of current assignment
    AstUser4InUse m_inuser4;

    // STATE
    bool m_noopt;  // Disable optimization of variables in this block

    // METHODS
    VL_DEBUG_FUNC;  // Declare debug()

    // VISITORS
    virtual void visit(AstNodeAssign* nodep) VL_OVERRIDE {
        // AstNode::user4ClearTree();  // Implied by AstUser4InUse
        // LHS first as fewer varrefs
        iterateAndNextNull(nodep->lhsp());
        // Now find vars marked as lhs
        iterateAndNextNull(nodep->rhsp());
    }
    virtual void visit(AstVarRef* nodep) VL_OVERRIDE {
        // it's LHS var is used so need a deep temporary
        if (nodep->lvalue()) {
            nodep->varp()->user4(true);
        } else {
            if (nodep->varp()->user4()) {
                if (!m_noopt) UINFO(4, "Block has LHS+RHS var: " << nodep << endl);
                m_noopt = true;
            }
        }
    }
    virtual void visit(AstNode* nodep) VL_OVERRIDE { iterateChildren(nodep); }

public:
    // CONSTRUCTORS
    explicit IncrementsAssignVisitor(AstNodeAssign* nodep) {
        UINFO(4, "  IncrementsAssignVisitor on " << nodep << endl);
        m_noopt = false;
        iterate(nodep);
    }
    virtual ~IncrementsAssignVisitor() {}
    bool noOpt() const { return m_noopt; }
};


class IncrementsVisitor : public AstNVisitor {
private:
    // STATE
    int m_modIncrementsNum;  // Var name counter

    AstNodeModule* m_modp;  // Current module
    AstCFunc* m_funcp;  // Current block
    AstNode* m_stmtp;  // Current statement
    AstWhile* m_inWhilep;  // Inside while loop, special statement additions
    AstTraceInc* m_inTracep;  // Inside while loop, special statement additions
    bool m_assignLhs;  // Inside assignment lhs, don't breakup extracts

    // METHODS
    void insertBefore(AstNode* placep, AstNode* newp) {
        AstNRelinker linker;
        placep->unlinkFrBack(&linker);
        newp->addNext(placep);
        linker.relink(newp);
    }

    AstVar* getBlockTemp(AstNode* nodep) {
        string newvarname = (string("__Vtemp") + cvtToStr(m_modp->varNumGetInc()));
        AstVar* varp
            = new AstVar(nodep->fileline(), AstVarType::STMTTEMP, newvarname, nodep->dtypep());
        m_funcp->addInitsp(varp);
        return varp;
    }

    void insertBeforeStmt(AstNode* newp) {
        // 
        // Insert newp before m_stmtp
        if (m_inWhilep) {
            // Statements that are needed for the 'condition' in a while
            // actually have to be put before & after the loop, since we
            // can't do any statements in a while's (cond).
            m_inWhilep->addPrecondsp(newp);
        } else if (m_inTracep) {
            m_inTracep->addPrecondsp(newp);
        } else if (m_stmtp) {
            AstNRelinker linker;
            m_stmtp->unlinkFrBack(&linker);
            newp->addNext(m_stmtp);
            linker.relink(newp);
        } else {
            newp->v3fatalSrc("No statement insertion point.");
        }
    }

    void createDeepTemp(AstNode* nodep, bool noSubst) {
        if (debug() > 8) nodep->dumpTree(cout, "deepin:");

        std::cout << "[mjsob] [" << __func__ << ":" << __LINE__ << std::endl;

        AstNRelinker linker;
        nodep->unlinkFrBack(&linker);

        AstVar* varp = getBlockTemp(nodep);
        if (noSubst) varp->noSubst(true);  // Do not remove varrefs to this in V3Const
        // Replace node tree with reference to var
        AstVarRef* newp = new AstVarRef(nodep->fileline(), varp, false);
        linker.relink(newp);
        // Put assignment before the referencing statement
        AstAssign* assp = new AstAssign(nodep->fileline(),
                                        new AstVarRef(nodep->fileline(), varp, true), nodep);
        insertBeforeStmt(assp);
        if (debug() > 8) assp->dumpTree(cout, "deepou:");
        nodep->user1(true);  // Don't add another assignment
    }

public:
    // CONSTRUCTORS
    explicit IncrementsVisitor(AstNetlist* nodep) {
        m_modIncrementsNum = 0;
        m_modp = NULL;
        m_funcp = NULL;
        m_stmtp = NULL;
        m_inWhilep = NULL;
        m_inTracep = NULL;
        m_assignLhs = false;
        iterate(nodep);
    }

    virtual ~IncrementsVisitor() {}

    // VISITORS
    virtual void visit(AstNodeModule* nodep) VL_OVERRIDE {
        UINFO(4, " MOD   " << nodep << endl);
        std::cout << "[mjsob] visit module, node: " << nodep->typeName() << " line: " << nodep->fileline() << std::endl;
        AstNodeModule* origModp = m_modp;
        {
            m_modp = nodep;
            m_funcp = NULL;
            iterateChildren(nodep);
        }
        m_modp = origModp;
    }
    virtual void visit(AstCFunc* nodep) VL_OVERRIDE {
        std::cout << "[mjsob] visit func, node: " << nodep->typeName() << " line: " << nodep->fileline() << std::endl;
        m_funcp = nodep;
        iterateChildren(nodep);
        m_funcp = NULL;
    }

    void startStatement(AstNode* nodep) {
        std::cout << "[mjsob] start stmt, node: " << nodep->typeName() << " line: " << nodep->fileline() << std::endl;
        std::cout << "[mjsob] m_funcp: " << m_funcp << std::endl;
        m_assignLhs = false;
        if (m_funcp) m_stmtp = nodep;
    }
    virtual void visit(AstWhile* nodep) VL_OVERRIDE {
        UINFO(4, "  WHILE  " << nodep << endl);
        startStatement(nodep);
        iterateAndNextNull(nodep->precondsp());
        startStatement(nodep);

        std::cout << "[mjsob] setting while to nodep" << std::endl;
        m_inWhilep = nodep;
        iterateAndNextNull(nodep->condp());
        m_inWhilep = NULL;
        startStatement(nodep);
        iterateAndNextNull(nodep->bodysp());
        iterateAndNextNull(nodep->incsp());
        m_stmtp = NULL;
    }
    virtual void visit(AstNodeAssign* nodep) VL_OVERRIDE {
        startStatement(nodep);
//XXX: Not sure if this is needed
#if 1
        {
            bool noopt = IncrementsAssignVisitor(nodep).noOpt();
            if (noopt && !nodep->user1()) {
                // Need to do this even if not wide, as e.g. a select may be on a wide operator
                UINFO(4, "Deep temp for LHS/RHS\n");
                createDeepTemp(nodep->rhsp(), false);
            }
        }
#endif
        iterateAndNextNull(nodep->rhsp());
        m_assignLhs = true;
        iterateAndNextNull(nodep->lhsp());
        m_assignLhs = false;
        m_stmtp = NULL;
    }
    virtual void visit(AstNodeStmt* nodep) VL_OVERRIDE {
        if (!nodep->isStatement()) {
            iterateChildren(nodep);
            return;
        }
        UINFO(4, "  STMT  " << nodep << endl);
        startStatement(nodep);
        iterateChildren(nodep);
        m_stmtp = NULL;
    }
    virtual void visit(AstTraceInc* nodep) VL_OVERRIDE {
        startStatement(nodep);
        m_inTracep = nodep;
        iterateChildren(nodep);
        m_inTracep = NULL;
        m_stmtp = NULL;
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

        insertBeforeStmt(//backp->backp(),
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
        insertBefore(backp->backp(), varp);

        insertBefore(backp->backp(), new AstAssign(fl, new AstVarRef(fl, varp, true),
                                                  new AstVarRef(fl, vr->varp(), false)));

        insertBefore(backp->backp(),
                     new AstAssign(fl, new AstVarRef(fl, vr->varp(), true),
                                   new AstAdd(fl, new AstVarRef(fl, vr->varp(), false),
                                              newconstp)));

        nodep->replaceWith(new AstVarRef(backp->fileline(), varp, false));

        VL_DO_DANGLING(nodep->deleteTree(), nodep);
    }

    virtual void visit(AstNode* nodep) VL_OVERRIDE { iterateChildren(nodep); }
};

void V3Increments::increments(AstNetlist* nodep) {
    UINFO(2, __FUNCTION__ << ": " << endl);
    { IncrementsVisitor bvisitor(nodep); }  // Destruct before checking
    V3Global::dumpCheckGlobalTree("link", 0, v3Global.opt.dumpTreeLevel(__FILE__) >= 3);
}
