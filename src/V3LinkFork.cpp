// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: Replace increments/decrements with new variables
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
// V3LinkFork's Transformations:
//
// TODO
// XXX
// FIXME
//
//*************************************************************************

#include "config_build.h"
#include "verilatedos.h"

#include "V3Global.h"
#include "V3LinkFork.h"
#include "V3Ast.h"

#include <algorithm>

//######################################################################

class LinkForkVisitor : public AstNVisitor {
    virtual void visit(AstFork* nodep) VL_OVERRIDE {
        iterateChildren(nodep);

        // TODO verify join type first, support only regular join
        if (nodep->joinType().join()) {
            return;
        }

        nodep->backp()->dumpTree(cout, "pre  fork ");


        // TODO convert AstFork to AstBegin
        AstBegin* blockp =
            new AstBegin(nodep->fileline(), nodep->name(),
                         nodep->stmtsp());

        nodep->replaceWith(blockp);

        blockp->backp()->dumpTree(cout, "post fork ");
    }

    virtual void visit(AstNode* nodep) VL_OVERRIDE { iterateChildren(nodep); }
public:
    // CONSTRUCTORS
    explicit LinkForkVisitor(AstNetlist* nodep) {
        iterate(nodep);
    }
    virtual ~LinkForkVisitor() {}
};

//######################################################################
// Task class functions

void V3LinkFork::linkFork(AstNetlist* nodep) {
    UINFO(2, __FUNCTION__ << ": " << endl);
    { LinkForkVisitor bvisitor(nodep); }  // Destruct before checking
    V3Global::dumpCheckGlobalTree("linkFork", 0, v3Global.opt.dumpTreeLevel(__FILE__) >= 3);
}
