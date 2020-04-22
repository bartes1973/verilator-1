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
    // STATE
    int m_modIncrementsNum;  // Var name counter

    // METHODS
    void insertBefore(AstNode* placep, AstNode* newp) {
        AstNRelinker linker;
        placep->unlinkFrBack(&linker);
        newp->addNext(placep);
        linker.relink(newp);
    }

public:
    // CONSTRUCTORS
    explicit IncrementsVisitor(AstNetlist* nodep) {
        m_modIncrementsNum = 0;
        iterate(nodep);
    }

    virtual ~IncrementsVisitor() {}

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


        insertBefore(backp->backp(),
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

        // TODO: change findSigned32Type() to dtype() - right now dtype() does not work, so leaving this for now.
        AstVar* varp
            = new AstVar(fl, AstVarType::BLOCKTEMP, name, backp->backp()->findSigned32DType());
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
